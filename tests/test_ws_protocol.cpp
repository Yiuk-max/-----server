// WebSocket binary 应用层编解码单元测试（纯函数，无网络/数据库）。
//   协议：| 4 字节 payload_len (网络序) | protobuf 字节 | 文件数据 |
#include "ws_protocol.h"

#include <arpa/inet.h>
#include <cassert>
#include <iostream>
#include <string>

int main() {
    // 1. encode -> decode 闭环（无文件）
    {
        const std::string payload("protobuf-bytes-1");
        std::string frame = ws_protocol::encode_binary(payload, {});
        assert(frame.size() >= 4);

        std::string body, file_part, error;
        assert(ws_protocol::decode_binary(frame, body, file_part, error));
        assert(body == payload);
        assert(file_part.empty());
        std::cout << "[ok] encode/decode payload only" << std::endl;
    }

    // 2. encode -> decode 闭环（含二进制文件数据）
    {
        const std::string payload("protobuf-bytes-2");
        const std::string file_data("BINARY\x00\x01\x02\xff", 10);
        std::string frame = ws_protocol::encode_binary(payload, file_data);

        std::string body, file_part, error;
        assert(ws_protocol::decode_binary(frame, body, file_part, error));
        assert(body == payload);
        assert(file_part == file_data);
        std::cout << "[ok] encode/decode with file data" << std::endl;
    }

    // 3. 长度头不足以容纳 payload -> 失败
    {
        std::string bad;
        const uint32_t net_len = htonl(1000);  // 声明 1000 字节 payload，实际没有
        bad.append(reinterpret_cast<const char*>(&net_len), 4);
        bad += "short";

        std::string body, file_part, error;
        assert(!ws_protocol::decode_binary(bad, body, file_part, error));
        assert(!error.empty());
        std::cout << "[ok] reject payload length overflow" << std::endl;
    }

    // 4. frame 小于 4 字节 -> 失败
    {
        std::string body, file_part, error;
        assert(!ws_protocol::decode_binary("ab", body, file_part, error));
        assert(!error.empty());
        std::cout << "[ok] reject short frame" << std::endl;
    }

    // 5. 空文件数据合法
    {
        const std::string payload("x");
        std::string frame = ws_protocol::encode_binary(payload, {});
        std::string body, file_part, error;
        assert(ws_protocol::decode_binary(frame, body, file_part, error));
        assert(body == payload);
        assert(file_part.empty());
        std::cout << "[ok] empty file data" << std::endl;
    }

    std::cout << "all ws protocol tests passed" << std::endl;
    return 0;
}
