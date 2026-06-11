#pragma once
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

// 接收消息解析结果结构体
struct Standard_Message {
    std::string json_part;
    std::string file_part;
    bool is_valid = false;
};

struct TransferContext {
    std::fstream     file;
    std::string      filename;
    size_t           total_size;
    size_t           chunk_count;
    size_t           received_count;  // 已收到几块
};

class sender
{
private:
    int client_fd_;
    int epoll_fd_;
    std::string out_buffer;      // 发送缓冲区
    std::mutex out_mtx;          // 保护缓冲区的锁
public:
    sender(int epoll_fd,int fd):epoll_fd_(epoll_fd),client_fd_(fd){}
    void add_to_out_buffer(const std::string& message);
    void process_file_data(json &msg_json, std::string &data);
    void send_file(const std::string& file_name);
    void send_msg();
};


class receiver
{
private:
    int client_fd_;
    int epoll_fd_;
    std::string in_buffer;       // 接收缓冲区，存储未处理的原始数据
    std::unordered_map<std::string, TransferContext> transfers; // 用 file_id 查对应的传输任务
public:
    receiver(int epoll_fd,int fd):epoll_fd_(epoll_fd),client_fd_(fd){}
    Standard_Message process_recv_data(std::string raw_message);// 处理原始数据，返回解析结果
    void upload_file(const json& meta, const std::string& data);
    void recv_msg(int fd);
};


