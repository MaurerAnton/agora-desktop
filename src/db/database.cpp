#include "db/database.hpp"
#include <cstring>
#include <stdexcept>
#include <sstream>
#include "json.hpp"

using json = nlohmann::json;

Database::Database(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("Failed to open database: " + std::string(sqlite3_errmsg(db_)));
    }
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr);
    create_tables();
    migrate();
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

void Database::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("SQL error: " + msg + " (" + sql.substr(0, 100) + ")");
    }
}

void Database::exec_callback(const std::string& sql,
        std::function<bool(int, char**, char**)> callback) {
    struct Context {
        std::function<bool(int, char**, char**)> cb;
        bool stop = false;
    };
    Context ctx{callback, false};

    auto wrapper = [](void* data, int ncols, char** vals, char** cols) -> int {
        auto* c = static_cast<Context*>(data);
        if (c->cb(ncols, vals, cols)) return 0;
        c->stop = true;
        return 1;
    };

    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), wrapper, &ctx, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("SQL error: " + msg);
    }
}

void Database::create_tables() {
    exec(R"(
        CREATE TABLE IF NOT EXISTS conversations (
            id TEXT PRIMARY KEY,
            title TEXT NOT NULL,
            last_updated INTEGER NOT NULL DEFAULT 0,
            system_prompt_id TEXT,
            model_id TEXT
        )
    )");
    exec(R"(
        CREATE TABLE IF NOT EXISTS messages (
            id TEXT PRIMARY KEY,
            conversation_id TEXT NOT NULL,
            parent_id TEXT,
            text TEXT NOT NULL DEFAULT '',
            thoughts TEXT DEFAULT '',
            thought_title TEXT DEFAULT '',
            token_count INTEGER DEFAULT 0,
            status TEXT NOT NULL DEFAULT 'SUCCESS',
            participant TEXT NOT NULL DEFAULT 'USER',
            timestamp INTEGER NOT NULL,
            thought_time_ms INTEGER DEFAULT 0,
            model_name TEXT DEFAULT '',
            retry_text TEXT DEFAULT '',
            pinned INTEGER DEFAULT 0,
            segments_json TEXT DEFAULT '',
            tool_calls_json TEXT DEFAULT '',
            attachment_json TEXT DEFAULT '',
            FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE
        )
    )");
    exec("CREATE INDEX IF NOT EXISTS idx_messages_conv ON messages(conversation_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_messages_pinned ON messages(conversation_id, pinned)");

    exec(R"(
        CREATE TABLE IF NOT EXISTS memories (
            name TEXT PRIMARY KEY,
            content TEXT NOT NULL DEFAULT '',
            description TEXT DEFAULT '',
            created INTEGER NOT NULL DEFAULT 0
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS system_prompts (
            id TEXT PRIMARY KEY,
            title TEXT NOT NULL DEFAULT '',
            content TEXT NOT NULL DEFAULT '',
            user_prepend TEXT NOT NULL DEFAULT '',
            user_postpend TEXT NOT NULL DEFAULT ''
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS selected_branches (
            conversation_id TEXT NOT NULL,
            parent_id TEXT NOT NULL,
            child_id TEXT NOT NULL,
            PRIMARY KEY (conversation_id, parent_id),
            FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE
        )
    )");
}

void Database::migrate() {
    // Check if pins column exists, add if not
    // SQLite doesn't support DROP COLUMN or IF NOT EXISTS for ALTER easily
    // We'll try and ignore errors
    sqlite3_exec(db_, "ALTER TABLE messages ADD COLUMN pinned INTEGER DEFAULT 0",
        nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "ALTER TABLE messages ADD COLUMN segments_json TEXT DEFAULT ''",
        nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "ALTER TABLE messages ADD COLUMN tool_calls_json TEXT DEFAULT ''",
        nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "ALTER TABLE messages ADD COLUMN attachment_json TEXT DEFAULT ''",
        nullptr, nullptr, nullptr);
}

std::string Database::json_escape(const std::string& s) {
    // Basic JSON string escape for SQLite storage
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

// ---- Conversations ----

std::vector<Conversation> Database::get_all_conversations() {
    std::vector<Conversation> results;
    exec_callback(
        "SELECT id, title, last_updated, system_prompt_id, model_id "
        "FROM conversations ORDER BY last_updated DESC",
        [&](int n, char** v, char**) -> bool {
            Conversation c;
            c.id = v[0] ? v[0] : "";
            c.title = v[1] ? v[1] : "";
            c.last_updated = v[2] ? std::stoll(v[2]) : 0;
            c.system_prompt_id = v[3] ? v[3] : "";
            c.model_id = v[4] ? v[4] : "";
            results.push_back(std::move(c));
            return true;
        }
    );
    // Load branches for each conversation
    for (auto& conv : results) {
        conv.selected_branches = get_selected_branches(conv.id);
    }
    return results;
}

Conversation Database::get_conversation(const std::string& id) {
    Conversation result;
    std::string sql = "SELECT id, title, last_updated, system_prompt_id, model_id "
        "FROM conversations WHERE id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        result.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        result.last_updated = sqlite3_column_int64(stmt, 2);
        if (sqlite3_column_text(stmt, 3))
            result.system_prompt_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (sqlite3_column_text(stmt, 4))
            result.model_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    }
    sqlite3_finalize(stmt);
    result.selected_branches = get_selected_branches(id);
    return result;
}

void Database::upsert_conversation(const Conversation& conv) {
    std::string sql = "INSERT OR REPLACE INTO conversations "
        "(id, title, last_updated, system_prompt_id, model_id) VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, conv.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, conv.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, conv.last_updated);
    if (conv.system_prompt_id.empty())
        sqlite3_bind_null(stmt, 4);
    else
        sqlite3_bind_text(stmt, 4, conv.system_prompt_id.c_str(), -1, SQLITE_TRANSIENT);
    if (conv.model_id.empty())
        sqlite3_bind_null(stmt, 5);
    else
        sqlite3_bind_text(stmt, 5, conv.model_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // Update branches
    exec("DELETE FROM selected_branches WHERE conversation_id = '" + conv.id + "'");
    for (const auto& [parent, child] : conv.selected_branches) {
        std::string bs = "INSERT INTO selected_branches (conversation_id, parent_id, child_id) "
            "VALUES ('" + conv.id + "','" + parent + "','" + child + "')";
        exec(bs);
    }
}

void Database::delete_conversation(const std::string& id) {
    std::string sql = "DELETE FROM conversations WHERE id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::set_conversation_branch(const std::string& conv_id,
        const std::string& parent_id, const std::string& child_id) {
    std::string sql = "INSERT OR REPLACE INTO selected_branches "
        "(conversation_id, parent_id, child_id) VALUES (?, ?, ?)";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, conv_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, parent_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, child_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::map<std::string, std::string> Database::get_selected_branches(const std::string& conv_id) {
    std::map<std::string, std::string> branches;
    exec_callback(
        "SELECT parent_id, child_id FROM selected_branches WHERE conversation_id = '" + conv_id + "'",
        [&](int n, char** v, char**) -> bool {
            branches[v[0] ? v[0] : ""] = v[1] ? v[1] : "";
            return true;
        }
    );
    return branches;
}

// ---- Messages ----

static Message message_from_row(int ncols, char** vals, char** cols) {
    Message m;
    for (int i = 0; i < ncols; i++) {
        if (!cols[i] || !vals[i]) continue;
        std::string cn = cols[i];
        std::string v = vals[i];
        if (cn == "id") m.id = v;
        else if (cn == "conversation_id") m.conversation_id = v;
        else if (cn == "parent_id") m.parent_id = v;
        else if (cn == "text") m.text = v;
        else if (cn == "thoughts") m.thoughts = v;
        else if (cn == "thought_title") m.thought_title = v;
        else if (cn == "token_count") m.token_count = std::stoi(v);
        else if (cn == "status") m.status = string_to_status(v);
        else if (cn == "participant") m.participant = string_to_participant(v);
        else if (cn == "timestamp") m.timestamp = std::stoll(v);
        else if (cn == "thought_time_ms") m.thought_time_ms = std::stoll(v);
        else if (cn == "model_name") m.model_name = v;
        else if (cn == "retry_text") m.retry_text = v;
        else if (cn == "pinned") m.pinned = (v == "1");
    }
    return m;
}

std::vector<Message> Database::get_messages_for_conversation(const std::string& conv_id) {
    std::vector<Message> results;
    std::string sql = "SELECT * FROM messages WHERE conversation_id = ? ORDER BY timestamp ASC";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, conv_id.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Message m;
        m.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        m.conversation_id = conv_id;
        if (sqlite3_column_text(stmt, 2))
            m.parent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        m.text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (sqlite3_column_text(stmt, 4))
            m.thoughts = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (sqlite3_column_text(stmt, 5))
            m.thought_title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        m.token_count = sqlite3_column_int(stmt, 6);
        m.status = string_to_status(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)));
        m.participant = string_to_participant(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
        m.timestamp = sqlite3_column_int64(stmt, 9);
        m.thought_time_ms = sqlite3_column_int64(stmt, 10);
        if (sqlite3_column_text(stmt, 11))
            m.model_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        if (sqlite3_column_text(stmt, 12))
            m.retry_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));
        m.pinned = sqlite3_column_int(stmt, 13) != 0;

        // Parse JSON fields if they exist
        if (sqlite3_column_text(stmt, 14)) {
            try {
                std::string js = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 14));
                if (!js.empty()) m.segments = json::parse(js).get<std::vector<MessageSegment>>();
            } catch (...) {}
        }
        if (sqlite3_column_text(stmt, 15)) {
            try {
                std::string js = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 15));
                if (!js.empty()) m.tool_calls = json::parse(js).get<std::vector<ToolCall>>();
            } catch (...) {}
        }
        if (sqlite3_column_text(stmt, 16)) {
            try {
                std::string js = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 16));
                if (!js.empty()) m.attachments = json::parse(js).get<AttachmentMeta>();
            } catch (...) {}
        }
        results.push_back(std::move(m));
    }
    sqlite3_finalize(stmt);
    return results;
}

