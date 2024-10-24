#ifndef _FUSEHTTPCLIENT_H
#define _FUSEHTTPCLIENT_H

#include "FuseBaseClient.h"
#include "HttpConnection.h"
#include "LocalUtility.h"
#include <string>
#include <memory>
#include <map>

class FuseHttpClient : public FuseClient
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

private:
    virtual bool test()
    {
        return true;
    }

protected:
    long do_request(const std::string &path,
                    HTTP_REQUEST_METHOD method,
                    Headers &headers,
                    const Body &data,
                    std::string &response);


public:
    static const std::string traceIdName;
    static const std::string albTraceIdName;
    static const std::string contentType;
    static const std::string multiPartFormData;
    static const std::string jsonData;
};

#endif // _FUSEHTTPCLIENT_H
