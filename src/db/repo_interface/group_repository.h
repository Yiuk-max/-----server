#pragma once
#include <memory>
#include <string>
#include <vector>
#include "group.h"
//创建群聊、删除群聊、群聊改名、群聊删除成员
//查找群聊、查找群聊内所有群成员、查找某用户已加入群聊
//修改群成员身份
//群内成员邀请外人加入群聊(利用relation_apply表)、外人申请加入群聊(群主/管理员审核)

class I_group_repo {
public:
    virtual ~I_group_repo() = default; 
    
    virtual std::shared_ptr<group> create_group(int owner_uid, const std::string& group_name) = 0;

    virtual bool delete_group(int group_uid, int requester_uid) = 0;

    virtual bool modify_group_name(int group_uid, int requester_uid, const std::string& new_group_name) = 0;

    virtual std::shared_ptr<group> load_group(int group_uid) = 0;

    virtual std::vector<int> get_group_members(int group_uid) = 0;
    virtual std::string show_group_members(int group_uid) = 0;

    virtual std::vector<int> get_user_groups(int user_uid) = 0;
    virtual std::string show_user_groups(int user_uid) = 0;

    virtual bool member_add_group(int group_uid, int requester_uid, int new_member_uid) = 0;

    // 踢出群成员：requester_uid 为群主/管理员，把 target_uid 移出 group_uid。
    // 成功返回 true（若本就不在群中也视为成功）。
    virtual bool remove_group_member(int group_uid, int requester_uid, int target_uid) = 0;

    // 申请加入群聊：落库 relation_apply（apply_type=2, status=0 等待），
    // 由群主/管理员调用 handle_join_request 处理。成功返回 true。
    virtual bool send_join_group(int group_uid, int requester_uid) = 0;

    // 处理入群申请（与 send_join_group 对应）：requester_uid 为群主/管理员，
    // 处理 target_uid 对 group_uid 的申请；accept=true 同意（更新状态+拉人入群）
    // / false 拒绝（仅更新状态）。成功返回 true。
    virtual bool handle_join_request(int group_uid, int requester_uid, int target_uid, bool accept) = 0;

    virtual bool modify_member_role(int group_uid, int requester_uid, int target_uid, bool promote) = 0;


};