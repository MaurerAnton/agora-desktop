#pragma once

#include <string>
#include <functional>
#include <map>
#include <curl/curl.h>
#include "json.hpp"

using json = nlohmann::json;

struct StreamEvent {
    enum Type { TEXT, THOUGHT, TOOL_CALL, TOOL_CALLS, USAGE, ERROR, DONE, RETRYING };
    Type type;
    std::string text;
    std::string thought;
    std::string tool_name;
    std::string tool_args;
    std::string tool_call_id;
    int token_count = 0;
    int attempt = 0;
    int max_attempts = 0;
};

// Callback for streaming events
using StreamCallback = std::function<void(const StreamEvent&)>;

class HttpClient {
public:
    explicit HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // Configure SOCKS5 proxy for Tor
    void set_proxy(const std::string& host, int port);
    void set_proxy_enabled(bool enabled);

    // Streaming POST (SSE) - returns HTTP status code
    int stream_post(const std::string& url,
                    const std::map<std::string, std::string>& headers,
                    const json& body,
                    StreamCallback callback,
                    int timeout_seconds = 120);

    // Regular POST - returns response body
    std::string post(const std::string& url,
                     const std::map<std::string, std::string>& headers,
                     const json& body,
                     int timeout_seconds = 30);

    // GET request
    std::string get(const std::string& url,
                    const std::map<std::string, std::string>& headers = {});

    // Cancel any active request
    void cancel();

private:
    CURL* curl_ = nullptr;
    std::string proxy_host_;
    int proxy_port_ = 9050;
    bool proxy_enabled_ = false;
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
    static size_t sse_callback(char* ptr, size_t size, size_t nmemb, void* userdata);

    struct SseContext {
        StreamCallback callback;
        std::string buffer;
        bool cancelled = false;
    };
};
