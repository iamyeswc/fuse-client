#include <UUID.h>
#include <chrono>

#include "FuseHttpClient.h"

const std::string FuseHttpClient::traceIdName = "X-Trace-Id";
const std::string FuseHttpClient::albTraceIdName = "X-Amzn-Trace-Id";
const std::string FuseHttpClient::contentType = "Content-Type";
const std::string FuseHttpClient::multiPartFormData = "multipart/form-data";
const std::string FuseHttpClient::jsonData = "application/json";
const unsigned int FuseHttpClient::max_fuse_slide_window = 600;

FuseHttpClient::FuseHttpClient(const std::string &host, unsigned int port) :
    m_host(host),
    m_port(port),
    m_in_fuse_mode(false),
    m_inplace_retry_times(0),
    m_timeout(0),
    m_coefficient(1),
    m_latency_timeout(std::numeric_limits<unsigned int>::max())
{
}

FuseHttpClient::~FuseHttpClient()
{
    if (m_recovery_thread.joinable())
    {
        m_in_fuse_mode = false;
        m_recovery_thread.join();
    }
}

void FuseHttpClient::set_fuse(unsigned int silde_window,
                              unsigned int threshold,
                              unsigned int recovery_interval,
                              unsigned int recovery_threshold)
{
#undef __FUNC__
#define __FUNC__ "FuseHttpClient::set_fuse"

    if (silde_window == 0)
    {
        m_timer_counter.reset();
        LOGi1("Disable fuse mode for FuseHttpClient %s since the slide window is zero", destination().c_str());
        return;
    }

    if (silde_window > max_fuse_slide_window)
    {
        silde_window = max_fuse_slide_window;
        LOGi1("Max fuse slide window in second is %u", max_fuse_slide_window);
    }

    m_timer_counter.reset(new TimerCounter(1, silde_window));

    m_fuse_slide_window = silde_window;
    m_fuse_threshold = threshold;
    m_fuse_recovery_interval = recovery_interval;
    m_fuse_recovery_threshold = recovery_threshold;

    LOGd4("Fuse mode: slide_window[%u], threshold[%u], recovery_interval[%u], recovery_threshold[%u]",
          m_fuse_slide_window, m_fuse_threshold, m_fuse_recovery_interval, m_fuse_recovery_threshold);
}

long FuseHttpClient::do_request(const std::string &path,
                                HTTP_REQUEST_METHOD method,
                                Headers &headers,
                                const Body &data,
                                std::string &response)
{
#undef __FUNC__
#define __FUNC__ "FuseHttpClient::do_request"

    //traceId
    std::string traceId;
    Headers::const_iterator iter = headers.find(traceIdName);
    if (iter != headers.cend())
    {
        traceId = iter->second;
    }
    else
    {
        GenUUID(traceId);
        headers[traceIdName] = traceId;
        LOGd1("%s Without trace-id", traceId.c_str());
    }
    headers[albTraceIdName] = "Root=" + traceId;

    long code = -1;
    HTTP_ERROR_CODE err;
    if (m_in_fuse_mode && !in_recovery_thread())
    {
        if (m_recovery_triggered->load())
        {
            LOGd1("%s In fuse mode, ignore the request", traceId.c_str());
            response.clear();
            return code;
        }
        m_in_fuse_mode = false;
        if (m_timer_counter)
        {
            m_timer_counter->reset();
        }
        LOGd1("%s leave fuse mode, and restart count", traceId.c_str());
    }

    std::shared_ptr<ngmp::common::Connection> connection;
    if (m_connection_pool)
    {
        connection = m_connection_pool->get_connection(destination());
    }
    if (!connection)
    {
        LOGx1("%s Not get valid connection from pool", traceId.c_str());
        response.clear();
        return code;
    }

    std::shared_ptr<HttpClient> client = std::dynamic_pointer_cast<HttpClient>(connection);
    const unsigned int inplace_retry_times = in_recovery_thread() ? 0 : m_inplace_retry_times.load();
    int64_t max_latency = 0;
    for (unsigned int i = 0; i <= inplace_retry_times; ++i)
    {
        response.clear();

        //do request
        const std::string URI = "http://" + destination() + path;
        data.prepare(client, traceId, URI, method, m_timeout.load() * m_coefficient, headers);

        LOGd3("%s Do request: %s %s", traceId.c_str(), HttpClient::methodName(method).c_str(), URI.c_str());
        for (const std::pair<const std::string, const std::string> &header : headers)
        {
            LOGd3("%s Request header %s : %s", traceId.c_str(), header.first.c_str(), header.second.c_str());
        }

        std::chrono::time_point<std::chrono::steady_clock> startTime = std::chrono::steady_clock::now();
        code = 0;
        err = client->SendRequest(code);
        if (client->GetResponseBody())
        {
            response = client->GetResponseBody();
        }
        std::chrono::time_point<std::chrono::steady_clock> endTime = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        max_latency = std::max(max_latency, latency);

        if (err == HTTP_SUCCESS)
        {
            LOGd5("%s request URL: %s, response: %ld %s, latency: %ldms", traceId.c_str(), URI.c_str(), code, response.c_str(), latency);
            break;
        }
        else
        {
            LOGx4("%s request URL: %s, response: %s, latency: %ldms",
                    traceId.c_str(), URI.c_str(), std::to_string(code).append(" ").append(response).c_str(), latency);
            if (err == HTTP_CLIENT_ERROR)
            {
                break;
            }
        }
    }
    if (!m_connection_pool->release_connection(destination(), connection))
    {
        LOGx1("%s fail to release connection", traceId.c_str());
        return code;
    }

    if ((err != HTTP_SUCCESS && err != HTTP_CLIENT_ERROR) || max_latency > m_latency_timeout)
    {
        if (m_timer_counter && !in_recovery_thread())
        {
            m_timer_counter->add_count(1);
            if (m_timer_counter->get_sum_of_last_slices(m_fuse_slide_window) >= m_fuse_threshold)
            {
                bool expected = false;
                if (m_in_fuse_mode.compare_exchange_strong(expected, true))
                {
                    LOGx3("%s %u  errors in %u seconds, enter fuse mode", traceId.c_str(), m_fuse_threshold, m_fuse_slide_window);
                    if (!m_recovery_triggered)
                    {
                        m_recovery_triggered = std::make_shared<std::atomic<bool>>(false);
                    }
                    if (m_recovery_triggered->compare_exchange_strong(expected, true))
                    {
                        if (m_recovery_thread.joinable())
                        {
                            m_recovery_thread.join();
                        }
                        m_recovery_thread = std::thread(&FuseHttpClient::recovery_func, this);
                    }
                }
            }
        }
    }

    return code;
}

