#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "json.hpp"

enum class Participant {
    USER,
    MODEL,
    ERROR
};

enum class MessageStatus {
    SENDING,
    THINKING,
    TOOL_CALLING,
    SUCCESS,
    STOPPED,
    ERROR
};

struct ToolCall {
    std::string tool_call_id;
    std::string name;
    std::string arguments;
    std::string result;
};

struct MessageSegment {
    std::string type;  // "answer", "thought", "tool", "transcription"
    std::string content;
    std::string tool_name;
    std::string tool_args;
    std::string tool_result;
    std::string tool_call_id;
    int64_t duration_ms = 0;
};

struct AttachmentItem {
    std::string original_uri;
    std::string type;      // "image", "video", "file", "pdf"
    std::string file_name;
    std::string mime_type;
    std::string text_content;
    std::string transcription;
};

struct AttachmentMeta {
    std::vector<AttachmentItem> items;
};

struct Message {
    std::string id;
    std::string conversation_id;
    std::string parent_id;       // empty = root
    std::string text;
    std::string thoughts;
    std::string thought_title;
    int token_count = 0;
    MessageStatus status = MessageStatus::SUCCESS;
    Participant participant = Participant::USER;
    int64_t timestamp = 0;
    int64_t thought_time_ms = 0;
    std::string model_name;
    std::string retry_text;
    bool pinned = false;         // NEW: pinned message support

    // Serialized fields
    std::vector<MessageSegment> segments;
    std::vector<ToolCall> tool_calls;
    AttachmentMeta attachments;

    // In-memory only
    bool streaming = false;
    std::string streaming_text;
    std::string streaming_thoughts;
};

struct Conversation {
    std::string id;
    std::string title;
    int64_t last_updated = 0;
    std::string system_prompt_id;
    std::string model_id;
    std::map<std::string, std::string> selected_branches;  // parent_id -> chosen_child_id
};

// JSON serialization
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ToolCall, tool_call_id, name, arguments, result)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MessageSegment, type, content, tool_name, tool_args, tool_result, tool_call_id, duration_ms)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AttachmentItem, original_uri, type, file_name, mime_type, text_content, transcription)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AttachmentMeta, items)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Message, id, conversation_id, parent_id, text, thoughts, thought_title,
    token_count, model_name, retry_text, pinned, timestamp, thought_time_ms)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Conversation, id, title, last_updated, system_prompt_id, model_id)
