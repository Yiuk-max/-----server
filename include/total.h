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
#include <shared_mutex>
#include <arpa/inet.h>                  // inet_ntop
#include <unordered_map>
#include <algorithm>
#include <memory>                       //智能指针
#include <condition_variable>           //信号量
#include <chrono>
#include <fstream>                      //文件操作
#include "account.h"
#include "UID_allocator.h"              // UID分配器
//#include "group_manager.h"              // 群聊管理器

using json = nlohmann::json;

#define SEND_CHUNK_SIZE (256*1024) // 256KB
extern bool running;
class group;
class client_session;
const std::string saving_path = "./received_files/"; // 文件保存路径