void FuseHttpClient::JsonBody::prepare(const std::shared_ptr<HttpClient> &client,
                                       const std::string &traceId,
                                       const std::string &URI,
                                       HTTP_REQUEST_METHOD method,
                                       unsigned int timeout,
                                       Headers &headers) const
{
#undef __FUNC__
#define __FUNC__ "FuseHttpClient::JsonBody::prepare"

    headers[contentType] = jsonData;
    if (!m_data.empty())
    {
        client->PreparePostData(m_data.data(), m_data.size());
        LOGd2("%s Request body: %s", traceId.c_str(), m_data.c_str());
    }
    client->SetOptions(URI.c_str(), method, headers, timeout);
}

void FuseHttpClient::MultiPartBody::prepare(const std::shared_ptr<HttpClient> &client,
                                            const std::string &traceId,
                                            const std::string &URI,
                                            HTTP_REQUEST_METHOD method,
                                            unsigned int timeout,
                                            Headers &headers) const
{
#undef __FUNC__
#define __FUNC__ "FuseHttpClient::MultiPartBody::prepare"

    headers[contentType] = multiPartFormData;
    for (auto &form_data : m_data)
    {
        const std::string &key = form_data.key;
        const std::vector<unsigned char> &in = form_data.in;
        const std::string &name = form_data.name;
        LOGd3("%s Request key: %s, name: %s", traceId.c_str(), key.c_str(), name.c_str());
        client->SetMultiPartBuffer(key, (const char*)in.data(), in.size(), name);
    }
    client->SetMultiPartOptions(URI.c_str(), headers, timeout);
}

void FuseHttpClient::recovery_func()
{
#undef __FUNC__
#define __FUNC__ "FuseHttpClient::recovery_func"

    std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now() + std::chrono::seconds(m_fuse_recovery_interval);

    unsigned int recovery_count = 0;
    while (m_in_fuse_mode)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (std::chrono::steady_clock::now() < next)
        {
            continue;
        }

        LOGd1("%s in fuse mode, try a test", destination().c_str());
        if (test())
        {
            ++recovery_count;
            LOGi2("%s %u times successful test", destination().c_str(), recovery_count);
            if (recovery_count >= m_fuse_recovery_threshold)
            {
                LOGi2("%s equal the threshold %u, leave fuse mode", destination().c_str(), m_fuse_recovery_threshold);
                if (m_timer_counter)
                {
                    m_timer_counter->reset();
                }
                m_in_fuse_mode = false;
            }
        }
        else
        {
            LOGx1("%s test failed", destination().c_str());
            recovery_count = 0;
        }

        next = std::chrono::steady_clock::now() + std::chrono::seconds(m_fuse_recovery_interval);
    }

    m_recovery_triggered->store(false);
}