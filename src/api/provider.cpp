#include "api/provider.hpp"
#include "utils/memory_manager.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <fstream>
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
        return std::make_unique<OpenAIProvider>(config, client);
    }
}

// --- Tool definitions ---

std::vector<ToolDefinition> Provider::get_builtin_tools() {
    std::vector<ToolDefinition> tools;

    if (config_.memory_tools_enabled) {
        auto& mem = MemoryManager::instance();
        std::string active_mem = mem.get_active_memory();

        tools.push_back({"function", "update_active_memory",
            "Update the active memory. Mode: replace, append, prepend, or patch (remove occurrence of content).",
            R"({"type":"object","properties":{"content":{"type":"string","description":"Memory content"},"mode":{"type":"string","enum":["replace","append","prepend","patch"],"description":"How to update"}},"required":["content","mode"]})"});

        tools.push_back({"function", "list_memory_files",
            "List all saved memory files.",
            R"({"type":"object","properties":{},"required":[]})"});

        tools.push_back({"function", "read_memory_file",
            "Read a saved memory file by name.",
            R"({"type":"object","properties":{"name":{"type":"string","description":"File name"}},"required":["name"]})"});

        tools.push_back({"function", "create_memory_file",
            "Create a new memory file.",
            R"({"type":"object","properties":{"name":{"type":"string","description":"File name"},"content":{"type":"string","description":"File content"},"description":{"type":"string","description":"Optional description"}},"required":["name","content"]})"});

        tools.push_back({"function", "delete_memory_file",
            "Delete a memory file by name.",
            R"({"type":"object","properties":{"name":{"type":"string","description":"File name"}},"required":["name"]})"});
    }

    if (config_.web_search_enabled) {
        tools.push_back({"function", "web_search",
            "Search the web using DuckDuckGo and return results.",
            R"({"type":"object","properties":{"query":{"type":"string","description":"Search query"}},"required":["query"]})"});
    }

    return tools;
}

std::string Provider::execute_tool(const std::string& name, const std::string& args) {
    try {
        json jargs = json::parse(args.empty() ? "{}" : args);
        auto& mem = MemoryManager::instance();

        if (name == "update_active_memory") {
            std::string content = jargs.value("content", "");
            std::string mode = jargs.value("mode", "replace");
            mem.update_active_memory(content, mode);
            return "Active memory updated (mode: " + mode + "). Current active memory:\n" + mem.get_active_memory();
        }
        if (name == "list_memory_files") {
            auto files = mem.list_files();
            std::ostringstream oss;
            oss << "Memory files (" << files.size() << "):\n";
            for (auto& f : files) {
                oss << "  - " << f.name << " (" << f.content.size() << " bytes)\n";
            }
            return oss.str();
        }
        if (name == "read_memory_file") {
            std::string fname = jargs.value("name", "");
            auto mf = mem.read_file(fname);
            return mf.content.empty() ? "File not found: " + fname : mf.content;
        }
        if (name == "create_memory_file") {
            std::string fname = jargs.value("name", "");
            std::string content = jargs.value("content", "");
            std::string desc = jargs.value("description", "");
            mem.write_file(fname, content, desc);
            return "Memory file created: " + fname;
        }
        if (name == "delete_memory_file") {
            std::string fname = jargs.value("name", "");
            mem.delete_file(fname);
            return "Memory file deleted: " + fname;
        }
        if (name == "web_search") {
            std::string query = jargs.value("query", "");
            // DuckDuckGo Lite scraper
            std::string url = "https://lite.duckduckgo.com/lite/?q=";
            for (char c : query) {
                if (c == ' ') url += '+';
                else url += c;
            }
            HttpClient hc;
            std::string html = hc.get(url);
            // Extract result snippets
            std::ostringstream results;
            results << "Web search results for: " << query << "\n\n";
            size_t pos = 0;
            int count = 0;
            while (count < 5) {
                size_t link_start = html.find("<a rel=\"nofollow\" href=\"", pos);
                if (link_start == std::string::npos) break;
                size_t href_end = html.find("\"", link_start + 28);
                std::string link = html.substr(link_start + 27, href_end - link_start - 27);
                size_t snippet_start = html.find("<td class=\"result-snippet\">", href_end);
                if (snippet_start == std::string::npos) break;
                size_t snippet_end = html.find("</td>", snippet_start);
                std::string snippet = html.substr(snippet_start + 28, snippet_end - snippet_start - 28);
                // Strip HTML tags
                std::string clean;
                bool in_tag = false;
                for (char c : snippet) {
                    if (c == '<') in_tag = true;
                    else if (c == '>') in_tag = false;
                    else if (!in_tag) clean += c;
                }
                results << (count + 1) << ". " << link << "\n   " << clean << "\n\n";
                pos = snippet_end;
                count++;
            }
            return results.str();
        }
    } catch (const std::exception& e) {
        return std::string("Tool error: ") + e.what();
    }
    return "Unknown tool: " + name;
}

