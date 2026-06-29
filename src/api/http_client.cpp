#include "api/http_client.hpp"
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <cstring>
#include <chrono>
#include <thread>

HttpClient::HttpClient() {
    curl_ = curl_easy_init();
    if (!curl_) throw std::runtime_error("Failed to init libcurl");
}

HttpClient::~HttpClient() {
    if (curl_) curl_easy_cleanup(curl_);
}

void HttpClient::set_proxy(const std::string& host, int port) {
    proxy_host_ = host;
    proxy_port_ = port;
}

void HttpClient::set_proxy_enabled(bool enabled) {
    proxy_enabled_ = enabled;
}

void HttpClient::cancel() {
    // Signal cancellation via proxy setting - CURL handles it
}

size_t HttpClient::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* str = static_cast<std::string*>(userdata);
    size_t total = size * nmemb;
    str->append(ptr, total);
    return total;
}

size_t HttpClient::sse_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<SseContext*>(userdata);
    if (ctx->cancelled) return 0; // Abort transfer

    size_t total = size * nmemb;
    ctx->buffer.append(ptr, total);

    // Process complete lines
    size_t pos;
    while ((pos = ctx->buffer.find('\n')) != std::string::npos) {
        std::string line = ctx->buffer.substr(0, pos);
        ctx->buffer.erase(0, pos + 1);

        // Trim \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // SSE: lines prefixed with "data: "
        if (line.find("data: ") == 0) {
            std::string data = line.substr(6);

            // Check for [DONE]
            if (data == "[DONE]") {
                StreamEvent evt;
                evt.type = StreamEvent::DONE;
                ctx->callback(evt);
                continue;
            }

            try {
                json j = json::parse(data);

                // OpenAI-compatible SSE
                if (j.contains("choices")) {
                    auto& choices = j["choices"];
                    if (!choices.empty()) {
                        auto& delta = choices[0].contains("delta") ? choices[0]["delta"] : choices[0];

                        // Text content
                        if (delta.contains("content") && !delta["content"].is_null()) {
                            std::string content = delta["content"].get<std::string>();
                            if (!content.empty()) {
                                StreamEvent evt;
                                evt.type = StreamEvent::TEXT;
                                evt.text = content;
                                ctx->callback(evt);
                            }
                        }

                        // Reasoning/thinking content (DeepSeek, Qwen, OpenAI o1)
                        if (delta.contains("reasoning_content") && !delta["reasoning_content"].is_null()) {
                            StreamEvent evt;
                            evt.type = StreamEvent::THOUGHT;
                            evt.thought = delta["reasoning_content"].get<std::string>();
                            ctx->callback(evt);
                        }

                        // Tool calls
                        if (delta.contains("tool_calls")) {
                            for (auto& tc : delta["tool_calls"]) {
                                StreamEvent evt;
                                evt.type = StreamEvent::TOOL_CALL;
                                if (tc.contains("id") && !tc["id"].is_null())
                                    evt.tool_call_id = tc["id"].get<std::string>();
                                if (tc.contains("function")) {
                                    auto& fn = tc["function"];
                                    if (fn.contains("name") && !fn["name"].is_null())
                                        evt.tool_name = fn["name"].get<std::string>();
                                    if (fn.contains("arguments") && !fn["arguments"].is_null())
                                        evt.tool_args = fn["arguments"].get<std::string>();
                                }
                                ctx->callback(evt);
                            }
                        }
                    }
                }

                // Anthropic SSE format
                if (j.contains("type")) {
                    std::string t = j["type"];
                    if (t == "content_block_delta") {
                        auto& delta = j["delta"];
                        if (delta.contains("type")) {
                            std::string dt = delta["type"];
                            if (dt == "text_delta" && delta.contains("text")) {
                                StreamEvent evt;
                                evt.type = StreamEvent::TEXT;
                                evt.text = delta["text"].get<std::string>();
                                ctx->callback(evt);
                            } else if (dt == "thinking_delta" && delta.contains("thinking")) {
                                StreamEvent evt;
                                evt.type = StreamEvent::THOUGHT;
                                evt.thought = delta["thinking"].get<std::string>();
                                ctx->callback(evt);
                            } else if (dt == "input_json_delta" && delta.contains("partial_json")) {
                                StreamEvent evt;
                                evt.type = StreamEvent::TOOL_CALL;
                                evt.tool_args = delta["partial_json"].get<std::string>();
                                ctx->callback(evt);
                            }
                        }
                    }
                }

                // Usage
                if (j.contains("usage")) {
                    auto& u = j["usage"];
                    StreamEvent evt;
                    evt.type = StreamEvent::USAGE;
                    if (u.contains("total_tokens"))
                        evt.token_count = u["total_tokens"].get<int>();
                    ctx->callback(evt);
                }
            } catch (const json::parse_error&) {
                // Skip unparseable lines
            }
        }
    }
    return total;
}

