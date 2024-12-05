#ifndef _CURL_FACTORY_H_
#define _CURL_FACTORY_H_

#include <string>
#include <map>
#include <curl/curl.h>
#include "./Util/ConnectionFactory.h"

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

#endif // _CURL_FACTORY_H_