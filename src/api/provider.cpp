#include "api/provider.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <openssl/sha.h>

Provider::Provider(const GenConfig& config, HttpClient& client)
    : config_(config), client_(client) {}

std::unique_ptr<Provider> Provider::create(const GenConfig& config, HttpClient& client) {
    if (config.provider == "anthropic") {
        return std::make_unique<AnthropicProvider>(config, client);
    } else if (config.provider == "ollama") {
        return std::make_unique<OllamaProvider>(config, client);
    } else if (config.provider == "gemini") {
        return std::make_unique<GeminiProvider>(config, client);
    } else {
        // All OpenAI-compatible: openai, deepseek, qwen, openrouter, custom
        return std::make_unique<OpenAIProvider>(config, client);
    }
}

std::string Provider::resolve_variables(const std::string& template_str, int64_t timestamp) {
    std::string result = template_str;

    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&t, &local);

    std::ostringstream date_ss, time_ss;
    date_ss << std::put_time(&local, "%Y-%m-%d");
    time_ss << std::put_time(&local, "%H:%M:%S");

    std::string date = date_ss.str();
    std::string time = time_ss.str();

    // Simple string replace for variables
    auto replace = [&result](const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = result.find(from, pos)) != std::string::npos) {
            result.replace(pos, from.length(), to);
            pos += to.length();
        }
    };

    replace("{date}", date);
    replace("{time}", time);
    replace("{current_date}", date);

    if (timestamp > 0) {
        std::time_t ts = timestamp / 1000;
        std::tm msg_tm{};
        localtime_r(&ts, &msg_tm);
        std::ostringstream sdate, stime;
        sdate << std::put_time(&msg_tm, "%Y-%m-%d");
        stime << std::put_time(&msg_tm, "%H:%M:%S");
        replace("{sent_date}", sdate.str());
        replace("{sent_time}", stime.str());
    }

    return result;
}

std::string Provider::build_tool_call_id(const std::string& name, const std::string& args) {
    std::string input = name + ":" + args;
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash);
    std::ostringstream oss;
    oss << "call_";
    for (int i = 0; i < 8; i++) {
        oss << std::hex << std::setfill('0') << std::setw(2) << (int)hash[i];
    }
    return oss.str();
}

std::vector<json> Provider::prepare_messages_openai(const std::vector<Message>& messages) {
    std::vector<json> result;

    // System message
    if (!config_.system_prompt.empty()) {
        std::string resolved = resolve_variables(config_.system_prompt);
        result.push_back({{"role", "system"}, {"content", resolved}});
    }

    // Limit context window
    size_t max_msgs = config_.max_context_window;
    std::vector<Message> window;

    // Take last N messages, prefer to keep pairs
    int user_count = 0;
    for (auto it = messages.rbegin(); it != messages.rend() && user_count < max_msgs / 2; ++it) {
        if (it->participant == Participant::USER) user_count++;
        window.insert(window.begin(), *it);
    }

    // Build messages
    for (const auto& msg : window) {
        json j;

        if (msg.participant == Participant::USER) {
            j["role"] = "user";
        } else if (msg.participant == Participant::MODEL) {
            j["role"] = "assistant";
        } else {
            continue;
        }

        // Apply user template
        std::string content = msg.text;
        if (msg.participant == Participant::USER && !config_.user_prepend.empty()) {
            content = resolve_variables(config_.user_prepend, msg.timestamp) + content;
        }
        if (msg.participant == Participant::USER && !config_.user_postpend.empty()) {
            content = content + resolve_variables(config_.user_postpend, msg.timestamp);
        }

        j["content"] = content;

        // Include thinking if present
        if (!msg.thoughts.empty() && config_.include_thoughts) {
            j["reasoning_content"] = msg.thoughts;
        }

        result.push_back(j);
    }

    return result;
}

std::vector<json> Provider::prepare_messages_anthropic(const std::vector<Message>& messages) {
    std::vector<json> result;
    // Anthropic doesn't use system role in messages array
    // It uses a top-level system field
    return result;
}

// ---- OpenAI Provider ----

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

void OpenAIProvider::generate(const std::vector<Message>& messages,
                              std::function<bool(const StreamEvent&)> callback) {
    std::string base = trim(config_.base_url);
    if (base.empty()) base = "https://api.openai.com/v1";
    if (base.back() == '/') base.pop_back();
    std::string url = base + "/chat/completions";
    std::cerr << "[http] POST " << url << std::endl;

    auto api_messages = prepare_messages_openai(messages);

    json body;
    body["model"] = config_.model;
    body["messages"] = api_messages;
    body["stream"] = true;
    body["temperature"] = config_.temperature;
    body["max_tokens"] = config_.max_tokens;

    // Thinking/reasoning support for DeepSeek and Qwen
    if (config_.provider == "deepseek" || config_.provider == "qwen") {
        if (config_.thinking_enabled) {
            body["reasoning_effort"] = "medium";
        }
    }

    std::map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + config_.api_key;
    headers["Content-Type"] = "application/json";

    client_.stream_post(url, headers, body, [&](const StreamEvent& evt) {
        return callback(evt);
    });
}

std::vector<std::string> OpenAIProvider::fetch_models() {
    std::string url = config_.base_url.empty() ? "https://api.openai.com/v1" : config_.base_url;
    url += "/models";

    std::map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + config_.api_key;

    std::string resp = client_.get(url, headers);
    std::vector<std::string> models;

    try {
        json j = json::parse(resp);
        if (j.contains("data")) {
            for (auto& m : j["data"]) {
                if (m.contains("id")) {
                    models.push_back(m["id"].get<std::string>());
                }
            }
        }
    } catch (...) {}

    return models;
}

