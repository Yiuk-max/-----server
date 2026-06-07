#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>                     // close
#include <sys/socket.h>                 // socket相关
#include <sys/epoll.h>                  // 添加epoll头文件
#include <fcntl.h>                      // 添加fcntl头文件 一些epoll相关的函数需要用到的宏
#include <nlohmann/json.hpp>            // JSON库
#include <netinet/in.h>                 // sockaddr_in6
#include <thread>
#include <mutex>
#include <arpa/inet.h>                  // inet_ntop
#include <unordered_map>
#include <algorithm>
#include <memory>                       //智能指针
#include <condition_variable>           //信号量
#include <chrono>
#include <fstream>                      //文件操作
#include "user.h"
#include "UID_allocator.h"              // UID分配器
#include "group_manager.h"              // 群聊管理器
#include "user_manager.h"               // 用户管理器


#define SEND_CHUNK_SIZE (256*1024) // 256KB
extern bool running;
using json = nlohmann::json;
class group;
class client_session;
const std::string saving_path = "./received_files/"; // 文件保存路径

struct TransferContext {
    std::fstream     file;
    std::string      filename;
    size_t           total_size;
    size_t           chunk_count;
    size_t           received_count;  // 已收到几块
};
// 接收消息解析结果结构体
struct Standard_Message {
    std::string json_part;   // JSON部分
    std::string file_part;   // 文件数据部分
    bool is_valid = false;   // 是否解析成功
};


    inline std::mutex client_mutex;
    inline std::vector<int> activate_clients;
    inline std::unordered_map<std::string, int> username_to_fd;
    inline std::unordered_map<int, std::shared_ptr<user>> users;
    //inline std::unordered_map<std::string,std::shared_ptr<group>> group_list;
    inline std::unordered_map<std::string,std::shared_ptr<client_session>> handle_msg_list;
    inline std::unordered_map<std::string, TransferContext> transfers;// 用 file_id 查对应的传输任务
