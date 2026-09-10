#include "ws_protocol.h"

#include <arpa/inet.h>
#include <cstring>

namespace ws_protocol {

std::string encode_binary(const nlohmann::json& meta, const std::string& file_data) {
    const std::string js = meta.dump();
    if (js.size() > UINT32_MAX) {
        return {};
    }
    const uint32_t net_len = htonl(static_cast<uint32_t>(js.size()));

    std::string out;
    out.reserve(4 + js.size() + file_data.size());
    out.append(reinterpret_cast<const char*>(&net_len), sizeof(net_len));
    out += js;
    out += file_data;
    return out;
}

bool decode_binary(const std::string& payload,
                   std::string& json_part,
                   std::string& file_part,
                   std::string& error) {
    if (payload.size() < 4) {
        error = "binary payload smaller than 4-byte length header";
        return false;
    }

    uint32_t net_len = 0;
    std::memcpy(&net_len, payload.data(), sizeof(net_len));
    const uint32_t json_len = ntohl(net_len);
    if (json_len > payload.size() - 4) {
        error = "json length exceeds binary payload size";
        return false;
    }

    json_part.assign(payload, 4, json_len);
    file_part.assign(payload, 4 + json_len, payload.size() - 4 - json_len);
    return true;
}

}  // namespace ws_protocol
