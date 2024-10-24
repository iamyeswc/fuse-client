#ifndef _FUSECLIENT_H
#define _FUSECLIENT_H

#include <memory>
#include <atomic>
#include <thread>
#include <string>
#include "./Util/ConnectionPool.h"
#include "./Util/TimerCounter.h"

class FuseClient
{
public:
    FuseClient(const std::string &host = "", unsigned int port = 80);
    virtual ~FuseClient();

    FuseClient(const FuseClient&) = delete;
    FuseClient& operator=(const FuseClient&) = delete;

    void set_fuse(unsigned int slide_window,
                  unsigned int threshold,
                  unsigned int recovery_interval,
                  unsigned int recovery_threshold);

    void set_host(const std::string &host)
    {
        m_host = host;
    }

    void set_port(unsigned int port)
    {
        m_port = port;
    }

    void set_inplace_retry_times(unsigned int num)
    {
        m_inplace_retry_times = num;
    }

    void set_timeout(unsigned int timeout)
    {
        m_timeout = timeout;
    }

    void set_latency_timeout(unsigned int latency_timeout)
    {
        m_latency_timeout = latency_timeout;
    }

    void set_recovery_triggered(const std::shared_ptr<std::atomic<bool>> &recovery_triggered)
    {
        m_recovery_triggered = recovery_triggered;
    }

    void set_connection_pool(const std::shared_ptr<ngmp::common::ConnectionPool> &connection_pool)
    {
        m_connection_pool = connection_pool;
    }

    std::string destination() const
    {
        return m_host + ":" + std::to_string(m_port);
    }

private:
    virtual bool test() = 0;

    bool in_recovery_thread() const
    {
        return m_recovery_thread.get_id() == std::this_thread::get_id();
    }

    void recovery_func();

public:
    static const unsigned int max_fuse_slide_window;

protected:
    std::shared_ptr<ngmp::common::ConnectionPool> m_connection_pool;
    std::string m_host;
    unsigned int m_port;

    std::atomic<bool> m_in_fuse_mode;
    std::unique_ptr<TimerCounter> m_timer_counter;
    std::shared_ptr<std::atomic<bool>> m_recovery_triggered;
    std::thread m_recovery_thread;

    std::atomic<unsigned int> m_timeout; // unit: second
    std::atomic<unsigned int> m_latency_timeout; // unit: millisecond
    std::atomic<unsigned int> m_inplace_retry_times;

    unsigned int m_fuse_slide_window;
    unsigned int m_fuse_threshold;
    unsigned int m_fuse_recovery_interval;
    unsigned int m_fuse_recovery_threshold;
};

#endif // _FUSECLIENT_H