std::string OpenAIProvider::execute_tool(const std::string& name, const std::string& args) {
    return Provider::execute_tool(name, args);
}

// --- Variable resolution ---

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
    replace("{active_memory}", MemoryManager::instance().get_active_memory());

    /* Tomogichi bridge — read the JSON export file */
    {
        const char* home = getenv("HOME");
        std::string tomogichi_path = home ? std::string(home) + "/tomogichi-agora.json" : "tomogichi-agora.json";
        std::ifstream f(tomogichi_path);
        if (f.is_open()) {
            std::ostringstream buf;
            buf << f.rdbuf();
            replace("{tomogichi}", buf.str());
            f.close();
        } else {
            replace("{tomogichi}", "");
        }
    }

    /* Emergency check — read the emergency file */
    {
        const char* home = getenv("HOME");
        std::string emerg_path = home ? std::string(home) + "/agora-emergency.md" : "agora-emergency.md";
        std::ifstream f(emerg_path);
        if (f.is_open()) {
            std::ostringstream buf;
            buf << f.rdbuf();
            replace("{emergency}", buf.str());
            f.close();
        } else {
            replace("{emergency}", "");
        }
    }

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

// --- Message preparation ---

std::vector<json> Provider::prepare_messages_openai(const std::vector<Message>& messages,
                                                     const std::vector<ToolDefinition>& tools) {
    std::vector<json> result;

    // System message with active memory appended
    if (!config_.system_prompt.empty()) {
        std::string resolved = resolve_variables(config_.system_prompt);
        // Append active memory if non-empty
        std::string active_mem = MemoryManager::instance().get_active_memory();
        if (!active_mem.empty()) {
            resolved += "\n\n<active_memory>\n" + active_mem + "\n</active_memory>";
        }
        result.push_back({{"role", "system"}, {"content", resolved}});
    }

    // Limit context window
    size_t max_msgs = config_.max_context_window;
    std::vector<Message> window;

    int user_count = 0;
    for (auto it = messages.rbegin(); it != messages.rend() && user_count < max_msgs / 2; ++it) {
        if (it->participant == Participant::USER) user_count++;
        window.insert(window.begin(), *it);
    }

    for (const auto& msg : window) {
        json j;

        if (msg.participant == Participant::USER) j["role"] = "user";
        else if (msg.participant == Participant::MODEL) j["role"] = "assistant";
        else continue;

        std::string content = msg.text;
        if (msg.participant == Participant::USER && !config_.user_prepend.empty()) {
            content = resolve_variables(config_.user_prepend, msg.timestamp) + content;
        }
        if (msg.participant == Participant::USER && !config_.user_postpend.empty()) {
            content = content + resolve_variables(config_.user_postpend, msg.timestamp);
        }

        j["content"] = content;

        if (!msg.thoughts.empty() && config_.include_thoughts) {
            j["reasoning_content"] = msg.thoughts;
        }

        result.push_back(j);
    }

    return result;
}

std::vector<json> Provider::prepare_messages_anthropic(const std::vector<Message>& messages) {
    return {};
}

// --- OpenAI Provider ---

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

    auto tools = get_builtin_tools();
    auto api_messages = prepare_messages_openai(messages, tools);

    json body;
    body["model"] = config_.model;
    body["messages"] = api_messages;
    body["stream"] = true;
    body["temperature"] = config_.temperature;
    body["max_tokens"] = config_.max_tokens;
    body["top_p"] = config_.top_p;
    body["frequency_penalty"] = config_.frequency_penalty;
    body["presence_penalty"] = config_.presence_penalty;

    // Thinking/reasoning support
    if (config_.provider == "deepseek" || config_.provider == "qwen") {
        if (config_.thinking_enabled) body["reasoning_effort"] = "medium";
    }

    // Tool definitions
    if (!tools.empty()) {
        json jtools = json::array();
        for (auto& td : tools) {
            json jt;
            jt["type"] = td.type;
            jt["function"]["name"] = td.function_name;
            jt["function"]["description"] = td.function_description;
            jt["function"]["parameters"] = json::parse(td.function_parameters);
            jtools.push_back(jt);
        }
        body["tools"] = jtools;
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
                if (m.contains("id")) models.push_back(m["id"].get<std::string>());
            }
        }
    } catch (...) {}

    return models;
}

