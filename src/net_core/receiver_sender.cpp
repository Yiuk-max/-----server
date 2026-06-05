#include "receiver_sender.h"
#include "epoller.h"
#include <arpa/inet.h>
void sender::add_to_out_buffer(const std::string &message)
{
    std::lock_guard<std::mutex> lock(out_mtx);
    bool was_empty = out_buffer.empty();
    out_buffer += message;
    // 仅当缓冲区由空变为非空时注册写事件，减少 epoll_ctl 调用
    if (was_empty)
    {
        struct epoll_event event;
        event.data.fd = client_fd_;
        event.events = EPOLLIN | EPOLLOUT | EPOLLET; // 同时监听读和写事件
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd_, &event);
    }
}
void sender::send_msg()
{
    std::lock_guard<std::mutex> lock(out_mtx);
    while (!out_buffer.empty())
    {
        ssize_t bytes_sent = send(client_fd_, out_buffer.c_str(), out_buffer.size(), 0);
        if (bytes_sent > 0)
        {
            out_buffer.erase(0, bytes_sent); // 移除已发送的部分
        }
        else if (bytes_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            // Socket缓冲区满了，稍后再试
            break;
        }
        else
        {
            // 发生错误，可能需要关闭连接
            // close_connection();
            break;
        }
    }
    if (out_buffer.empty())
    {
        // 发送完毕，取消写事件的注册
        struct epoll_event event;
        event.data.fd = client_fd_;
        event.events = EPOLLIN | EPOLLET; // 只保留读事件
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd_, &event);
    }
}
void sender::send_file(const std::string &file_name)
{
    std::string file_path = saving_path + file_name; // 构造文件路径
    // 1. 打开文件，算总大小
    std::ifstream file(file_path, std::ios::binary);
    
    file.seekg(0, std::ios::end);  // 指针跳到末尾
    size_t total_size = file.tellg();  // 末尾位置就是文件大小
    file.seekg(0, std::ios::beg);  // 指针跳回开头准备读

    // 2. 算要切几块
    const size_t CHUNK_SIZE = SEND_CHUNK_SIZE;  // 256KB
    size_t chunk_count = (total_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    // 比如文件 700KB：(700*1024 + 256*1024 - 1) / (256*1024) = 3块

    // 3. 生成这次传输的唯一ID（简单版）
    std::string file_id = std::to_string(time(nullptr));

    // 4. 循环切块发送
    for (size_t i = 0; i < chunk_count; i++) {
        size_t offset = i * CHUNK_SIZE;
        // 最后一块可能不足256KB，取实际大小
        size_t this_chunk = std::min(CHUNK_SIZE, total_size - offset);

        // 读这块数据
        std::string buf(this_chunk, '\0');
        file.read(buf.data(), this_chunk);

        // 构造JSON头
        nlohmann::json meta;
        meta["type"]        = "file_chunk";
        meta["file_id"]     = file_id;
        meta["filename"]    = file_name;
        meta["total_size"]  = total_size;
        meta["chunk_index"] = i;
        meta["chunk_count"] = chunk_count;
        meta["chunk_size"]  = this_chunk;
        meta["offset"]      = offset;
        
        process_file_data(meta, buf);
    }
}
void sender::process_file_data(json &msg_json, std::string &data)
{
    // 构造出标准消息结构体，以便发送
    Standard_Message result;
    uint32_t net_total_len = htonl(8 + msg_json.dump().size() + data.size());
    uint32_t net_json_len = htonl(msg_json.dump().size());

    std::string packet;
    packet.append(reinterpret_cast<const char*>(&net_total_len), 4);
    packet.append(reinterpret_cast<const char*>(&net_json_len), 4);
    packet += msg_json.dump();
    packet += data; // 文件数据部分

    add_to_out_buffer(packet);
}
Standard_Message receiver::process_recv_data(std::string raw_message)
{
    in_buffer += raw_message;
    Standard_Message result;
    
    // 需要包头：4字节包头总长度 + 4字节JSON长度
    while (in_buffer.size() >= 8)
    {
        // 解析1：包头总长度
        uint32_t net_total_len;
        std::memcpy(&net_total_len, in_buffer.c_str(), sizeof(net_total_len));
        uint32_t total_len = ntohl(net_total_len);
        
        if (in_buffer.size() < 4 + total_len)
        {
            // 包体未完全接收，等待更多数据
            break;
        }
        
        // 解析2：JSON长度
        uint32_t net_json_len;
        std::memcpy(&net_json_len, in_buffer.c_str() + 4, sizeof(net_json_len));
        uint32_t json_len = ntohl(net_json_len);
        
        // 验证长度合法性
        if (json_len > total_len - 4)
        {
            // 数据错误，跳过这个包
            in_buffer.erase(0, 4 + total_len);
            break;
        }
        
        // 提取JSON部分
        result.json_part = in_buffer.substr(8, json_len);
        
        // 提取文件部分 (如果有)
        uint32_t file_len = total_len - 4 - json_len;
        if (file_len > 0)
        {
            result.file_part = in_buffer.substr(8 + json_len, file_len);
        }
        
        result.is_valid = true;
        
        // 移除已处理的部分
        in_buffer.erase(0, 4 + total_len);
        
        return result;
    }
    
    return result; // is_valid = false
}
void receiver::handle_file(const json &meta, const std::string &data)
{
    // 这里可以根据meta中的信息（如file_id、chunk_index等）来处理文件数据
    std::cout << "Received file chunk: " << meta.dump() << ", data size: " << data.size() << std::endl;
    std::string file_id    = meta["file_id"];
    std::string filename   = meta["filename"];
    size_t      offset     = meta["offset"];
    size_t      chunk_size = meta["chunk_size"];
    size_t      chunk_count = meta["chunk_count"];
    size_t      total_size  = meta["total_size"];

    // 1. 第一块到达时，初始化文件
    if (transfers.find(file_id) == transfers.end()) {
        TransferContext ctx;
        ctx.filename       = filename;
        ctx.total_size     = total_size;
        ctx.chunk_count    = chunk_count;
        ctx.received_count = 0;

        // 预分配文件大小
        // 先跳到最后一个字节位置写一个0，文件就有了完整大小
        std::string save_path = saving_path + filename + "_" + file_id; // 避免重名
        ctx.file.open(save_path, std::ios::binary | std::ios::out | std::ios::in);
        if (!ctx.file.is_open()) {
            // 文件不存在则先创建
            std::ofstream tmp(save_path, std::ios::binary);
            tmp.close();
            ctx.file.open(save_path, std::ios::binary | std::ios::out | std::ios::in);
        }
        ctx.file.seekp(total_size - 1);
        ctx.file.put('\0');  // 预占空间

        transfers[file_id] = std::move(ctx);
    }

    auto& ctx = transfers[file_id];

    // 2. 跳到 offset 位置写入这块数据
    ctx.file.seekp(offset);
    ctx.file.write(data.data(), data.size());
    ctx.received_count++;

    // 3. 所有块都到了，收尾
    if (ctx.received_count == ctx.chunk_count) {
        ctx.file.close();
        transfers.erase(file_id);
        
        std::cout << "文件接收完成: " << filename << std::endl;
    }
}