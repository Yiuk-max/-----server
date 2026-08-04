#pragma once
#include <string>
#include <vector>

// ============================================================
// 全局通知服务（依赖倒置的"端口"）。
//
// 作用：把"向在线用户发送系统/聊天消息"的能力从业务逻辑中解耦出来。
//       业务层（logic/group.cpp、logic/social_module.cpp 等）只需要
//       通过本服务向某个在线用户发消息，而不再直接依赖
//       session_manager / client_session 这些网络层实体。
//
// 实现细节（如何按 UID 找到连接并真正发送）封装在 notice_service.cpp，
// 头文件只暴露面向业务的语义化接口。
// ============================================================
class NoticeService {
public:
    static NoticeService& get_instance();

    // 向指定用户 UID 发送一条消息（type 为 "system" / "private_chat" / "Group_Chat" 等）
    void send_to_user(int uid, const std::string& message, const std::string& type);

    // 向多个用户 UID 广播同一条消息（忽略不在线的用户）
    void send_to_users(const std::vector<int>& uid_list, const std::string& message, const std::string& type);

    // 查询某 UID 当前是否在线
    bool is_online(int uid);

private:
    NoticeService() = default;
    NoticeService(const NoticeService&) = delete;
    NoticeService& operator=(const NoticeService&) = delete;
};