Message Database::get_message(const std::string& msg_id) {
    Message m;
    std::string sql = "SELECT * FROM messages WHERE id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, msg_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        m.id = msg_id;
        if (sqlite3_column_text(stmt, 1))
            m.conversation_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (sqlite3_column_text(stmt, 2))
            m.parent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        m.text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (sqlite3_column_text(stmt, 4))
            m.thoughts = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        m.pinned = sqlite3_column_int(stmt, 13) != 0;
    }
    sqlite3_finalize(stmt);
    return m;
}

void Database::upsert_message(const Message& msg) {
    std::string segments_json = json(msg.segments).dump();
    std::string tool_calls_json = json(msg.tool_calls).dump();
    std::string attachment_json = json(msg.attachments.items).dump();

    std::string sql = "INSERT OR REPLACE INTO messages "
        "(id, conversation_id, parent_id, text, thoughts, thought_title, "
        "token_count, status, participant, timestamp, thought_time_ms, model_name, "
        "retry_text, pinned, segments_json, tool_calls_json, attachment_json) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, msg.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, msg.conversation_id.c_str(), -1, SQLITE_TRANSIENT);
    if (msg.parent_id.empty())
        sqlite3_bind_null(stmt, 3);
    else
        sqlite3_bind_text(stmt, 3, msg.parent_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, msg.text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, msg.thoughts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, msg.thought_title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, msg.token_count);
    sqlite3_bind_text(stmt, 8, status_to_string(msg.status).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, participant_to_string(msg.participant).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 10, msg.timestamp);
    sqlite3_bind_int64(stmt, 11, msg.thought_time_ms);
    if (msg.model_name.empty())
        sqlite3_bind_null(stmt, 12);
    else
        sqlite3_bind_text(stmt, 12, msg.model_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, msg.retry_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 14, msg.pinned ? 1 : 0);
    sqlite3_bind_text(stmt, 15, segments_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 16, tool_calls_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 17, attachment_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // Update conversation last_updated
    std::string up = "UPDATE conversations SET last_updated = strftime('%s','now')*1000 "
        "WHERE id = '" + msg.conversation_id + "'";
    exec(up);
}

void Database::delete_messages_by_conversation(const std::string& conv_id) {
    std::string sql = "DELETE FROM messages WHERE conversation_id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, conv_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<Message> Database::get_pinned_messages(const std::string& conv_id) {
    std::vector<Message> results;
    std::string sql = "SELECT * FROM messages WHERE conversation_id = ? AND pinned = 1 ORDER BY timestamp ASC";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, conv_id.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Message m;
        m.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        m.conversation_id = conv_id;
        if (sqlite3_column_text(stmt, 2))
            m.parent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        m.text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        m.thoughts = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)) ?: "";
        m.participant = string_to_participant(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
        m.timestamp = sqlite3_column_int64(stmt, 9);
        m.pinned = true;
        results.push_back(std::move(m));
    }
    sqlite3_finalize(stmt);
    return results;
}

