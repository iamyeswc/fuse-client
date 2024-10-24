#ifndef _CONNECTION_H
#define _CONNECTION_H

#include <chrono>
#include <memory>

namespace ngmp {
namespace common {

class Connection
{
    friend class ConnectionPool;

public:
    Connection() = default;
    virtual ~Connection() = default;

private:
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    bool is_expired() const
    {
        const std::chrono::seconds::rep idle_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_last_used_time).count();
        return idle_time > m_idle_timeout;
    }

    void set_last_used_time()
    {
        m_last_used_time = std::chrono::steady_clock::now();
    }

    void set_idle_timeout(unsigned int idle_timeout)
    {
        m_idle_timeout = idle_timeout;
    }

public:
    virtual bool connect() = 0;

    virtual bool disconnect() = 0;

private:
    unsigned int m_idle_timeout;
    std::chrono::steady_clock::time_point m_last_used_time;

};

} //namespace common
} //namespace ngmp
#endif // _CONNECTION_H