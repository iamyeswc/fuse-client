#include "HttpUtility.h"
#include "LocalUtility.h"

#include <mutex>


class HttpClientImpl
{
public:
    HttpClientImpl(bool verify_peer, bool verify_host) : curl(0), headers(NULL), response_body(),
        ssl_verify_peer(verify_peer), ssl_verify_host(verify_host)
    {
    }

    static bool HTTPS_GLOBAL_INITIALIZE()
    {
        static std::once_flag init_flag;
        CURLcode ret = CURL_LAST;
        std::call_once(init_flag, [&ret](){ ret = curl_global_init(CURL_GLOBAL_ALL); });
        return ret == CURLE_OK;
    }

    static void HTTPS_GLOBAL_FINALIZE()
    {
        curl_global_cleanup();
    }

    bool Initialize()
    {
        curl = curl_easy_init();
        return curl != 0;
    }

    bool Finalize()
    {
        if (response_body.memory)
        {
            free(response_body.memory);
            response_body.memory = NULL;
        }

        if (curl)
        {
            curl_easy_cleanup(curl);
            curl = 0;
        }

        if(formpost)
        {
            curl_formfree(formpost);
            formpost = nullptr;
        }
        return true;
    }
    void SetProxy(const char* proxy, int port, const char* uid, const char* pwd)
    {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy);
        curl_easy_setopt(curl, CURLOPT_PROXYPORT, port);

        std::string strUID = Escape(uid, strlen(uid));
        std::string strPWD = Escape(pwd, strlen(pwd));
        std::string strTemp = strUID + ":" + strPWD;
        curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, strTemp.c_str());
    }

    void SetOptions(const char* url, HTTP_REQUEST_METHOD method,
        std::map<std::string, std::string>& http_headers, unsigned int timeout)
    {
#undef  __FUNC__
#define __FUNC__ "HttpClientImpl::SetOptions"

        //Disable proxy, if need proxy, set in another function
        curl_easy_setopt(curl, CURLOPT_PROXY, "");
        curl_easy_setopt(curl, CURLOPT_URL, url);

        if (method == HTTP_GET)
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        else if (method == HTTP_POST)
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
        else if (method == HTTP_PUT)
            curl_easy_setopt(curl, CURLOPT_PUT, 1L);
        else//DELETE is not supported
            ;

        if (!ssl_verify_peer)
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        if (!ssl_verify_host)
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        //Default timeout is 0 (zero) which means it never times out during transfer.
        //unit is second,  for whole request
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

        for (std::map<std::string, std::string>::const_iterator iter = http_headers.begin();
            iter != http_headers.end(); iter++)
        {
            std::string header = iter->first + ": " + iter->second;
            LOGd1("\t%s", header.c_str());
            headers = curl_slist_append(headers, header.c_str());
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        //set to 1 tells the library to fail the request if the HTTP code returned is equal to or larger than 400
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

        //set to is 1, libcurl will not use any functions that install signal handlers or 
        //any functions that cause signals to be sent to the process. 
        //This option is here to allow multi-threaded unix applications to still set/use all timeout options etc, 
        //without risking getting signals.
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        //output request and response verbose on standard output
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        //to get the response body
        if (response_body.memory)
            free(response_body.memory);
        response_body.memory = (char*)malloc(1);  /* will be grown as needed by the realloc */
        response_body.size = 0;    /* no data at this point */
        response_body.memory[0] = '\0';
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response_body);
    }

    void PreparePostData(const char* data, unsigned long size)
    {
        /* size of the POST data */
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, size);
        /* pass in a pointer to the data - libcurl will not copy */
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    }

    std::string Escape(const char* input, unsigned int size)
    {
        char *escaped = curl_easy_escape(curl, input, size);
        if (!escaped)
            return "";
        std::string output = escaped;
        curl_free(escaped);
        return output;
    }

    HTTP_ERROR_CODE SendRequest(long &response_code)
    {
#undef  __FUNC__
#define __FUNC__ "HttpClientImpl::SendRequest"
        response_code = 0;

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        headers = NULL;
        curl_formfree(formpost);
        formpost = lastptr = nullptr;
        if (res != CURLE_OK)
        {
            LOGx2("curl_easy_perform() failed, %d: %s", res, curl_easy_strerror(res));
            if (res == CURLE_COULDNT_RESOLVE_HOST ||
                res == CURLE_COULDNT_CONNECT)
                return HTTP_NETWORK_ERROR;
            else if (res == CURLE_OPERATION_TIMEDOUT)
                return HTTP_TIMEOUT;
            else
            {
                res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
                if ((CURLE_OK == res) && response_code)
                {
                    LOGd1("http request return code %d", response_code);
                    if (response_code >= 500)
                        return HTTP_SERVER_ERROR;
                    else if (response_code >= 400)
                        return HTTP_CLIENT_ERROR;
                    else if (response_code == 302)
                        return HTTP_REPORT_SERVICE_RETRY;
                    else if (response_code >= 0)
                        return HTTP_SUCCESS;
                    else
                        return HTTP_UNKNOWN;
                }
                else
                    return HTTP_UNKNOWN;
            }
        }
        else
        {
            res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            if ((CURLE_OK == res) && response_code)
            {
                LOGd1("curl_easy_perform() success, http request return code %d", response_code);
                if (response_code == 302)
                    return HTTP_REPORT_SERVICE_RETRY;
            }
            return HTTP_SUCCESS;
        }
    }

    char* GetResponseBody()
    {
        if (response_body.memory)
            return response_body.memory;
        else
            return "";
    }

    void SetMultiPartFile(std::string& key, std::string &path){
        curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, key.c_str(), CURLFORM_FILE, path.data(), CURLFORM_END);
    }
    void SetMultiPartBuffer(std::string& key, const char *buffer, size_t size, const std::string &name){
        curl_formadd(&formpost, &lastptr,
              CURLFORM_COPYNAME, key.c_str(),
              CURLFORM_BUFFER, name.c_str(), //file name in header
              CURLFORM_BUFFERPTR, buffer, //buffer
              CURLFORM_BUFFERLENGTH, size, //
              CURLFORM_END);
    }

    void SetMultiPartOptions(const char* url, std::map<std::string, std::string>& http_headers, unsigned int timeout)
    {
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);        
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
        for (std::map<std::string, std::string>::const_iterator iter = http_headers.begin();
            iter != http_headers.end(); iter++)
        {
            std::string header = iter->first + ": " + iter->second;
            headers = curl_slist_append(headers, header.c_str());
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);

        //To get the response
        if (response_body.memory)
            free(response_body.memory);
        response_body.memory = (char*)malloc(1);  /* will be grown as needed by the realloc */
        response_body.size = 0;    /* no data at this point */
        response_body.memory[0] = '\0';
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response_body);

        //set to 1 tells the library to fail the request if the HTTP code returned is equal to or larger than 400
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    }
private:
    CURL* curl;
    struct curl_httppost * formpost = NULL;
    struct curl_httppost * lastptr = NULL;
    struct curl_slist *headers;
    struct MemoryStruct
    {
        char *memory;
        size_t size;
        MemoryStruct() : memory(0), size(0){}
    };
    MemoryStruct response_body;
    bool ssl_verify_peer;
    bool ssl_verify_host;

    static size_t WriteMemoryCallback(
        void *contents, size_t size, size_t nmemb, void *userp)
    {
        size_t realsize = size * nmemb;
        MemoryStruct *mem = (MemoryStruct *)userp;

        mem->memory = (char*)realloc(mem->memory, mem->size + realsize + 1);
        if (mem->memory == NULL) {
            /* out of memory! */
            return 0;
        }

        memcpy(&(mem->memory[mem->size]), contents, realsize);
        mem->size += realsize;
        mem->memory[mem->size] = 0;

        return realsize;
    }
};