// --- Anthropic Provider ---

void AnthropicProvider::generate(const std::vector<Message>& messages,
                                 std::function<bool(const StreamEvent&)> callback) {
    std::string base = trim(config_.base_url);
    if (base.empty()) base = "https://api.anthropic.com/v1";
    if (base.back() == '/') base.pop_back();
    std::string url = base + "/messages";
    std::cerr << "[http] POST " << url << std::endl;

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
    body["temperature"] = config_.temperature;
    body["top_p"] = config_.top_p;

    if (!config_.system_prompt.empty()) {
        std::string resolved = resolve_variables(config_.system_prompt);
        std::string active_mem = MemoryManager::instance().get_active_memory();
        if (!active_mem.empty()) resolved += "\n\n<active_memory>\n" + active_mem + "\n</active_memory>";
        body["system"] = resolved;
    }

    if (config_.thinking_enabled) {
        body["thinking"] = {{"type", "enabled"}, {"budget_tokens", 4096}};
    }

    // Tools
    auto tools = get_builtin_tools();
    if (!tools.empty()) {
        json jtools = json::array();
        for (auto& td : tools) {
            json jt;
            jt["name"] = td.function_name;
            jt["description"] = td.function_description;
            jt["input_schema"] = json::parse(td.function_parameters);
            jtools.push_back(jt);
        }
        body["tools"] = jtools;
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
    return {"claude-4-sonnet-20250514", "claude-4-opus-20250514",
            "claude-3.5-sonnet-20241022", "claude-3.5-haiku-20241022",
            "claude-3-opus-20240229"};
}

// --- Ollama Provider ---

void OllamaProvider::generate(const std::vector<Message>& messages,
                              std::function<bool(const StreamEvent&)> callback) {
    std::string base = trim(config_.base_url);
    if (base.empty()) base = "http://localhost:11434";
    if (base.back() == '/') base.pop_back();
    std::string url = base + "/api/chat";
    std::cerr << "[http] POST " << url << std::endl;

    std::vector<json> api_messages;
    if (!config_.system_prompt.empty()) {
        std::string resolved = resolve_variables(config_.system_prompt);
        std::string active_mem = MemoryManager::instance().get_active_memory();
        if (!active_mem.empty()) resolved += "\n\n<active_memory>\n" + active_mem + "\n</active_memory>";
        api_messages.push_back({{"role", "system"}, {"content", resolved}});
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
    body["options"] = {
        {"temperature", config_.temperature},
        {"top_p", config_.top_p},
        {"num_predict", config_.max_tokens}
    };

    auto tools = get_builtin_tools();
    if (!tools.empty()) {
        json jtools = json::array();
        for (auto& td : tools) {
            json jt;
            jt["type"] = "function";
            jt["function"]["name"] = td.function_name;
            jt["function"]["description"] = td.function_description;
            jt["function"]["parameters"] = json::parse(td.function_parameters);
            jtools.push_back(jt);
        }
        body["tools"] = jtools;
    }

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
                if (m.contains("name")) models.push_back(m["name"].get<std::string>());
            }
        }
    } catch (...) {}

    return models;
}

// --- Gemini Provider ---

void GeminiProvider::generate(const std::vector<Message>& messages,
                              std::function<bool(const StreamEvent&)> callback) {
    std::string base = trim(config_.base_url);
    if (base.empty()) base = "https://generativelanguage.googleapis.com/v1beta";
    if (base.back() == '/') base.pop_back();
    std::string url = base + "/models/" + config_.model + ":streamGenerateContent?key=" + config_.api_key;
    std::cerr << "[http] POST " << url << std::endl;

    std::vector<json> contents;

    if (!config_.system_prompt.empty()) {
        std::string resolved = resolve_variables(config_.system_prompt);
        std::string active_mem = MemoryManager::instance().get_active_memory();
        if (!active_mem.empty()) resolved += "\n\n<active_memory>\n" + active_mem + "\n</active_memory>";
        json sys_part;
        sys_part["text"] = resolved;
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
        {"topP", config_.top_p},
        {"maxOutputTokens", config_.max_tokens}
    };

    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";

    client_.stream_post(url, headers, body, [&](const StreamEvent& evt) {
        return callback(evt);
    });
}

std::vector<std::string> Provider::fetch_models() { return {}; }
