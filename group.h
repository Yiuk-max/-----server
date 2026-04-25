#pragma once
#include "total.h"
class group{
    private:
        std::vector<int> group_clients;
        std::unordered_map<std::string,int> client_name_group;
        int manager_fd_;
    public:
        group()=default;
        group(int manager):manager_fd_(manager){
            group_clients.push_back(manager_fd_);
            // 补充：将创建者用户名加入组映射（需依赖total.h的users全局变量）
            auto it = users.find(manager_fd_);
            if (it != users.end()) {
                client_name_group[it->second.getName()] = manager_fd_;
            }
        }
        bool add_client(std::string name);
        bool delete_client(std::string name);
        void group_spk(std::string message);
        // 拷贝构造
        group(const group& other)
            : group_clients(other.group_clients),
            client_name_group(other.client_name_group),
            manager_fd_(other.manager_fd_) {}

        // 移动构造
        group(group&& other) noexcept
            : group_clients(std::move(other.group_clients)),
            client_name_group(std::move(other.client_name_group)),
            manager_fd_(other.manager_fd_) {}

        // 拷贝赋值
        group& operator=(const group& other) {
            if (this != &other) {
                group_clients = other.group_clients;
                client_name_group = other.client_name_group;
                manager_fd_ = other.manager_fd_;
            }
            return *this;
        }

        // 移动赋值
        group& operator=(group&& other) noexcept {
            if (this != &other) {
                group_clients = std::move(other.group_clients);
                client_name_group = std::move(other.client_name_group);
                manager_fd_ = other.manager_fd_;
            }
            return *this;
        }

};
