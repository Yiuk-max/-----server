#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>// close
#include <sys/socket.h>// socket相关
#include <sys/epoll.h>// 添加epoll头文件
#include <fcntl.h>// 添加fcntl头文件 一些epoll相关的函数需要用到的宏
#include <nlohmann/json.hpp>// JSON库
#include <netinet/in.h>// sockaddr_in6
#include <thread>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <memory>//智能指针
#include <condition_variable>//信号量
#include <chrono>
#include "user.h"
extern bool running;
using json = nlohmann::json;
class group;
class handle_msg;
extern int epoll_fd;
    inline std::mutex client_mutex;
    inline std::vector<int> activate_clients;
    inline std::unordered_map<std::string, int> username_to_fd;
    inline std::unordered_map<int, user> users;
    inline std::unordered_map<std::string,std::shared_ptr<group>> group_list;
    inline std::unordered_map<std::string,std::shared_ptr<handle_msg>> handle_msg_list;
