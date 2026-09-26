#pragma once
#include <cstdint>
#include <optional>
#include <string>

// ============================================================
// 聊天消息数据（普通消息 + 文件消息统一载体）
// 取代原先 package_chat_message / build_chat_envelope / NoticeService
// 的一长串参数，后续新增字段只需改这里，不用再动所有调用点。
// ============================================================

// 回复引用摘要
struct ReplyInfo {
    int         message_id = 0;
    std::string sender_name;
    std::string content;
};

// 文件消息元数据
struct FileInfo {
    int         file_id = 0;
    std::string name;
    uint64_t    size = 0;
};

struct ChatMessageData {
    std::string message;         // 聊天正文（文件消息可为空）
    std::string type;            // private_chat / Group_Chat / Channel_Chat
    int         message_id = 0;
    int         group_uid   = 0;  // 群聊=群ID，频道=频道ID；私聊=0
    int         sender_uid  = 0;
    std::string sender_name;
    std::string timestamp;

    std::optional<ReplyInfo> reply;  // 回复引用（可空）
    std::optional<FileInfo>  file;   // 文件消息（可空）
};
