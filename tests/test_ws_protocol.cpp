// WebSocket binary 应用层编解码单元测试（纯函数，无网络/数据库）。
//   协议：| 4 字节 json_len (网络序) | JSON | 文件数据 |
#include "ws_protocol.h"

#include <arpa/inet.h>
#include <cassert>
#include <iostream>
#include <string>

int main() {
    // 1. encode -> decode 闭环（无文件）
    {
        nlohmann::json meta{{"type", "download_file"}, {"file_name", "a.txt"}};
        std::string payload = ws_protocol::encode_binary(meta, {});
        assert(payload.size() >= 4);

        std::string json_part, file_part, error;
        assert(ws_protocol::decode_binary(payload, json_part, file_part, error));
        assert(json_part == meta.dump());
        assert(file_part.empty());
        std::cout << "[ok] encode/decode json only" << std::endl;
    }

    // 2. encode -> decode 闭环（含二进制文件数据）
    {
        nlohmann::json meta{{"type", "file_chunk"}, {"chunk_index", 0}};
        const std::string file_data("BINARY\x00\x01\x02\xff", 10);
        std::string payload = ws_protocol::encode_binary(meta, file_data);

        std::string json_part, file_part, error;
        assert(ws_protocol::decode_binary(payload, json_part, file_part, error));
        assert(json_part == meta.dump());
        assert(file_part == file_data);
        std::cout << "[ok] encode/decode with file data" << std::endl;
    }

    // 3. 长度头不足以容纳 JSON -> 失败
    {
        std::string bad;
        const uint32_t net_len = htonl(1000);  // 声明 1000 字节 JSON，实际没有
        bad.append(reinterpret_cast<const char*>(&net_len), 4);
        bad += "short";

        std::string json_part, file_part, error;
        assert(!ws_protocol::decode_binary(bad, json_part, file_part, error));
        assert(!error.empty());
        std::cout << "[ok] reject json length overflow" << std::endl;
    }

    // 4. payload 小于 4 字节 -> 失败
    {
        std::string json_part, file_part, error;
        assert(!ws_protocol::decode_binary("ab", json_part, file_part, error));
        assert(!error.empty());
        std::cout << "[ok] reject short payload" << std::endl;
    }

    // 5. 空文件数据合法
    {
        nlohmann::json meta{{"type", "x"}};
        std::string payload = ws_protocol::encode_binary(meta, {});
        std::string json_part, file_part, error;
        assert(ws_protocol::decode_binary(payload, json_part, file_part, error));
        assert(file_part.empty());
        std::cout << "[ok] empty file data" << std::endl;
    }

    std::cout << "all ws protocol tests passed" << std::endl;
    return 0;
}
