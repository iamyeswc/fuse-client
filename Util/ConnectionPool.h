#ifndef _CONNECTIONPOOL_H
#define _CONNECTIONPOOL_H

#include <memory>
#include <thread>
#include <mutex>
#include <chrono>
#include <cassert>
#include <atomic>
#include <numeric>
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <condition_variable>

#include "Connection.h"
#include "ConnectionFactory.h"

namespace ngmp {
namespace common {

class ConnectionPool final
{
    using Connections = std::unordered_set<std::shared_ptr<Connection>>;

public:
    explicit ConnectionPool(unsigned int max_connections = 0, unsigned int idle_timeout = 60, unsigned int clean_interval = 60) :
        m_max_connections(max_connections), m_idle_timeout(idle_timeout), m_clean_interval(clean_interval)
    {
        m_stop = false;
        assert(idle_timeout > 0);
        assert(max_connections >= 0);
        assert(clean_interval > 0);

        start_clean_connection();
    }

    ~ConnectionPool()
    {
        if (m_clean_thread.joinable())
        {
            m_stop = true;
            m_clean_thread.join();
        }
    }

private:
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

public:
    /*
     * timeout_time:
     * -1: block until valid connection
     * 0:  not wait
     * >0: wait until timewait or valid connection
    */
    std::shared_ptr<Connection> get_connection(const std::string &destination, int timeout = 0)
    {
        if (m_stop)
        {
            return nullptr;
        }

        timeout = timeout < 0 ? -1 : timeout;
        const std::chrono::seconds timeout_time = std::chrono::duration<unsigned int>(timeout);
        auto valid_idle_connection = [&destination, this]()
        {
            if (m_connections_idle.find(destination) == m_connections_idle.end() ||
                m_connections_idle.at(destination).empty())
            {
                return false;
            }
            auto iter = std::find_if(m_connections_idle.at(destination).cbegin(), m_connections_idle.at(destination).cend(),
                                    [](const std::shared_ptr<Connection> &connection)
                                    {
                                        return !connection->is_expired();
                                    }
            );
            return iter != m_connections_idle.at(destination).cend();
        };
        std::unique_lock<std::mutex> lock(m_connections_mtx);

        if (m_connnections_condition.wait_for(lock, timeout_time, valid_idle_connection))
        {
            for (const std::shared_ptr<Connection> connection : m_connections_idle[destination])
            {
                if (!connection->is_expired())
                {
                    m_connections_busy[destination].insert(connection);
                    m_connections_idle[destination].erase(connection);
                    return connection;
                }
            }
        }
        if ((m_max_connections == 0 || all_size(destination) < m_max_connections) && m_connection_factory)
        {
            std::shared_ptr<Connection> connection = m_connection_factory->create_connection();
            if (connection)
            {
                connection->set_idle_timeout(m_idle_timeout);
                m_connections_busy[destination].insert(connection);
                return connection;
            }
        }
        return nullptr;
    }

    bool release_connection(const std::string &destination, std::shared_ptr<Connection> connection)
    {
        if (connection == nullptr || m_connections_busy.find(destination) == m_connections_busy.end())
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_connections_mtx);
        m_connections_idle[destination].insert(connection);
        m_connections_busy[destination].erase(connection);
        connection->set_last_used_time();
        m_connnections_condition.notify_one();
        return true;
    }

    void set_connection_factory(std::shared_ptr<ConnectionFactory> connection_factory)
    {
        m_connection_factory = connection_factory;
    }

private:
    void start_clean_connection()
    {
        m_clean_thread = std::thread(&ConnectionPool::clean_connection, this);
    }

    void clean_connection()
    {
        std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now() + std::chrono::seconds(m_clean_interval);
        while (!m_stop)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (std::chrono::steady_clock::now() >= next)
            {
                std::lock_guard<std::mutex> lock(m_connections_mtx);
                auto iter_connections = m_connections_idle.begin();
                while (iter_connections != m_connections_idle.end())
                {
                    Connections &connections = iter_connections->second;
                    auto iter_connection = connections.begin();
                    while (iter_connection != connections.end())
                    {
                        if ((*iter_connection)->is_expired())
                        {
                            iter_connection = connections.erase(iter_connection);
                        }
                        else
                        {
                            ++iter_connection;
                        }
                    }
                    if (connections.empty())
                    {
                        iter_connections = m_connections_idle.erase(iter_connections);
                    }
                    else
                    {
                        ++iter_connections;
                    }
                }
                next = std::chrono::steady_clock::now() + std::chrono::seconds(m_clean_interval);
            }
        }
    }

    unsigned int idle_size(const std::string &destination) const
    {
        return m_connections_idle.find(destination) != m_connections_idle.end() ?
                m_connections_idle.at(destination).size() : 0;
    }

    unsigned int busy_size(const std::string &destination) const
    {
        return m_connections_busy.find(destination) != m_connections_busy.end() ?
                m_connections_busy.at(destination).size() : 0;
    }

    unsigned int all_size(const std::string &destination) const
    {
        return  idle_size(destination) + busy_size(destination);
    }

private:
    std::unordered_map<std::string, Connections> m_connections_idle;  // key: destination
    std::unordered_map<std::string, Connections> m_connections_busy;

    std::shared_ptr<ConnectionFactory> m_connection_factory;
    unsigned int m_idle_timeout;
    unsigned int m_max_connections; // equal for every destination, 0: unlimited connections
    unsigned int m_clean_interval;
    std::thread m_clean_thread;

    std::atomic<bool> m_stop;
    std::mutex m_connections_mtx;
    std::condition_variable m_connnections_condition;
};

} //namespace common
} //namespace ngmp
#endif // _CONNECTIONPOOL_H
