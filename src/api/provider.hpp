#pragma once

#include "api/http_client.hpp"
#include "models/message.hpp"
#include "utils/config.hpp"
#include <memory>
#include <string>
#include <vector>
#include <functional>

struct GenConfig {
    std::string provider;        // "openai", "anthropic", "gemini", "deepseek", "ollama", "custom"
    std::string base_url;
    std::string api_key;
    std::string model;
    std::string system_prompt;
    std::string user_prepend;
    std::string user_postpend;
    int max_context_window = 20;
    bool include_thoughts = true;
    float temperature = 0.7f;
    int max_tokens = 4096;
    bool thinking_enabled = true;
};

class Provider {
public:
    Provider(const GenConfig& config, HttpClient& client);
    virtual ~Provider() = default;

    virtual void generate(const std::vector<Message>& messages,
                          std::function<bool(const StreamEvent&)> callback) = 0;

    virtual std::vector<std::string> fetch_models();

    static std::unique_ptr<Provider> create(const GenConfig& config, HttpClient& client);

protected:
    GenConfig config_;
    HttpClient& client_;

    std::vector<json> prepare_messages_openai(const std::vector<Message>& messages);
    std::vector<json> prepare_messages_anthropic(const std::vector<Message>& messages);
    std::string resolve_variables(const std::string& template_str, int64_t timestamp = 0);
    static std::string build_tool_call_id(const std::string& name, const std::string& args);
};

class OpenAIProvider : public Provider {
public:
    using Provider::Provider;
    void generate(const std::vector<Message>& messages,
                  std::function<bool(const StreamEvent&)> callback) override;
    std::vector<std::string> fetch_models() override;
};

class AnthropicProvider : public Provider {
public:
    using Provider::Provider;
    void generate(const std::vector<Message>& messages,
                  std::function<bool(const StreamEvent&)> callback) override;
    std::vector<std::string> fetch_models() override;
};

class OllamaProvider : public Provider {
public:
    using Provider::Provider;
    void generate(const std::vector<Message>& messages,
                  std::function<bool(const StreamEvent&)> callback) override;
    std::vector<std::string> fetch_models() override;
};

class CustomOpenAIProvider : public Provider {
public:
    using Provider::Provider;
    void generate(const std::vector<Message>& messages,
                  std::function<bool(const StreamEvent&)> callback) override;
};

class GeminiProvider : public Provider {
public:
    using Provider::Provider;
    void generate(const std::vector<Message>& messages,
                  std::function<bool(const StreamEvent&)> callback) override;
};
