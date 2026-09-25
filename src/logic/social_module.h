#pragma once
#include <iostream>
#include <string>
#include "repository_hub.h"
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
    std::vector<int> friend_relations;           // 好友UID列表（登录时从 DB 加载）
    std::vector<int> friend_groups;              // 群组UID列表（登录时从 DB 加载）
    std::vector<int> communities;                // 社区id列表（登录时从 DB 加载）
    std::vector<int> channels;                   // 社区频道id列表（登录时从 DB 加载）
    int user_UID_; // 当前用户的UID
    std::string name_; // 当前用户的昵称
    std::shared_ptr<RepositoryHub> repo_hub_;  // 仓储门面（构造注入，按需从 hub 取各 repo）
public:
    social_module(int UID, std::shared_ptr<RepositoryHub> hub);   // 构造注入 RepositoryHub（由 client_session 传入）
    ~social_module();                                              // 释放该用户持有的群聊内存引用
    void add_friend(int friend_UID);
    bool accept_friend_request(int sender_UID);
    bool remove_friend(int friend_UID);

    // 仅更新内存中的好友列表（供对方在线时同步使用；不写库、不发通知）
    void add_friend_to_list(int friend_UID);
    void remove_friend_from_list(int friend_UID);

    void create_friend_group(std::string group_name, int& out_group_uid);
    void exit_friend_group(int group_UID);
    bool has_group(int group_UID) const;   // 当前用户是否属于该群（内存群列表）
    bool has_channel(int channel_uid) const; // 当前用户是否可见该社区频道（内存频道列表）
    // 仅更新内存中的群列表（供被拉入群/申请通过后同步；不写库）
    void add_group_to_list(int group_UID);
    // 仅从内存群列表移除（供被踢出群后同步；不写库）
    void remove_group_from_list(int group_UID);
    // 社区/频道内存列表同步（供被拉入/踢出/加入社区后同步；不写库）
    void add_community_to_list(int community_id);
    void remove_community_from_list(int community_id);
    void add_channel_to_list(int channel_id);
    void remove_channel_from_list(int channel_id);
    // 重新从 DB 拉取社区/频道列表（供社区成员关系变化后刷新）
    void reload_community_state();
    void send_friend_request(const std::string& email, std::string &apply_message);
    bool handle_friend_request(int sender_UID, bool accept);
    std::string show_friend_requests();
    std::string show_friends();
};
