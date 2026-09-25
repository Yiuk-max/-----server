#pragma once
#include <ctime>
#include <string>

class client_session;

namespace logic {
    // 解析 MySQL DATETIME "YYYY-MM-DD HH:MM:SS" 为 time_t；失败返回 0
    std::time_t parse_datetime(const std::string& s);

    // 生成随机登录令牌（64 位十六进制）
    std::string generate_token();

    // 校验目标 UID 是否存在（个人或群聊 UID），不存在时向当前会话回错误消息
    bool target_uid_exists(client_session& s, int target_uid);

    // 校验目标 UID 是否在线，不在线时向当前会话回错误消息
    bool target_uid_online(client_session& s, int target_uid);

    // 查看待处理的好友申请（供登录收尾与好友模块复用）
    void show_friend_requests(client_session& s);

    // 查询并推送自 since_time 之后的离线消息（登录收尾用）
    void send_offline_messages(client_session& s, const std::string& since_time);

    // 查“被回复的原消息”摘要；成功填充 sender_name/content 并返回 true
    bool load_reply_summary(client_session& s, int reply_to_message_id,
                            std::string& sender_name, std::string& content);
}
