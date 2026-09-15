#include "ws_protocol.h"

#include <arpa/inet.h>
#include <cstring>

namespace ws_protocol {

std::string encode_binary(const std::string& payload, const std::string& file_data) {
    if (payload.size() > UINT32_MAX) {
        return {};
    }
    const uint32_t net_len = htonl(static_cast<uint32_t>(payload.size()));

    std::string out;
    out.reserve(4 + payload.size() + file_data.size());
    out.append(reinterpret_cast<const char*>(&net_len), sizeof(net_len));
    out += payload;
    out += file_data;
    return out;
}

bool decode_binary(const std::string& frame,
                   std::string& payload,
                   std::string& file_part,
                   std::string& error) {
    if (frame.size() < 4) {
        error = "binary payload smaller than 4-byte length header";
        return false;
    }

    uint32_t net_len = 0;
    std::memcpy(&net_len, frame.data(), sizeof(net_len));
    const uint32_t payload_len = ntohl(net_len);
    if (payload_len > frame.size() - 4) {
        error = "payload length exceeds binary frame size";
        return false;
    }

    payload.assign(frame, 4, payload_len);
    file_part.assign(frame, 4 + payload_len, frame.size() - 4 - payload_len);
    return true;
}

}  // namespace ws_protocol
