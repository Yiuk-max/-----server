#pragma once

#include <cstdint>
#include <string>

// ============================================================
// WebSocket 应用层消息编解码（纯函数，便于单元测试）
//
//   - 普通业务消息：WebSocket binary 消息，payload 直接是 protobuf 字节。
//   - 文件消息    ：WebSocket binary 消息，payload 为
//         | 4 字节 payload_len (网络序) | payload_len 字节 protobuf | 剩余文件数据 |
//
// WebSocket 自带消息边界，因此不复用 TCP 的 total_len 字段。
// ============================================================
namespace ws_protocol {

// 把 payload + 文件数据编码为 binary 消息 payload。
std::string encode_binary(const std::string& payload, const std::string& file_data);

// 解析 binary 消息 payload。成功返回 true；失败返回 false 并写入 error，
// 此时不保证 payload/file_part 的最终内容。
bool decode_binary(const std::string& frame,
                   std::string& payload,
                   std::string& file_part,
                   std::string& error);

}  // namespace ws_protocol
