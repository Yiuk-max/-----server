#pragma once
#include "total.h"
std::shared_ptr<group> find_group_by_name(std::string group_name);
class client_session;
class group{
    private:
        std::vector<int> group_clients;
        std::unordered_map<std::string,int> client_name_group;
        int manager_fd_;
        std::mutex group_mutex_;
        std::string group_name_;

        int UID;//群聊唯一标识
    public:
        group()=default;
        group(int manager,std::string name):manager_fd_(manager),group_name_(name){
            group_clients.push_back(manager_fd_);
            UID = UID_allocator::get_instance().request_group_id();
            Group_manager::get_instance().create_group(UID, std::make_unique<group>(*this));
        }
        bool add_client(std::string name);
        bool delete_client(std::string name);
        void group_spk(std::string message);
        bool is_manager_fd(int fd);
        bool modify_group_name(int renmaer_fd,std::string new_group_name);
        // 拷贝构造
        group(const group& other)
            : group_clients(other.group_clients),
            client_name_group(other.client_name_group),
            manager_fd_(other.manager_fd_),
            UID(other.UID) {}

        // 移动构造
        group(group&& other) noexcept
            : group_clients(std::move(other.group_clients)),
            client_name_group(std::move(other.client_name_group)),
            manager_fd_(other.manager_fd_),
            UID(other.UID)  {}

        // 拷贝赋值
        group& operator=(const group& other) {
            if (this != &other) {
                group_clients = other.group_clients;
                client_name_group = other.client_name_group;
                manager_fd_ = other.manager_fd_;
                UID = other.UID;
            }
            return *this;
        }

        // 移动赋值
        group& operator=(group&& other) noexcept {
            if (this != &other) {
                group_clients = std::move(other.group_clients);
                client_name_group = std::move(other.client_name_group);
                manager_fd_ = other.manager_fd_;
                UID = other.UID;
            }
            return *this;
        }
};
