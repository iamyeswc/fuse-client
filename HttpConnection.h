#ifndef _HTTPSUTILITY_INCLUDED_
#define _HTTPSUTILITY_INCLUDED_

#include <string>
#include <map>
#include <curl/curl.h>
#include "./Util/Connection.h"
#include "./Util/ConnectionFactory.h"

class HttpConnectionImpl;

enum HTTP_REQUEST_METHOD
{
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
};

enum HTTP_ERROR_CODE
{
    HTTP_SUCCESS,
    HTTP_TIMEOUT,
    HTTP_NETWORK_ERROR,
    HTTP_CLIENT_ERROR,
    HTTP_SERVER_ERROR,
    HTTP_UNKNOWN,
    HTTP_REPORT_SERVICE_RETRY,
};

//These two function should be called only once
bool HTTPS_GLOBAL_INITIALIZE();
bool HTTPS_GLOBAL_FINALIZE();

class HttpConnection : public ngmp::common::Connection
{
public:
    HttpConnection(bool verify_peer, bool verify_host);
    virtual ~HttpConnection();

    virtual bool Initialize();
    virtual bool Finalize();

    void SetHttpProxy(const char* proxy, int port, const char* uid, const char* pwd);
    void SetOptions(const char* url, HTTP_REQUEST_METHOD method,
        std::map<std::string, std::string>& http_headers, unsigned int timeout);
    void PreparePostData(const char* data, unsigned int size);

    std::string Escape(const char* input, unsigned int size);

    HTTP_ERROR_CODE SendRequest(long &resp_code);
    char* GetResponseBody();

    void SetMultiPartFile(std::string  key, std::string & path);
    void SetMultiPartBuffer(std::string key, const char *buffer, size_t size, const std::string &name = "filename");
    void SetMultiPartOptions(const char *url, std::map<std::string, std::string> &http_headers, unsigned int timeout);

    static const std::string methodName(HTTP_REQUEST_METHOD method)
    {
        switch (method)
        {
            case HTTP_GET:      return "GET";
            case HTTP_POST:     return "POST";
            case HTTP_PUT:      return "PUT";
            case HTTP_DELETE:   return "DELETE";
        }
    }

private:
    virtual bool connect() override;

    virtual bool disconnect() override;

private:
    HttpConnectionImpl* impl;
};

class CurlFactory : public ngmp::common::ConnectionFactory
{
public:
    virtual std::shared_ptr<ngmp::common::Connection> create_connection() override
    {
        std::shared_ptr<ngmp::common::Connection> connection(new HttpConnection(false, false),
            [](ngmp::common::Connection *connection)
            {
                if (connection)
                {
                    connection->disconnect();
                }
                delete connection;
            }
        );
        if (connection)
        {
            connection->connect();
        }
        return connection;
    }
};

#endif