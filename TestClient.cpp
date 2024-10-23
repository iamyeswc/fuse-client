#include "TestClient.h"

#include <memory>

#include "HttpUtility.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "Utility.cpp"

using namespace std;

bool TestClient::QueryService()
{
    std::shared_ptr<HttpClient> client(new HttpClient(false, false),
                                       [](HttpClient *c) {
                                           if (c)
                                           {
                                               c->Finalize();
                                               delete c;
                                           }
                                       });
    if (!client || !client->Initialize())
    {
        printf("Initialize http client failed");
        return false;
    }
    long startTime = TimerCounterStart();
    const string URL = "https://jsonplaceholder.typicode.com/posts";
    printf("URL:%s", URL.c_str());
    std::map <string, string> headers;
    std::string uuid = "test";
    headers["X-Trace-Id"] = uuid;
    client->SetOptions(URL.c_str(), HTTP_GET, headers, 5);

    long resp_code = 0;
    HTTP_ERROR_CODE err = client->SendRequest(resp_code);
    long elapsedTime = TimerCounterEnd() - startTime;
    if (err == HTTP_SUCCESS && resp_code == 200)
    {
        const char *resp = client->GetResponseBody();
        printf("%s XDR Service query cost=%d ms, response:%s", elapsedTime, resp);

        //parse response
        rapidjson::Document document;
        document.Parse(resp);
        if (document.HasParseError())
        {
            printf("Test Service parse response failed (%d:%d)", document.GetParseError(), document.GetErrorOffset());
            return false;
        }
        else
        {
            printf("Test Service response Body: %s", resp);
        }
    }
    else
    {
        printf("Test Service query resp_code=%d, err=%d, cost=%d ms", resp_code, err, elapsedTime);
        return false;
    }
    return true;
}

unsigned long TimerCounterStart()
{
#ifdef WIN32
    return GetTickCount();
#else
    return OSRelated::GetTick();
#endif
}

unsigned long TimerCounterEnd()
{
#ifdef WIN32
    return GetTickCount();
#else
    return OSRelated::GetTick();
#endif
}