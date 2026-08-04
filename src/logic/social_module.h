#pragma once
#include <iostream>
#include <string>
#include "group_manager.h"
#include "UID_allocator.h"
#include "account_manager.h"
class account_manager;
namespace friend_info
{
    struct friend_relation
    {
        int friend_UID;
        std::string remark_name;
        std::string group_name;
    };
    struct friend_group
    {
        std::string group_name;
        std::vector<int> group_UID;
    };
    struct friend_request
    {
        int sender_UID;
        std::string sender_name;
        std::string apply_message;
    };
    
}
class social_module
{
private:
    std::vector<int> friend_relations;           // 好友UID列表
    std::vector<int> friend_groups;              // 群组UID列表
    std::vector<friend_info::friend_request> friend_requests; 
    int user_UID_; // 当前用户的UID
    std::string name_; // 当前用户的昵称
public:
    social_module(int UID);   // 构造函数实现在 social_module.cpp，避免循环 include 导致 account_manager 不完整
    void add_friend(int friend_UID);
    void accept_friend_request(int sender_UID);
    void remove_friend(int friend_UID);

    void create_friend_group(std::string group_name, int& out_group_uid);
    void add_friend_to_group(int friend_UID, int group_UID);
    void exit_friend_group(int group_UID);
    void send_friend_request(int receiver_UID, std::string &apply_message);
    void handle_friend_request(int sender_UID, bool accept);
    std::string show_friends();
    std::string show_group_member(int group_UID);
};