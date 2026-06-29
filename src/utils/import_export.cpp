#include "utils/import_export.hpp"
#include "utils/config.hpp"
#include "db/database.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <chrono>
#include <ctime>
#include <algorithm>
#include "json.hpp"

using json = nlohmann::json;

static std::string tmpdir() {
    const char* t = getenv("TMPDIR");
    return t ? std::string(t) : "/tmp";
}

static bool run_cmd(const std::string& cmd, std::string& output) {
    char buf[4096];
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return false;
    std::ostringstream oss;
    while (fgets(buf, sizeof(buf), f)) oss << buf;
    int rc = pclose(f);
    output = oss.str();
    return rc == 0;
}

static std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return buf;
}

ExportResult export_data(const std::string& output_path,
                         const std::string& db_path,
                         const std::string& config_path,
                         const std::string& memory_dir,
                         bool include_api_keys) {
    ExportResult result;
    std::string work_dir = tmpdir() + "/agora_export_" + std::to_string(getpid());
    mkdir(work_dir.c_str(), 0755);

    // Open database
    Database db(db_path);

    // --- manifest.json ---
    json manifest;
    manifest["version"] = 1;
    manifest["app_version"] = "agora-desktop 0.1.0";
    manifest["exported_at"] = now_iso();
    manifest["categories"] = {"conversations", "system_prompts", "settings", "memories"};
    if (include_api_keys) manifest["categories"].push_back("api_keys");
    manifest["has_api_keys"] = include_api_keys;

    {
        std::ofstream f(work_dir + "/manifest.json");
        f << manifest.dump(2);
    }

    // --- conversations.json ---
    json conversations_json;
    {
        json convs = json::array();
        auto all_convs = db.get_all_conversations();
        for (auto& c : all_convs) {
            json jc;
            jc["id"] = c.id;
            jc["title"] = c.title;
            jc["last_updated"] = c.last_updated;
            jc["system_prompt_id"] = c.system_prompt_id;
            jc["model_id"] = c.model_id;
            convs.push_back(jc);
        }
        conversations_json["conversations"] = convs;
        result.conversations = (int)all_convs.size();
    }
    {
        json msgs = json::array();
        for (auto& c : db.get_all_conversations()) {
            auto messages = db.get_messages_for_conversation(c.id);
            for (auto& m : messages) {
                json jm;
                jm["id"] = m.id;
                jm["conversation_id"] = m.conversation_id;
                jm["parent_id"] = m.parent_id;
                jm["text"] = m.text;
                jm["thoughts"] = m.thoughts;
                jm["participant"] = (m.participant == Participant::USER ? "USER" :
                                     m.participant == Participant::MODEL ? "MODEL" : "ERROR");
                jm["status"] = "SUCCESS";
                jm["timestamp"] = m.timestamp;
                jm["model_name"] = m.model_name;
                msgs.push_back(jm);
            }
        }
        conversations_json["messages"] = msgs;
        result.messages = (int)msgs.size();
    }
    {
        std::ofstream f(work_dir + "/conversations.json");
        f << conversations_json.dump(2);
    }

    // --- system_prompts.json ---
    {
        auto& cfg = Config::instance();
        json sp = cfg.system_prompts;
        std::ofstream f(work_dir + "/system_prompts.json");
        f << json{{"system_prompts", sp}}.dump(2);
        result.prompts = (int)sp.size();
    }

    // --- settings.json ---
    {
        std::ifstream src(config_path);
        if (src.is_open()) {
            std::ofstream dst(work_dir + "/settings.json");
            dst << src.rdbuf();
        }
    }

    // --- api_keys.json (optional) ---
    if (include_api_keys) {
        auto& cfg = Config::instance();
        json ak = cfg.api_keys;
        std::ofstream f(work_dir + "/api_keys.json");
        f << json{{"api_keys", ak}}.dump(2);
    }

    // --- memories ---
    {
        mkdir((work_dir + "/memories").c_str(), 0755);
        auto mems = db.get_all_memories();
        if (!mems.empty()) {
            mkdir((work_dir + "/memories/memory_db").c_str(), 0755);
            json mem_meta;
            for (auto& m : mems) {
                std::ofstream f(work_dir + "/memories/memory_db/" + m.name);
                f << m.content;
                mem_meta[m.name] = {{"description", m.description}, {"created", m.created}};
            }
            std::ofstream f(work_dir + "/memories/memory_meta.json");
            f << mem_meta.dump(2);
            result.memories = (int)mems.size();
        }
    }

    // Zip it
    std::string cmd = "cd " + work_dir + " && zip -rq " + output_path + " .";
    std::string out;
    if (!run_cmd(cmd, out)) {
        result.error = "zip failed: " + out;
        cmd = "rm -rf " + work_dir;
        run_cmd(cmd, out);
        return result;
    }

    // Cleanup
    cmd = "rm -rf " + work_dir;
    run_cmd(cmd, out);

    result.success = true;
    return result;
}

static bool unzip_to(const std::string& zip_path, const std::string& dir) {
    std::string cmd = "unzip -oq \"" + zip_path + "\" -d \"" + dir + "\"";
    std::string out;
    return run_cmd(cmd, out);
}

