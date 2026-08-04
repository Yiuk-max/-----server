#pragma once
// ============================================================
// 全局公共头：仅存放不依赖具体业务/网络的通用声明与类型别名。
// 原则：本文件不 include 任何业务头(account/group/social等)，
//       也不 include 任何数据库驱动头，避免造成循环依赖与全项目污染。
// 各源文件请按需自行 include 自己真正需要的头文件。
// ============================================================

// ---- 标准库 ----
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <chrono>
#include <thread>
#include <fstream>
#include <cstdint>

// ---- 系统库 ----
#include <unistd.h>                     // close
#include <sys/socket.h>                 // socket
#include <sys/epoll.h>                  // epoll
#include <fcntl.h>                      // fcntl
#include <netinet/in.h>                 // sockaddr_in6
#include <arpa/inet.h>                  // inet_ntop

// ---- JSON ----
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// ---- 全局常量 ----
// 文件保存路径（当前写死，后续可由配置项读取）
#ifndef SERVER_SAVING_PATH
#define SERVER_SAVING_PATH "./received_files/"
#endif
// 文件分块大小：256KB
#ifndef SEND_CHUNK_SIZE
#define SEND_CHUNK_SIZE (256 * 1024)
#endif

// ---- 全局运行标志 ----
extern bool running;

// ---- 前置声明（避免相互 include 时造成不完整类型问题）----
class group;
class client_session;
