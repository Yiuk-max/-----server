// TCP 帧协议无数据库依赖测试：
//   1. total_len 语义为“整帧长度”，与客户端接口文档一致
//   2. 半包等待、粘包连续解析
//   3. 发送端生成的长度字段与实际包长一致
//
// 编译示例见文件末尾注释；不参与默认 server 构建。
#include "receiver_sender.h"

#include <arpa/inet.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::string make_frame(const std::string& json_text, const std::string& file_data) {
    const uint32_t total = static_cast<uint32_t>(8 + json_text.size() + file_data.size());
    const uint32_t json_len = static_cast<uint32_t>(json_text.size());
    const uint32_t net_total = htonl(total);
    const uint32_t net_json = htonl(json_len);

    std::string frame;
    frame.append(reinterpret_cast<const char*>(&net_total), 4);
    frame.append(reinterpret_cast<const char*>(&net_json), 4);
    frame.append(json_text);
    frame.append(file_data);
    return frame;
}

void test_full_frame() {
    int efd = epoll_create1(0);
    receiver r(efd, -1);

    const std::string json_text = R"({"type":"login","UID":1001})";
    r.append_data(make_frame(json_text, {}));
    Standard_Message msg = r.process_recv_data("");
    assert(msg.is_valid);
    assert(msg.json_part == json_text);
    assert(msg.file_part.empty());

    // 缓冲已清空：再次处理不应产生新帧
    assert(!r.process_recv_data("").is_valid);
    close(efd);
    std::cout << "[ok] full frame" << std::endl;
}

void test_half_packet() {
    int efd = epoll_create1(0);
    receiver r(efd, -1);

    const std::string json_text = R"({"type":"show"})";
    const std::string frame = make_frame(json_text, {});

    r.append_data(frame.substr(0, 5));               // 连包头都不完整
    assert(!r.process_recv_data("").is_valid);
    r.append_data(frame.substr(5, 3));               // 包头完整、JSON 不完整
    assert(!r.process_recv_data("").is_valid);
    r.append_data(frame.substr(8));                  // 补全
    Standard_Message msg = r.process_recv_data("");
    assert(msg.is_valid);
    assert(msg.json_part == json_text);
    close(efd);
    std::cout << "[ok] half packet" << std::endl;
}

void test_sticky_packets() {
    int efd = epoll_create1(0);
    receiver r(efd, -1);

    const std::string a = R"({"type":"a"})";
    const std::string b = R"({"type":"b"})";
    r.append_data(make_frame(a, {}) + make_frame(b, {}));

    Standard_Message first = r.process_recv_data("");
    Standard_Message second = r.process_recv_data("");
    assert(first.is_valid && first.json_part == a);
    assert(second.is_valid && second.json_part == b);
    assert(!r.process_recv_data("").is_valid);
    close(efd);
    std::cout << "[ok] sticky packets" << std::endl;
}

void test_file_frame() {
    int efd = epoll_create1(0);
    receiver r(efd, -1);

    const std::string json_text = R"({"type":"upload_file"})";
    const std::string file_data("BINARY\x00\x01\x02", 9);
    r.append_data(make_frame(json_text, file_data));

    Standard_Message msg = r.process_recv_data("");
    assert(msg.is_valid);
    assert(msg.json_part == json_text);
    assert(msg.file_part == file_data);
    close(efd);
    std::cout << "[ok] file frame" << std::endl;
}

void test_invalid_length_is_dropped() {
    int efd = epoll_create1(0);
    receiver r(efd, -1);

    // total_len < 8 非法：应丢弃缓冲而不是无限等待
    uint32_t bad = htonl(4);
    std::string buf;
    buf.append(reinterpret_cast<const char*>(&bad), 4);
    buf.append(8, '\0');
    r.append_data(buf);
    assert(!r.process_recv_data("").is_valid);
    close(efd);
    std::cout << "[ok] invalid length dropped" << std::endl;
}

void test_sender_length_matches_packet() {
    int efd = epoll_create1(0);
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    sender s(efd, sv[0]);
    const std::string json_text = R"({"type":"system","content":"hi"})";
    const std::string file_data("xyz", 3);

    // 复用与 connection 相同的组帧方式，验证长度口径一致
    const uint32_t total = static_cast<uint32_t>(8 + json_text.size() + file_data.size());
    const uint32_t json_len = static_cast<uint32_t>(json_text.size());
    const uint32_t net_total = htonl(total);
    const uint32_t net_json = htonl(json_len);
    std::string packet;
    packet.append(reinterpret_cast<const char*>(&net_total), 4);
    packet.append(reinterpret_cast<const char*>(&net_json), 4);
    packet.append(json_text);
    packet.append(file_data);
    s.add_to_out_buffer(packet);
    assert(s.send_msg());

    char buf[256];
    ssize_t n = recv(sv[1], buf, sizeof(buf), 0);
    assert(n == static_cast<ssize_t>(packet.size()));

    // 接收端解析发送端产物，形成闭环
    receiver r(efd, -1);
    r.append_data(std::string(buf, static_cast<size_t>(n)));
    Standard_Message msg = r.process_recv_data("");
    assert(msg.is_valid);
    assert(msg.json_part == json_text);
    assert(msg.file_part == file_data);

    close(sv[0]);
    close(sv[1]);
    close(efd);
    std::cout << "[ok] sender/receiver round trip" << std::endl;
}

}  // namespace

int main() {
    test_full_frame();
    test_half_packet();
    test_sticky_packets();
    test_file_frame();
    test_invalid_length_is_dropped();
    test_sender_length_matches_packet();
    std::cout << "all tcp protocol tests passed" << std::endl;
    return 0;
}

// g++ -std=c++17 -Isrc/net_core -Iinclude -Isrc/net_common -Isrc/logic
//     -Isrc/utils -Isrc/db/repo_interface -Isrc/db/repo -Isrc/db/mysql
//     -I/usr/include/mysql-cppconn
//     tests/test_tcp_protocol.cpp src/net_core/receiver_sender.cpp -pthread
//     -o /tmp/test_tcp_protocol
