#ifndef _CONNECTIONFACTORY_H
#define _CONNECTIONFACTORY_H

#include "Connection.h"
#include <memory>

namespace ngmp {
namespace common {

class ConnectionFactory
{
public:
    ConnectionFactory() = default;
    virtual ~ConnectionFactory() = default;

    virtual std::shared_ptr<Connection> create_connection() = 0;

private:
    ConnectionFactory(const ConnectionFactory&) = delete;
    ConnectionFactory& operator=(const ConnectionFactory&) = delete;
};

} //namespace common
} //namespace ngmp
#endif // _CONNECTIONFACTORY_H