void Database::pin_message(const std::string& msg_id, bool pinned) {
    std::string sql = "UPDATE messages SET pinned = ? WHERE id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, pinned ? 1 : 0);
    sqlite3_bind_text(stmt, 2, msg_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<Message> Database::search_messages(const std::string& query, int limit) {
    std::vector<Message> results;
    std::string like = "%" + query + "%";
    std::string sql = "SELECT * FROM messages WHERE text LIKE ? AND participant IN ('USER','MODEL') "
        "ORDER BY timestamp DESC LIMIT ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, like.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Message m;
        m.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        m.text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        m.participant = string_to_participant(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
        m.timestamp = sqlite3_column_int64(stmt, 9);
        results.push_back(std::move(m));
    }
    sqlite3_finalize(stmt);
    return results;
}

// ---- Memories ----

std::vector<MemoryFile> Database::get_all_memories() {
    std::vector<MemoryFile> results;
    exec_callback(
        "SELECT name, content, description, created FROM memories ORDER BY name",
        [&](int, char** v, char**) -> bool {
            MemoryFile m;
            m.name = v[0] ? v[0] : "";
            m.content = v[1] ? v[1] : "";
            m.description = v[2] ? v[2] : "";
            m.created = v[3] ? std::stoll(v[3]) : 0;
            results.push_back(std::move(m));
            return true;
        }
    );
    return results;
}

MemoryFile Database::get_memory(const std::string& name) {
    MemoryFile m;
    std::string sql = "SELECT name, content, description, created FROM memories WHERE name = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        m.name = name;
        m.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (sqlite3_column_text(stmt, 2))
            m.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        m.created = sqlite3_column_int64(stmt, 3);
    }
    sqlite3_finalize(stmt);
    return m;
}

void Database::upsert_memory(const MemoryFile& mem) {
    std::string sql = "INSERT OR REPLACE INTO memories (name, content, description, created) "
        "VALUES (?, ?, ?, ?)";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, mem.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, mem.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, mem.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, mem.created);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::delete_memory(const std::string& name) {
    std::string sql = "DELETE FROM memories WHERE name = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// ---- System Prompts ----

std::vector<SystemPromptEntry> Database::get_all_system_prompts() {
    std::vector<SystemPromptEntry> results;
    exec_callback(
        "SELECT id, title, content, user_prepend, user_postpend FROM system_prompts",
        [&](int, char** v, char**) -> bool {
            SystemPromptEntry sp;
            sp.id = v[0] ? v[0] : "";
            sp.title = v[1] ? v[1] : "";
            sp.content = v[2] ? v[2] : "";
            sp.user_prepend = v[3] ? v[3] : "";
            sp.user_postpend = v[4] ? v[4] : "";
            results.push_back(std::move(sp));
            return true;
        }
    );
    return results;
}

SystemPromptEntry Database::get_system_prompt(const std::string& id) {
    SystemPromptEntry sp;
    std::string sql = "SELECT id, title, content, user_prepend, user_postpend FROM system_prompts WHERE id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        sp.id = id;
        sp.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        sp.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (sqlite3_column_text(stmt, 3))
            sp.user_prepend = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (sqlite3_column_text(stmt, 4))
            sp.user_postpend = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    }
    sqlite3_finalize(stmt);
    return sp;
}

void Database::upsert_system_prompt(const SystemPromptEntry& sp) {
    std::string sql = "INSERT OR REPLACE INTO system_prompts "
        "(id, title, content, user_prepend, user_postpend) VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, sp.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sp.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sp.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, sp.user_prepend.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, sp.user_postpend.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::delete_system_prompt(const std::string& id) {
    std::string sql = "DELETE FROM system_prompts WHERE id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::begin_transaction() {
    exec("BEGIN TRANSACTION");
}

void Database::commit_transaction() {
    exec("COMMIT");
}
