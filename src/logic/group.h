#pragma once
#include "total.h"
class client_session;

class group{
    private:
        std::vector<int> member_UID_list;
        std::unordered_map<std::string,int> client_name_group;
        int manager_UID_;
        std::mutex group_mutex_;
        std::string group_name_;

        int UID;//群聊唯一标识
    public:
        group()=default;
        group(int manager,std::string name):manager_UID_(manager),group_name_(name){
            member_UID_list.push_back(manager_UID_);
            UID = UID_allocator::get_instance().request_group_id();
            Group_manager::get_instance().create_group(UID, std::make_unique<group>(*this));
        }
        bool add_client(int UID);
        bool delete_client(int UID);
        void group_spk(std::string message);
        bool is_manager_(int UID);
        bool modify_group_name(int renmaer_UID,std::string new_group_name);
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
