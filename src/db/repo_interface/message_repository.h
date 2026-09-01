#pragma once
#include <memory>
#include <string>
#include <tuple>
#include <vector>

struct message {
    int message_id;         // 消息 ID（自增主键）
    int sender_UID;        // 发送者 UID
    int receiver_UID;      // 接收者 UID（私聊）或群聊 UID（群聊）
    std::string content;   // 消息内容
    bool is_group;         // 是否为群聊消息
    std::string timestamp; // 时间戳
};
//存储信息、定时删除信息、查询没有收到的私聊、群聊离线信息、查询私聊/群聊历史消息

class I_message_repo {
public:
    virtual ~I_message_repo() = default;

    // 存储消息：成功返回数据库分配的 message_id，失败返回 -1
    virtual int store_message(int sender_UID, int receiver_UID, const std::string& message, bool is_group) = 0;

    virtual bool delete_message(int message_id) = 0;

    virtual bool delete_expired_messages() = 0;

    // 按 id 查询单条消息；不存在返回 false
    virtual bool get_message(int message_id, message& out) = 0;

    // 查询自 since_time 之后发给该用户的离线消息（私聊 + 所在群群聊）
    virtual std::vector<message> get_offline_messages(int receiver_UID, const std::string& since_time) = 0;

    virtual std::vector<message> get_chat_history(int user1_UID, int user2_UID, int limit) = 0;

};