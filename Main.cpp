#include "FuseHttpClient.h"

//global service client
thread_local std::unordered_map<std::string, std::shared_ptr<FuseHttpClient>> g_tls_service_client;
std::shared_ptr<ngmp::common::ConnectionPool> ScanService::connectionPool = nullptr;

int main() {
    std::shared_ptr<ngmp::common::KeyNode> hRootKey(OpenConfig(CONFIG_FILE_DEFAULT),
    [](ngmp::common::KeyNode* p)
    {
        CloseConfig(p);
    });

    static std::once_flag flag;
    std::call_once(flag, []()
    {
        connectionPool = std::make_shared<ngmp::common::ConnectionPool>();
        if (connectionPool)
        {
            connectionPool->set_connection_factory(std::make_shared<CurlFactory>());
        }
    });

#define ADDSERVICECLIENTOBJ(SERVICECLIENT, SERVICENAME, OBJKEY)                                                           \
    std::shared_ptr<ngmp::common::KeyNode> h##SERVICECLIENT(ISVW_CFG_OpenKey(hRootKey.get(), OBJKEY),                     \
    [] (ngmp::common::KeyNode* p)                                                                                         \
    {                                                                                                                     \
        ISVW_CFG_CloseKey(p);                                                                                             \
    });                                                                                                                   \
    if (h##SERVICECLIENT)                                                                                                 \
    {                                                                                                                     \
        g_tls_service_client.emplace(SERVICENAME, std::make_shared<SERVICECLIENT>(h##SERVICECLIENT.get(), connectionPool));   \
    }                                                                                                                     \
    else                                                                                                                  \
    {                                                                                                                     \
        LOGx1("Unable to initialize %s service client.", SERVICENAME);                                                    \
    }                                                                                                                     \

    //Mail scan service client
    ADDSERVICECLIENTOBJ(TiClient, "ti", "Scan\\ThreatIntelligence");
    ADDSERVICECLIENTOBJ(SalClient, "sal", "Scan\\SalUrlExtract");
    ADDSERVICECLIENTOBJ(NrdClient, "nrd", "Scan\\NRD");
    ADDSERVICECLIENTOBJ(DMARCMonitorClient, "dmarcmonitor", "Scan\\DMARCMonitor");


    return 0;
}