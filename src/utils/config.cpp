#include "utils/config.hpp"
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>

using json = nlohmann::json;

Config& Config::instance() {
    static Config config;
    return config;
}

void Config::load(const std::string& path) {
    config_path = path;
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "No config file at " << path << ", using defaults" << std::endl;
        init_defaults();
        return;
    }

    try {
        json j = json::parse(file);

        if (j.contains("selected_model")) selected_model = j["selected_model"];
        if (j.contains("active_system_prompt_id")) active_system_prompt_id = j["active_system_prompt_id"];
        if (j.contains("max_context_window")) max_context_window = j["max_context_window"];
        if (j.contains("thinking_enabled")) thinking_enabled = j["thinking_enabled"];
        if (j.contains("temperature")) temperature = j["temperature"];
        if (j.contains("max_tokens")) max_tokens = j["max_tokens"];
        if (j.contains("top_p")) top_p = j["top_p"];
        if (j.contains("frequency_penalty")) frequency_penalty = j["frequency_penalty"];
        if (j.contains("presence_penalty")) presence_penalty = j["presence_penalty"];
        if (j.contains("title_gen_enabled")) title_gen_enabled = j["title_gen_enabled"];
        if (j.contains("auto_backup_enabled")) auto_backup_enabled = j["auto_backup_enabled"];
        if (j.contains("auto_backup_hours")) auto_backup_hours = j["auto_backup_hours"];
        if (j.contains("db_path")) db_path = j["db_path"];
        if (j.contains("memory_dir")) memory_dir = j["memory_dir"];

        if (j.contains("api_keys")) api_keys = j["api_keys"];
        if (j.contains("providers")) providers = j["providers"];
        if (j.contains("system_prompts")) system_prompts = j["system_prompts"];
        if (j.contains("tor")) tor = j["tor"];
        if (j.contains("stt")) stt = j["stt"];
        if (j.contains("theme")) theme = j["theme"];
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse config: " << e.what() << std::endl;
    }

    init_defaults();
}

void Config::save(const std::string& path) {
    std::string save_path = path.empty() ? config_path : path;

    json j;
    j["selected_model"] = selected_model;
    j["active_system_prompt_id"] = active_system_prompt_id;
    j["max_context_window"] = max_context_window;
    j["thinking_enabled"] = thinking_enabled;
    j["temperature"] = temperature;
    j["max_tokens"] = max_tokens;
    j["top_p"] = top_p;
    j["frequency_penalty"] = frequency_penalty;
    j["presence_penalty"] = presence_penalty;
    j["title_gen_enabled"] = title_gen_enabled;
    j["auto_backup_enabled"] = auto_backup_enabled;
    j["auto_backup_hours"] = auto_backup_hours;
    j["db_path"] = db_path;
    j["memory_dir"] = memory_dir;
    j["api_keys"] = api_keys;
    j["providers"] = providers;
    j["system_prompts"] = system_prompts;
    j["tor"] = tor;
    j["stt"] = stt;
    j["theme"] = theme;

    std::ofstream file(save_path);
    if (file.is_open()) {
        file << j.dump(2) << std::endl;
    }
}

void Config::init_defaults() {
    if (db_path.empty()) {
        const char* home = getenv("HOME");
        std::string base = home ? std::string(home) + "/.local/share/agora" : "/tmp/agora";
        mkdir(base.c_str(), 0755);
        db_path = base + "/agora.db";
        memory_dir = base + "/memories";
        mkdir(memory_dir.c_str(), 0755);
    }
}

std::string Config::get_api_key(const std::string& provider) const {
    for (const auto& k : api_keys) {
        if (k.provider == provider) return k.key;
    }
    return "";
}

std::string Config::get_base_url(const std::string& provider) const {
    for (const auto& p : providers) {
        if (p.name == provider) return p.base_url;
    }
    return "";
}

bool Config::merge_provider_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string sentinel_path = path + ".imported";

    // Check if already imported and file hasn't changed
    struct stat st_prov{}, st_sent{};
    bool sentinel_exists = (stat(sentinel_path.c_str(), &st_sent) == 0);
    if (sentinel_exists && stat(path.c_str(), &st_prov) == 0) {
        if (st_prov.st_mtime <= st_sent.st_mtime) {
            std::cout << "Providers already imported (sentinel is up to date)" << std::endl;
            return false;
        }
    }

    try {
        json j = json::parse(file);

        // Merge selected_model
        if (j.contains("selected_model") && j["selected_model"].is_string()) {
            selected_model = j["selected_model"].get<std::string>();
            std::cout << "  model: " << selected_model << std::endl;
        }

        // Merge API keys (additive: new keys replace old with same provider)
        if (j.contains("api_keys") && j["api_keys"].is_array()) {
            for (auto& k : j["api_keys"]) {
                std::string prov = k.value("provider", "");
                std::string key = k.value("key", "");
                if (prov.empty() || key.empty()) continue;

                // Replace existing or add new
                bool found = false;
                for (auto& ak : api_keys) {
                    if (ak.provider == prov) {
                        ak.key = key;
                        if (k.contains("id")) ak.id = k["id"];
                        if (k.contains("name")) ak.name = k["name"];
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ApiKeyEntry e;
                    e.provider = prov;
                    e.key = key;
                    e.id = k.value("id", prov);
                    e.name = k.value("name", prov);
                    api_keys.push_back(e);
                }
                std::cout << "  api key: " << prov << std::endl;
            }
        }

        // Merge providers
        if (j.contains("providers") && j["providers"].is_array()) {
            for (auto& p : j["providers"]) {
                std::string name = p.value("name", "");
                if (name.empty()) continue;

                bool found = false;
                for (auto& pr : providers) {
                    if (pr.name == name) {
                        if (p.contains("base_url")) pr.base_url = p["base_url"];
                        if (p.contains("enabled")) pr.enabled = p["enabled"];
                        if (p.contains("api_key_id")) pr.api_key_id = p["api_key_id"];
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ProviderConfig pc;
                    pc.name = name;
                    pc.base_url = p.value("base_url", "");
                    pc.enabled = p.value("enabled", true);
                    pc.api_key_id = p.value("api_key_id", "");
                    providers.push_back(pc);
                }
                std::cout << "  provider: " << name << std::endl;
            }
        }

        // Merge Tor
        if (j.contains("tor")) {
            auto& t = j["tor"];
            if (t.contains("enabled")) tor.enabled = t["enabled"];
            if (t.contains("socks_host")) tor.socks_host = t["socks_host"];
            if (t.contains("socks_port")) tor.socks_port = t["socks_port"];
            std::cout << "  tor: " << (tor.enabled ? "enabled" : "disabled") << std::endl;
        }

        // Merge STT
        if (j.contains("stt")) {
            auto& s = j["stt"];
            if (s.contains("enabled")) stt.enabled = s["enabled"];
            if (s.contains("endpoint_url")) stt.endpoint_url = s["endpoint_url"];
            if (s.contains("sample_rate")) stt.sample_rate = s["sample_rate"];
            std::cout << "  stt: " << stt.endpoint_url << std::endl;
        }

        // Save merged config
        save(config_path);

        // Touch sentinel
        std::ofstream sent(sentinel_path);
        sent << "imported" << std::endl;
        sent.close();

        std::cout << "Imported providers from " << path << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "Failed to parse provider file: " << e.what() << std::endl;
        return false;
    }
}
