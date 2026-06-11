#pragma once
#include <vector>
#include <string>
#include <mutex>
#include "session_manager.h"
#include "UID_allocator.h"

class client_session;

class group{
    private:
        std::vector<int> member_UID_list;
        int manager_UID_;
        std::mutex group_mutex_;
        std::string group_name_;

        int UID;//群聊唯一标识
        std::string str_UID;
    public:
        group() : manager_UID_(-1), UID(-1) {}
        group(int manager,std::string name,int UID_):manager_UID_(manager),group_name_(name),UID(UID_){
            member_UID_list.push_back(manager_UID_);
            str_UID = UID_allocator::get_instance().get_string_UID(UID_);
        }
        bool add_client(int UID,int sender_UID);
        bool delete_client(int UID,int sender_UID);
        void group_spk(std::string message);
        bool is_manager_(int UID);
        bool modify_group_name(int renmaer_UID,std::string new_group_name);
        std::string get_group_info();
        std::string show_member();
        
};
