#pragma once
#include <iostream>
#include <string>

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
class social_relation_manager
{
private:
    std::vector<friend_info::friend_relation> friend_relations;
    std::vector<friend_info::friend_group> friend_groups; 
    std::vector<friend_info::friend_request> friend_requests; 
    int user_UID; // 当前用户的UID
public:
    social_relation_manager(int UID):user_UID(UID){}
    void add_friend(int friend_UID);
    void remove_friend(int friend_UID);
    void create_friend_group(int user_UID, std::string group_name);
    void exit_friend_group(int user_UID, std::string group_name);
    void send_friend_request(int sender_UID, int receiver_UID, std::string apply_message);
    void handle_friend_request(int receiver_UID, int sender_UID, bool accept);
    void show_friends(int user_UID);
};