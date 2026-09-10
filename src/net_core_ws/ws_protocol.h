#pragma once

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

// ============================================================
// WebSocket 应用层消息编解码（纯函数，便于单元测试）
//
//   - 普通业务消息：WebSocket text 消息，payload 直接是完整 JSON。
//   - 文件消息    ：WebSocket binary 消息，payload 为
//         | 4 字节 json_len (网络序) | json_len 字节 JSON | 剩余文件数据 |
//
// WebSocket 自带消息边界，因此不复用 TCP 的 total_len 字段。
// ============================================================
namespace ws_protocol {

// 把 JSON + 文件数据编码为 binary 消息 payload。
std::string encode_binary(const nlohmann::json& meta, const std::string& file_data);

// 解析 binary 消息 payload。成功返回 true；失败返回 false 并写入 error，
// 此时不保证 json_part/file_part 的最终内容。
bool decode_binary(const std::string& payload,
                   std::string& json_part,
                   std::string& file_part,
                   std::string& error);

}  // namespace ws_protocol
