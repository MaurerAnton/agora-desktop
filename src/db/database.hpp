#pragma once

#include "models/message.hpp"
#include "models/conversation.hpp"
#include "utils/config.hpp"
#include <sqlite3.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Conversations
    std::vector<Conversation> get_all_conversations();
    Conversation get_conversation(const std::string& id);
    void upsert_conversation(const Conversation& conv);
    void delete_conversation(const std::string& id);
    void set_conversation_branch(const std::string& conv_id,
        const std::string& parent_id, const std::string& child_id);
    std::map<std::string, std::string> get_selected_branches(const std::string& conv_id);

    // Messages (tree structure)
    std::vector<Message> get_messages_for_conversation(const std::string& conv_id);
    Message get_message(const std::string& msg_id);
    void upsert_message(const Message& msg);
    void delete_messages_by_conversation(const std::string& conv_id);
    std::vector<Message> get_pinned_messages(const std::string& conv_id);
    void pin_message(const std::string& msg_id, bool pinned);
    std::vector<Message> search_messages(const std::string& query, int limit = 50);

    // Memories
    std::vector<MemoryFile> get_all_memories();
    MemoryFile get_memory(const std::string& name);
    void upsert_memory(const MemoryFile& mem);
    void delete_memory(const std::string& name);

    // System prompts
    std::vector<SystemPromptEntry> get_all_system_prompts();
    SystemPromptEntry get_system_prompt(const std::string& id);
    void upsert_system_prompt(const SystemPromptEntry& sp);
    void delete_system_prompt(const std::string& id);

    // Transaction helpers
    void begin_transaction();
    void commit_transaction();

private:
    void create_tables();
    void migrate();
    sqlite3* db_ = nullptr;
    void exec(const std::string& sql);
    void exec_callback(const std::string& sql,
        std::function<bool(int, char**, char**)> callback);
    std::string json_escape(const std::string& s);
};

inline std::string status_to_string(MessageStatus s) {
    switch (s) {
        case MessageStatus::SENDING: return "SENDING";
        case MessageStatus::THINKING: return "THINKING";
        case MessageStatus::TOOL_CALLING: return "TOOL_CALLING";
        case MessageStatus::SUCCESS: return "SUCCESS";
        case MessageStatus::STOPPED: return "STOPPED";
        case MessageStatus::ERROR: return "ERROR";
    }
    return "SUCCESS";
}

inline MessageStatus string_to_status(const std::string& s) {
    if (s == "SENDING") return MessageStatus::SENDING;
    if (s == "THINKING") return MessageStatus::THINKING;
    if (s == "TOOL_CALLING") return MessageStatus::TOOL_CALLING;
    if (s == "STOPPED") return MessageStatus::STOPPED;
    if (s == "ERROR") return MessageStatus::ERROR;
    return MessageStatus::SUCCESS;
}

inline std::string participant_to_string(Participant p) {
    switch (p) {
        case Participant::USER: return "USER";
        case Participant::MODEL: return "MODEL";
        case Participant::ERROR: return "ERROR";
    }
    return "USER";
}

inline Participant string_to_participant(const std::string& s) {
    if (s == "MODEL") return Participant::MODEL;
    if (s == "ERROR") return Participant::ERROR;
    return Participant::USER;
}
