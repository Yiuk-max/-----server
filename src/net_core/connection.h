#pragma once
#include "total.h"
#include "receiver_sender.h"
#include <chrono>

// ============================================================
// connection（纯网络收发层）—— 方案 3b（重拆）彻底独立
//
// 在 3b 下，connection 被从 client_session 中彻底拆出，拥有独立的
// 生命周期，并由 epoll 事件循环（sub_reactor）直接面向它维护。
// client_session 退化为纯"会话 + 业务"，不再关心任何网络收发细节。
//
// 职责边界（本对象 = 纯网络收发 + 协议帧）：
//   - 网络层：封装 fd、epoll 读/写动作（供 epoller 的 sub_reactor 调用）
//   - 协议层：接收缓冲 / 帧解析（receiver_）与打包发送（sender_）
//   - 文件层：上传（receiver_）与下载（sender_）
//
// 生命周期与对象关系（避免 shared_ptr 环）：
//   - sub_reactor : shared_ptr<connection>     （epoll 直接持有 connection 及其 fd）
//   - connection  : shared_ptr<client_session> （connection 持有业务会话）
//   - client_sess : weak_ptr<connection>        （会话用弱引用回指 connection）
//   因此只有 epoller -> connection -> client_session 一条持有链，无环。
//   会话通过弱引用回指：即使 epoll 已回收 connection 而 session 仍在 session_manager
//   （例如连接断开但未登出），后续发消息也不会访问悬垂指针。
//
// 事件处理约定：
//   - EPOLLIN : sub_reactor 读取原始字节 -> append_raw_data(data)
//               -> 线程池调用 process_incoming()（切帧 + 交给 session 业务）
//   - EPOLLOUT: sub_reactor 调用 handle_write()（真正 send_msg）
// ============================================================
class client_session;

class connection {
public:
    connection(int epoll_fd, int fd);
    ~connection();

    // ---------- 供 epoller（sub_reactor）调用 ----------
    int fd() const { return client_fd_; }
    void handle_write();                              // EPOLLOUT：调用 sender_->send_msg()
    void append_raw_data(const std::string& data);    // 收到原始字节流 -> receiver_->append_data
    void process_incoming();                          // 切出所有完整帧并交给 session 业务（线程池调用）
    void attach_session(std::shared_ptr<client_session> session); // 绑定业务会话

    // ---------- 心跳 / 活动检测 ----------
    void update_active();            // 收到数据时刷新最后活动时间
    long long idle_seconds() const;  // 距最后活动已过去的秒数（<=0 表示活动未过期）
    bool is_idle(int timeout_s) const; // 超过 timeout_s 无活动则返回 true

    // ---------- 供 client_session / 业务层调用 ----------
    void package_message(const std::string& message, const std::string& type); // 打包+入发送缓冲
    void send_file(const std::string& file_name);            // 下载文件（sender_）
    void upload_file(const json& meta, const std::string& data); // 上传文件（receiver_）
    bool has_session() const { return session_ != nullptr; }

    // ---------- 访问器 ----------
    sender&   sender_obj();
    receiver& receiver_obj();

    // ---------- 连接生命周期 ----------
    void close();   // 关闭底层 fd（幂等）

private:
    Standard_Message next_frame();              // 从接收缓冲切出下一个完整帧（粘包/半包）

    int                        client_fd_;
    int                        epoll_fd_;
    std::unique_ptr<receiver>  receiver_;
    std::unique_ptr<sender>    sender_;
    std::mutex                 recv_mtx_;       // 串行化接收缓冲的追加与切帧
    std::shared_ptr<client_session> session_;   // 绑定到此连接的业务会话

    // 心跳/活动检测
    mutable std::mutex                 active_mtx_;     // 保护 last_active_
    std::chrono::steady_clock::time_point last_active_;  // 最后活动时刻（steady 时钟）
};

