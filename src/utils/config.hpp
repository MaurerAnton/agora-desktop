#pragma once

#include <string>
#include <vector>
#include <map>
#include "json.hpp"

struct ApiKeyEntry {
    std::string id;
    std::string name;
    std::string key;
    std::string provider;
};

struct SystemPromptEntry {
    std::string id;
    std::string title;
    std::string content;
    std::string user_prepend;
    std::string user_postpend;
};

struct ProviderConfig {
    std::string name;
    std::string base_url;
    std::string api_key_id;
    bool enabled = true;
};

struct TorConfig {
    bool enabled = false;
    std::string socks_host = "127.0.0.1";
    int socks_port = 9050;
};

struct SttConfig {
    bool enabled = true;
    std::string endpoint_url;  // remote STT server URL
    int sample_rate = 16000;
};

class Config {
public:
    static Config& instance();
    void load(const std::string& path);
    void save(const std::string& path);

    std::string selected_model;
    std::string active_system_prompt_id;
    std::vector<ApiKeyEntry> api_keys;
    std::vector<ProviderConfig> providers;
    std::vector<SystemPromptEntry> system_prompts;
    TorConfig tor;
    SttConfig stt;

    std::string memory_dir;
    std::string db_path;
    int max_context_window = 20;
    bool thinking_enabled = true;

    std::string get_api_key(const std::string& provider) const;
    std::string get_base_url(const std::string& provider) const;

    // Auto-import providers from a simple JSON file (drop-in provisioning)
    bool merge_provider_file(const std::string& path);

private:
    Config() = default;
    void init_defaults();
    std::string config_path;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ApiKeyEntry, id, name, key, provider)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SystemPromptEntry, id, title, content, user_prepend, user_postpend)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ProviderConfig, name, base_url, api_key_id, enabled)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TorConfig, enabled, socks_host, socks_port)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SttConfig, enabled, endpoint_url, sample_rate)
