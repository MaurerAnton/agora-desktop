#pragma once

#include "api/http_client.hpp"
#include "models/message.hpp"
#include "utils/config.hpp"
#include <memory>
#include <string>
#include <vector>
#include <functional>

struct ToolDefinition {
    std::string type = "function";
    std::string function_name;
    std::string function_description;
    std::string function_parameters; // JSON schema string
};

struct GenConfig {
    std::string provider;
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
    float top_p = 1.0f;
    float frequency_penalty = 0.0f;
    float presence_penalty = 0.0f;
    bool thinking_enabled = true;
    // Tool toggles
    bool web_search_enabled = false;
    bool memory_tools_enabled = true;
    // Conversation-level overrides
    bool has_conv_overrides = false;
};

class Provider {
public:
    Provider(const GenConfig& config, HttpClient& client);
    virtual ~Provider() = default;

    virtual void generate(const std::vector<Message>& messages,
                          std::function<bool(const StreamEvent&)> callback) = 0;

    virtual std::vector<std::string> fetch_models();

    static std::unique_ptr<Provider> create(const GenConfig& config, HttpClient& client);

    // Tool execution (called by main loop)
    virtual std::string execute_tool(const std::string& name, const std::string& args);

protected:
    GenConfig config_;
    HttpClient& client_;

    std::vector<json> prepare_messages_openai(const std::vector<Message>& messages,
                                               const std::vector<ToolDefinition>& tools = {});
    std::vector<json> prepare_messages_anthropic(const std::vector<Message>& messages);
    std::string resolve_variables(const std::string& template_str, int64_t timestamp = 0);
    static std::string build_tool_call_id(const std::string& name, const std::string& args);
    std::vector<ToolDefinition> get_builtin_tools();
};

class OpenAIProvider : public Provider {
public:
    using Provider::Provider;
    void generate(const std::vector<Message>& messages,
                  std::function<bool(const StreamEvent&)> callback) override;
    std::vector<std::string> fetch_models() override;
    std::string execute_tool(const std::string& name, const std::string& args) override;
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
