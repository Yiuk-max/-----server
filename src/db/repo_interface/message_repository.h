#pragma once
#include <memory>
#include <string>
#include <tuple>
#include <vector>

struct message {
    int message_id;         // 消息 ID（自增主键）
    int sender_UID;        // 发送者 UID
    int receiver_UID;      // 接收者 UID（私聊）或群聊/频道 ID（群聊/频道）
    std::string content;   // 消息内容
    std::string type;      // 消息类型："private" / "group" / "channel"
    std::string timestamp; // 时间戳
    std::string sender_name; // 发送者昵称（仅历史查询时填充，便于前端展示）

    // 是否为群聊型消息（group / channel）
    bool is_group() const { return type != "private"; }

    // 回复：本条消息回复的原消息 id（0=非回复）；
    // reply_sender_name / reply_content 为原消息摘要（历史查询时 JOIN 填充）。
    int reply_to_message_id = 0;
    std::string reply_sender_name;
    std::string reply_content;

    // 文件消息：is_file=1 时本消息只存 file_id 引用，文件本体按 file_id 单独下载。
    // file_name/file_size 由查询时 JOIN file 表填充，用于前端展示。
    bool     is_file   = false;
    int      file_id   = 0;
    std::string file_name;
    uint64_t file_size = 0;
};
//存储信息、定时删除信息、查询没有收到的私聊、群聊离线信息、查询私聊/群聊历史消息

class I_message_repo {
public:
    virtual ~I_message_repo() = default;

    // 存储消息：成功返回数据库分配的 message_id，失败返回 -1
    // 存储消息：成功返回数据库分配的 message_id，失败返回 -1。
    // reply_to_message_id > 0 表示本条是回复该 id 的消息（只支持一层直接引用）。
    virtual int store_message(int sender_UID, int receiver_UID, const std::string& message,
                              const std::string& type, int reply_to_message_id = 0,
                              std::string* out_timestamp = nullptr,
                              bool is_file = false, int file_id = 0) = 0;

    virtual bool delete_message(int message_id) = 0;

    virtual bool delete_expired_messages() = 0;

    // 按 id 查询单条消息；不存在返回 false
    virtual bool get_message(int message_id, message& out) = 0;

    // 查询自 since_time 之后发给该用户的离线消息（私聊 + 所在群群聊）
    virtual std::vector<message> get_offline_messages(int receiver_UID, const std::string& since_time) = 0;

    // 游标分页查聊天历史（用 message.id 作游标，供前端"上滑加载更多"）。
    //   self_uid  ：当前登录用户
    //   peer_uid  ：对方 UID（私聊）或群/频道 ID（群聊/频道）
    //   type      ：消息类型 'private' / 'group' / 'channel'
    //   before_id ：只取 id < before_id 的记录；<=0 表示取最新一页（登录后首屏）
    //   limit     ：本页最多返回多少条
    //   out       ：按时间正序（id 升序，旧→新）返回
    //   has_more  ：是否还有更旧的记录（前端据此决定是否还能上滑加载）
    // 成功返回 true。
    virtual bool get_history_page(int self_uid, int peer_uid, const std::string& type,
                                  int before_id, int limit,
                                  std::vector<message>& out, bool& has_more) = 0;

};