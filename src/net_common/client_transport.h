#pragma once

#include <string>

#include "message.pb.h"

// 连接关闭策略：协议/IO 错误立即关闭；正常退出、顶号则等待待发送消息写完。
enum class CloseMode {
    immediate,
    after_pending_writes,
};

// client_session 面向的传输端口。
// 业务层只构造 protobuf Envelope，不感知 TCP 帧头或 WebSocket 帧格式。
class IClientTransport {
public:
    virtual ~IClientTransport() = default;

    // 可由业务线程调用；实现负责线程安全以及协议打包。
    virtual void send_packet(const chat_proto::Envelope& message,
                             std::string file_data = {}) = 0;

    // 发送已序列化的 protobuf 字节（跳过 SerializeToString，供广播复用同一份 payload）。
    virtual void send_serialized(const std::string& payload) = 0;

    // M0 仅用于保持现有 TCP 文件功能可编译；WebSocket 文件协议不在本阶段实现。
    virtual void send_file(std::string file_name) = 0;
    virtual void accept_file_chunk(const chat_proto::FileChunkMeta& meta,
                                   std::string file_data) = 0;

    virtual void close(CloseMode mode) = 0;
};
