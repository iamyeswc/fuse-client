#include <UUID.h>
#include <chrono>

#include "FuseHttpClient.h"

const std::string FuseHttpClient::traceIdName = "X-Trace-Id";
const std::string FuseHttpClient::albTraceIdName = "X-Amzn-Trace-Id";
const std::string FuseHttpClient::contentType = "Content-Type";
const std::string FuseHttpClient::multiPartFormData = "multipart/form-data";
const std::string FuseHttpClient::jsonData = "application/json";

FuseHttpClient::FuseHttpClient(const std::string &host, unsigned int port)
    : FuseClient(host, port)
{
}

FuseHttpClient::~FuseHttpClient()
{
}


long FuseHttpClient::do_request(const std::string &path,
                                HTTP_REQUEST_METHOD method,
                                Headers &headers,
                                const Body &data,
                                std::string &response)
{

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
        data.prepare(client, traceId, URI, method, m_timeout.load(), headers);

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