static std::string url_encode(const std::string& url) {
    std::string result;
    for (char c : url) {
        if (c == ' ') { result += "%20"; continue; }
        if (c == '#') { result += "%23"; continue; }
        if (c == '%') { result += "%25"; continue; }
        if (c == '+') { result += "%2B"; continue; }
        if (c == '@') { result += "%40"; continue; }
        if (c == '?') { result += "%3F"; continue; }
        if (c == '&') { result += "%26"; continue; }
        if (c == '=') { result += "%3D"; continue; }
        result += c;
    }
    return result;
}

int HttpClient::stream_post(const std::string& url,
                            const std::map<std::string, std::string>& headers,
                            const json& body,
                            StreamCallback callback,
                            int timeout_seconds) {
    if (!curl_) return -1;

    std::string body_str = body.dump();

    struct curl_slist* header_list = nullptr;
    for (const auto& [k, v] : headers) {
        std::string h = k + ": " + v;
        header_list = curl_slist_append(header_list, h.c_str());
    }
    // For SSE, we need accept headers
    header_list = curl_slist_append(header_list, "Accept: text/event-stream");
    header_list = curl_slist_append(header_list, "Cache-Control: no-cache");

    SseContext ctx;
    ctx.callback = callback;
    ctx.cancelled = false;

    std::string encoded_url = url_encode(url);
    curl_easy_setopt(curl_, CURLOPT_URL, encoded_url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, body_str.size());
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, sse_callback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, (long)timeout_seconds);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 15L);

    // Tor SOCKS5 proxy
    if (proxy_enabled_) {
        std::string proxy = proxy_host_ + ":" + std::to_string(proxy_port_);
        curl_easy_setopt(curl_, CURLOPT_PROXY, proxy.c_str());
        curl_easy_setopt(curl_, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);
    }

    // Disable SSL verification for self-hosted endpoints
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl_);

    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);

    char* effective_url = nullptr;
    curl_easy_getinfo(curl_, CURLINFO_EFFECTIVE_URL, &effective_url);

    curl_slist_free_all(header_list);
    curl_easy_reset(curl_);

    if (res != CURLE_OK && res != CURLE_WRITE_ERROR) {
        StreamEvent evt;
        evt.type = StreamEvent::ERROR;
        evt.text = "Request failed [" + url + "]: " + std::string(curl_easy_strerror(res))
                 + " (curl_code=" + std::to_string((int)res)
                 + ", http_status=" + std::to_string(http_code)
                 + (effective_url ? ", effective_url=" + std::string(effective_url) : "") + ")";
        callback(evt);
    }

    return (int)http_code;
}

std::string HttpClient::post(const std::string& url,
                             const std::map<std::string, std::string>& headers,
                             const json& body,
                             int timeout_seconds) {
    if (!curl_) return "";

    std::string body_str = body.dump();
    std::string response;

    struct curl_slist* header_list = nullptr;
    for (const auto& [k, v] : headers) {
        std::string h = k + ": " + v;
        header_list = curl_slist_append(header_list, h.c_str());
    }
    header_list = curl_slist_append(header_list, "Content-Type: application/json");

    curl_easy_setopt(curl_, CURLOPT_URL, url_encode(url).c_str());
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, body_str.size());
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, (long)timeout_seconds);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 10L);

    if (proxy_enabled_) {
        std::string proxy = proxy_host_ + ":" + std::to_string(proxy_port_);
        curl_easy_setopt(curl_, CURLOPT_PROXY, proxy.c_str());
        curl_easy_setopt(curl_, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);
    }

    CURLcode res = curl_easy_perform(curl_);

    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(header_list);
    curl_easy_reset(curl_);

    if (res != CURLE_OK) {
        std::cerr << "[http] POST " << url << " failed: " << curl_easy_strerror(res)
                  << " (curl_code=" << (int)res << ", http_status=" << http_code << ")" << std::endl;
    }

    return response;
}

std::string HttpClient::get(const std::string& url,
                            const std::map<std::string, std::string>& headers) {
    if (!curl_) return "";

    std::string response;

    struct curl_slist* header_list = nullptr;
    for (const auto& [k, v] : headers) {
        std::string h = k + ": " + v;
        header_list = curl_slist_append(header_list, h.c_str());
    }

    curl_easy_setopt(curl_, CURLOPT_URL, url_encode(url).c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 10L);

    if (proxy_enabled_) {
        std::string proxy = proxy_host_ + ":" + std::to_string(proxy_port_);
        curl_easy_setopt(curl_, CURLOPT_PROXY, proxy.c_str());
        curl_easy_setopt(curl_, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);
    }

    CURLcode res = curl_easy_perform(curl_);

    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(header_list);
    curl_easy_reset(curl_);

    if (res != CURLE_OK) {
        std::cerr << "[http] GET " << url << " failed: " << curl_easy_strerror(res)
                  << " (curl_code=" << (int)res << ", http_status=" << http_code << ")" << std::endl;
    }

    return response;
}