// ---- Anthropic Provider ----

void AnthropicProvider::generate(const std::vector<Message>& messages,
                                 std::function<bool(const StreamEvent&)> callback) {
    std::string base = trim(config_.base_url);
    if (base.empty()) base = "https://api.anthropic.com/v1";
    if (base.back() == '/') base.pop_back();
    std::string url = base + "/messages";
    std::cerr << "[http] POST " << url << std::endl;

    // Build Anthropic-format messages
    std::vector<json> api_messages;
    for (const auto& msg : messages) {
        if (msg.participant == Participant::ERROR) continue;

        json j;
        j["role"] = msg.participant == Participant::USER ? "user" : "assistant";

        std::string content = msg.text;
        if (msg.participant == Participant::USER && !config_.user_prepend.empty()) {
            content = resolve_variables(config_.user_prepend, msg.timestamp) + content;
        }
        j["content"] = content;
        api_messages.push_back(j);
    }

    json body;
    body["model"] = config_.model;
    body["messages"] = api_messages;
    body["max_tokens"] = config_.max_tokens;
    body["stream"] = true;

    // System prompt as top-level field
    if (!config_.system_prompt.empty()) {
        body["system"] = resolve_variables(config_.system_prompt);
    }

    if (config_.thinking_enabled) {
        body["thinking"] = {{"type", "enabled"}, {"budget_tokens", 4096}};
    }

    std::map<std::string, std::string> headers;
    headers["x-api-key"] = config_.api_key;
    headers["anthropic-version"] = "2023-06-01";
    headers["Content-Type"] = "application/json";

    client_.stream_post(url, headers, body, [&](const StreamEvent& evt) {
        return callback(evt);
    });
}

std::vector<std::string> AnthropicProvider::fetch_models() {
    // Anthropic doesn't have a public models list endpoint
    return {"claude-4-sonnet-20250514", "claude-4-opus-20250514",
            "claude-3.5-sonnet-20241022", "claude-3.5-haiku-20241022",
            "claude-3-opus-20240229"};
}

// ---- Ollama Provider ----

void OllamaProvider::generate(const std::vector<Message>& messages,
                              std::function<bool(const StreamEvent&)> callback) {
    std::string base = trim(config_.base_url);
    if (base.empty()) base = "http://localhost:11434";
    if (base.back() == '/') base.pop_back();
    std::string url = base + "/api/chat";
    std::cerr << "[http] POST " << url << std::endl;

    std::vector<json> api_messages;
    if (!config_.system_prompt.empty()) {
        api_messages.push_back({{"role", "system"}, {"content", resolve_variables(config_.system_prompt)}});
    }

    for (const auto& msg : messages) {
        if (msg.participant == Participant::ERROR) continue;
        json j;
        j["role"] = msg.participant == Participant::USER ? "user" : "assistant";
        j["content"] = msg.text;
        api_messages.push_back(j);
    }

    json body;
    body["model"] = config_.model;
    body["messages"] = api_messages;
    body["stream"] = true;
    body["options"] = {{"temperature", config_.temperature}, {"num_predict", config_.max_tokens}};

    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";

    client_.stream_post(url, headers, body, [&](const StreamEvent& evt) {
        return callback(evt);
    });
}

std::vector<std::string> OllamaProvider::fetch_models() {
    std::string url = config_.base_url.empty() ? "http://localhost:11434" : config_.base_url;
    url += "/api/tags";

    std::string resp = client_.get(url);
    std::vector<std::string> models;

    try {
        json j = json::parse(resp);
        if (j.contains("models")) {
            for (auto& m : j["models"]) {
                if (m.contains("name")) {
                    models.push_back(m["name"].get<std::string>());
                }
            }
        }
    } catch (...) {}

    return models;
}

// ---- Gemini Provider ----

void GeminiProvider::generate(const std::vector<Message>& messages,
                              std::function<bool(const StreamEvent&)> callback) {
    std::string base = trim(config_.base_url);
    if (base.empty()) base = "https://generativelanguage.googleapis.com/v1beta";
    if (base.back() == '/') base.pop_back();
    std::string url = base + "/models/" + config_.model + ":streamGenerateContent?key=" + config_.api_key;
    std::cerr << "[http] POST " << url << std::endl;

    std::vector<json> contents;

    if (!config_.system_prompt.empty()) {
        json sys_part;
        sys_part["text"] = resolve_variables(config_.system_prompt);
        contents.push_back({{"role", "user"}, {"parts", json::array({sys_part})}});
        contents.push_back({{"role", "model"}, {"parts", json::array({})}});
    }

    for (const auto& msg : messages) {
        if (msg.participant == Participant::ERROR) continue;
        json j;
        j["role"] = msg.participant == Participant::USER ? "user" : "model";
        json part;
        part["text"] = msg.text;
        j["parts"] = json::array({part});
        contents.push_back(j);
    }

    json body;
    body["contents"] = contents;
    body["generationConfig"] = {
        {"temperature", config_.temperature},
        {"maxOutputTokens", config_.max_tokens}
    };

    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";

    client_.stream_post(url, headers, body, [&](const StreamEvent& evt) {
        return callback(evt);
    });
}

std::vector<std::string> Provider::fetch_models() { return {}; }
