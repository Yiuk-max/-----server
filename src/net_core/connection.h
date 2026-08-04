#pragma once
#include "total.h"
#include "receiver_sender.h"

// ============================================================
// connection（纯网络收发层）
//
// 方案 3a（轻拆）：把一条 TCP 连接的"收发部分"（receiver / sender、
// 帧解析、打包发送、文件收发）从 client_session 中拆出，收敛成一个
// 独立的 connection 对象。client_session 持有 connection，对外 API
// （package_message / handle 等签名）保持不变，只是内部瘦身。
//
// 职责边界（本对象只管"网络收发 + 协议帧"，不含任何业务/会话状态）：
//   - 网络层：封装 fd、epoll 读/写动作（供 epoller 的 sub_reactor 调用）
//   - 协议层：帧的解析（receiver_->process_recv_data）与打包发送
//             （package_message 序列化写进发送缓冲区）
//   - 文件层：上传（receiver_）与下载（sender_）
//
// 生命周期：connection 拥有底层 fd，析构时负责关闭。
// ============================================================
class connection {
public:
    connection(int epoll_fd, int fd);
    ~connection();

    // ---------- 供 epoller（sub_reactor）调用 ----------
    int fd() const { return client_fd_; }
    void handle_write();                        // EPOLLOUT：调用 sender_->send_msg()
    void append_raw_data(const std::string& data);   // 收到原始字节流 -> receiver_->append_data
    Standard_Message next_frame();              // 从接收缓冲切出下一个完整帧（粘包/半包）

    // ---------- 供 client_session / 业务层调用 ----------
    void package_message(const std::string& message, const std::string& type); // 打包+入发送缓冲
    void send_file(const std::string& file_name);            // 下载文件（sender_）
    void upload_file(const json& meta, const std::string& data); // 上传文件（receiver_）

    // ---------- 访问器（message_handler 等需要直连收发模块）----------
    sender&   sender_obj();
    receiver& receiver_obj();

    // ---------- 连接生命周期 ----------
    void close();   // 关闭底层 fd（幂等）

private:
    int                        client_fd_;
    int                        epoll_fd_;
    std::unique_ptr<receiver>  receiver_;
    std::unique_ptr<sender>    sender_;
};