bool HTTPS_GLOBAL_INITIALIZE()
{
    return HttpClientImpl::HTTPS_GLOBAL_INITIALIZE();
}

bool HTTPS_GLOBAL_FINALIZE()
{
    HttpClientImpl::HTTPS_GLOBAL_FINALIZE();
    return true;
}

HttpClient::HttpClient(bool verify_peer, bool verify_host)
{
    impl = new HttpClientImpl(verify_peer, verify_host);
}

HttpClient::~HttpClient()
{
    delete impl;
}


bool HttpClient::Initialize()
{
    return impl->Initialize();
}

bool HttpClient::Finalize()
{
    return impl->Finalize();
}

void  HttpClient::SetHttpProxy(const char* proxy, int port, const char* uid, const char* pwd)
{
    impl->SetProxy(proxy, port, uid, pwd);
}

void HttpClient::SetOptions(const char* url, HTTP_REQUEST_METHOD method,
    std::map<std::string, std::string>& http_headers, unsigned int timeout)
{
    impl->SetOptions(url, method, http_headers, timeout);
}

void HttpClient::PreparePostData(const char* data, unsigned int size)
{
    impl->PreparePostData(data, size);
}

std::string HttpClient::Escape(const char* input, unsigned int size)
{
    return impl->Escape(input, size);
}

HTTP_ERROR_CODE HttpClient::SendRequest(long &resp_code)
{
    return impl->SendRequest(resp_code);
}

char* HttpClient::GetResponseBody()
{
    return impl->GetResponseBody();
}

void HttpClient::SetMultiPartFile(std::string key, std::string &path){
    impl->SetMultiPartFile(key, path);
}

void HttpClient::SetMultiPartBuffer(std::string key, const char *buffer, size_t size, const std::string &name){
    impl->SetMultiPartBuffer(key,buffer,size,name);
}

void HttpClient::SetMultiPartOptions(const char* url, std::map<std::string, std::string>& http_headers, unsigned int timeout){
    impl->SetMultiPartOptions(url, http_headers, timeout);
}

bool HttpClient::connect() {
    return Initialize();
}

bool HttpClient::disconnect() {
    return Finalize();
}