std::string read_manifest(const std::string& input_path) {
    std::string work_dir = tmpdir() + "/agora_import_" + std::to_string(getpid());
    mkdir(work_dir.c_str(), 0755);
    if (!unzip_to(input_path, work_dir)) {
        std::string cmd = "rm -rf " + work_dir;
        std::string out;
        run_cmd(cmd, out);
        return "";
    }
    std::ifstream f(work_dir + "/manifest.json");
    std::string content;
    if (f.is_open()) {
        std::ostringstream oss;
        oss << f.rdbuf();
        content = oss.str();
    }
    std::string cmd = "rm -rf " + work_dir;
    std::string out;
    run_cmd(cmd, out);
    return content;
}

ImportResult import_data(const std::string& input_path,
                         const std::string& db_path,
                         const std::string& config_path,
                         const std::string& memory_dir) {
    ImportResult result;
    std::string work_dir = tmpdir() + "/agora_import_" + std::to_string(getpid());
    mkdir(work_dir.c_str(), 0755);

    if (!unzip_to(input_path, work_dir)) {
        result.error = "Failed to unzip file";
        return result;
    }

    Database db(db_path);
    db.begin_transaction();

    try {
        // --- conversations.json ---
        std::ifstream cf(work_dir + "/conversations.json");
        if (cf.is_open()) {
            json j = json::parse(cf);
            if (j.contains("conversations")) {
                for (auto& jc : j["conversations"]) {
                    Conversation conv;
                    conv.id = jc.value("id", "");
                    conv.title = jc.value("title", "Imported");
                    conv.last_updated = jc.value("last_updated", (int64_t)0);
                    conv.system_prompt_id = jc.value("system_prompt_id", "");
                    conv.model_id = jc.value("model_id", "");
                    db.upsert_conversation(conv);
                    result.conversations++;
                }
            }
            if (j.contains("messages")) {
                for (auto& jm : j["messages"]) {
                    Message msg;
                    msg.id = jm.value("id", "");
                    msg.conversation_id = jm.value("conversation_id", "");
                    msg.parent_id = jm.value("parent_id", "");
                    msg.text = jm.value("text", "");
                    msg.thoughts = jm.value("thoughts", "");
                    std::string part = jm.value("participant", "USER");
                    msg.participant = (part == "MODEL" ? Participant::MODEL :
                                       part == "ERROR" ? Participant::ERROR : Participant::USER);
                    msg.status = MessageStatus::SUCCESS;
                    msg.timestamp = jm.value("timestamp", (int64_t)0);
                    msg.model_name = jm.value("model_name", "");
                    db.upsert_message(msg);
                    result.messages++;
                }
            }
        }

        // --- system_prompts.json ---
        std::ifstream sf(work_dir + "/system_prompts.json");
        if (sf.is_open()) {
            json j = json::parse(sf);
            auto& cfg = Config::instance();
            if (j.contains("system_prompts")) {
                for (auto& jsp : j["system_prompts"]) {
                    SystemPromptEntry sp;
                    sp.id = jsp.value("id", "");
                    sp.title = jsp.value("title", "Imported");
                    sp.content = jsp.value("content", "");
                    sp.user_prepend = jsp.value("user_prepend", "");
                    sp.user_postpend = jsp.value("user_postpend", "");
                    // Don't duplicate existing IDs
                    bool exists = false;
                    for (auto& existing : cfg.system_prompts) {
                        if (existing.id == sp.id) { exists = true; break; }
                    }
                    if (!exists) {
                        cfg.system_prompts.push_back(sp);
                        db.upsert_system_prompt(sp);
                        result.prompts++;
                    }
                }
                cfg.save(config_path);
            }
        }

        // --- settings.json ---
        std::ifstream stf(work_dir + "/settings.json");
        if (stf.is_open()) {
            std::ofstream dst(config_path);
            dst << stf.rdbuf();
            // Reload config to pick up imported settings
            Config::instance().load(config_path);
        }

        // --- api_keys.json ---
        std::ifstream af(work_dir + "/api_keys.json");
        if (af.is_open()) {
            json j = json::parse(af);
            auto& cfg = Config::instance();
            if (j.contains("api_keys")) {
                for (auto& jak : j["api_keys"]) {
                    ApiKeyEntry ak;
                    ak.id = jak.value("id", "");
                    ak.name = jak.value("name", "");
                    ak.provider = jak.value("provider", "");
                    ak.key = jak.value("key", "");
                    bool exists = false;
                    for (auto& e : cfg.api_keys) {
                        if (e.provider == ak.provider) { exists = true; break; }
                    }
                    if (!exists) cfg.api_keys.push_back(ak);
                }
                cfg.save(config_path);
            }
        }

        // --- memories ---
        std::string mem_src = work_dir + "/memories/memory_db";
        struct stat st;
        if (stat(mem_src.c_str(), &st) == 0) {
            std::string mem_file = memory_dir;
            mkdir(mem_file.c_str(), 0755);
            std::string cmd = "cp -n \"" + mem_src + "\"/* \"" + mem_file + "\"/ 2>/dev/null";
            std::string out;
            run_cmd(cmd, out);
            result.memories = 1; // approximation
        }

        db.commit_transaction();
        result.success = true;
    } catch (const std::exception& e) {
        db.commit_transaction();
        result.error = e.what();
    }

    std::string cmd = "rm -rf " + work_dir;
    std::string out;
    run_cmd(cmd, out);

    return result;
}
