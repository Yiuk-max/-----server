#pragma once
#include "total.h"
#include "receiver_sender.h"
#include "client_transport.h"
#include <atomic>
#include <chrono>
#include <functional>

// 现有 epoll/TCP 传输实现。
// connection 持有 client_session，会话通过 weak_ptr<IClientTransport> 回指，避免引用环。
class client_session;

class connection : public IClientTransport {
public:
    connection(int epoll_fd, int fd);
    ~connection() override;

    // ---------- 供 epoller（sub_reactor）调用 ----------
    int fd() const { return client_fd_.load(); }
    void handle_write();
    void append_raw_data(const std::string& data);
    void process_incoming();
    void attach_session(std::shared_ptr<client_session> session);
    void set_close_callback(std::function<void(int)> callback);

    // ---------- 心跳 / 活动检测 ----------
    void update_active();
    long long idle_seconds() const;
    bool is_idle(int timeout_s) const;

    // ---------- IClientTransport ----------
    void send_packet(json message, std::string file_data = {}) override;
    void send_file(std::string file_name) override;
    void accept_file_chunk(json meta, std::string file_data) override;
    void close(CloseMode mode) override;

    bool has_session() const { return session_ != nullptr; }

private:
    Standard_Message next_frame();
    void finish_close(int fd);

    std::atomic<int>              client_fd_;
    int                           epoll_fd_;
    std::unique_ptr<receiver>     receiver_;
    std::unique_ptr<sender>       sender_;
    std::mutex                    recv_mtx_;
    std::mutex                    lifecycle_mtx_;
    bool                          closing_after_write_ = false;
    bool                          closed_ = false;
    std::shared_ptr<client_session> session_;
    std::function<void(int)>      close_callback_;

    mutable std::mutex                 active_mtx_;
    std::chrono::steady_clock::time_point last_active_;
};
