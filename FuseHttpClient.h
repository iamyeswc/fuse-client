#ifndef _FUSEHTTPCLIENT_H
#define _FUSEHTTPCLIENT_H

#include "./Util/ConnectionPool.h"
#include "./Util/TimerCounter.h"
#include "HttpUtility.h"
#include "LocalUtility.h"
#include <string>
#include <memory>
#include <atomic>
#include <thread>

class FuseHttpClient
{
public:
    using Headers = std::map<std::string, std::string>;

    struct Body
    {
        virtual ~Body() {}

        virtual void prepare(const std::shared_ptr<HttpClient> &client,
                             const std::string &traceId,
                             const std::string &URI,
                             HTTP_REQUEST_METHOD method,
                             unsigned int timeout,
                             Headers &headers) const = 0;
    };

    class JsonBody : public Body
    {
    public:
        JsonBody() = default;
        JsonBody(const std::string &data) : m_data(data)
        {}

        void prepare(const std::shared_ptr<HttpClient> &client,
                     const std::string &traceId,
                     const std::string &URI,
                     HTTP_REQUEST_METHOD method,
                     unsigned int timeout,
                     Headers &headers) const;
    private:
        std::string m_data;
    };

    struct FormData
    {
        std::string key;
        std::vector<unsigned char> in;
        std::string name;
    };

    class MultiPartBody : public Body
    {
    public:
        void emplace_back(const FormData &form)
        {
            m_data.emplace_back(form);
        }

        std::vector<FormData>::size_type size() const
        {
            return m_data.size();
        }

        bool empty() const
        {
            return m_data.empty();
        }

        void prepare(const std::shared_ptr<HttpClient> &client,
                     const std::string &traceId,
                     const std::string &URI,
                     HTTP_REQUEST_METHOD method,
                     unsigned int timeout,
                     Headers &headers) const;
    private:
        std::vector<FormData> m_data;
    };

    FuseHttpClient(const std::string &host = "", unsigned int port = 80);
    virtual ~FuseHttpClient();

    FuseHttpClient(const FuseHttpClient&) = delete;
    FuseHttpClient& operator=(const FuseHttpClient&) = delete;

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

    void set_coefficient(unsigned int coefficient)
    {
        m_coefficient = coefficient;
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

    std::string destination()
    {
        return m_host + ":" + std::to_string(m_port);
    }

private:
    virtual bool test()
    {
        return true;
    }

    bool in_recovery_thread() const
    {
        return m_recovery_thread.get_id() == std::this_thread::get_id();
    }

protected:
    long do_request(const std::string &path,
                    HTTP_REQUEST_METHOD method,
                    Headers &headers,
                    const Body &data,
                    std::string &response);

private:
    void recovery_func();

public:
    static const std::string traceIdName;
    static const std::string albTraceIdName;
    static const std::string contentType;
    static const std::string multiPartFormData;
    static const std::string jsonData;
    static const unsigned int max_fuse_slide_window;

private:
    std::shared_ptr<ngmp::common::ConnectionPool> m_connection_pool;
    std::string m_host;
    unsigned int m_port;

    std::atomic<bool> m_in_fuse_mode;
    std::unique_ptr<TimerCounter> m_timer_counter;
    std::shared_ptr<std::atomic<bool>> m_recovery_triggered;
    std::thread m_recovery_thread;

    std::atomic<unsigned int> m_timeout; // unit: second
    std::atomic<unsigned int> m_coefficient; // timeout coefficient
    std::atomic<unsigned int> m_latency_timeout; // unit: millisecond
    std::atomic<unsigned int> m_inplace_retry_times;

    unsigned int m_fuse_slide_window;
    unsigned int m_fuse_threshold;
    unsigned int m_fuse_recovery_interval;
    unsigned int m_fuse_recovery_threshold;
};

#endif