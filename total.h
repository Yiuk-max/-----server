#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <memory>
#include "user.h"
class group;
    inline std::mutex client_mutex;
    inline std::vector<int> activate_clients;
    inline std::unordered_map<std::string, int> username_to_fd;
    inline std::unordered_map<int, user> users;
    inline std::unordered_map<std::string,std::shared_ptr<group>> group_list;
