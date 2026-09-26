#pragma once
#include <memory>
#include <string>
#include <tuple>
#include <vector>

// ============================================================
// 社区/频道/社区成员 仓储契约（纯接口，无 SQL）。
// 数据模型见 sql/community.sql：
//   community        = 社区（is_core=1 核心频道）+ 普通频道（is_core=0）
//   community_member = 社区成员（挂在核心频道上，一份共享给所有频道）
// ============================================================

// 社区信息（核心频道）
struct community_info {
    int         community_id = -1;
    std::string name;
    std::string description;
    std::string avatar;     // file_id 字符串，旧字段保留
    std::string category;   // 预留：社区所属分区
    int         owner_uid = -1;
    std::string my_role;    // 当前用户在社区的 role（owner/admin/member，调用方按需填充）
    int         avatar_id = -1;  // 社区头像 file.id
    int         banner_id = -1;  // 社区背景图 file.id
};

// 普通频道信息
struct channel_info {
    int         channel_id   = -1;
    int         community_id = -1;
    std::string name;
    std::string category;   // 频道分区
};

// 社区成员信息
struct community_member_info {
    int         user_uid = -1;
    std::string nickname;   // 社区昵称（空表示用全局昵称）
    std::string role;       // owner/admin/member
};

class I_community_repo {
public:
    virtual ~I_community_repo() = default;

    // ==================== 社区（核心频道） ====================
    // 建社区：插入核心频道 + 创建者入 community_member(owner)。成功返回 community_id，失败 -1。
    virtual int  create_community(int owner_uid, const std::string& name,
                                  const std::string& description) = 0;
    // 删社区：先删频道消息，再删核心频道（普通频道由外键级联删）。
    virtual bool delete_community(int community_id, int requester_uid) = 0;
    virtual bool modify_community(int community_id, int requester_uid,
                                  const std::string& name,
                                  const std::string& description,
                                  int avatar_id, int banner_id) = 0;

    // ==================== 查询 ====================
    virtual bool is_community_member(int community_id, int user_uid) = 0;
    // 我加入的社区 id 列表（核心频道 id）
    virtual std::vector<int> get_user_communities(int user_uid) = 0;
    // 我加入的社区信息列表（含 my_role，供 show_communities）
    virtual bool get_user_communities_info(int user_uid,
                                           std::vector<community_info>& out) = 0;
    virtual bool get_community_info(int community_id, community_info& out) = 0;
    // 某社区下的普通频道列表
    virtual bool get_community_channels(int community_id,
                                        std::vector<channel_info>& out) = 0;
    // 我可见的普通频道 id 列表（供 social_module 加载）
    virtual std::vector<int> get_user_channels(int user_uid) = 0;
    virtual bool get_community_members(int community_id,
                                       std::vector<community_member_info>& out) = 0;

    // ==================== 频道 ====================
    // 建普通频道：成功返回频道 id，失败 -1。普通频道不写成员表。
    virtual int  create_channel(int community_id, int requester_uid,
                                const std::string& name,
                                const std::string& category) = 0;
    virtual bool delete_channel(int community_id, int channel_id, int requester_uid) = 0;
    virtual bool modify_channel(int community_id, int channel_id, int requester_uid,
                                const std::string& name,
                                const std::string& category) = 0;
    // 查频道所属社区 id；频道不存在返回 false
    virtual bool get_channel_community(int channel_id, int& out_community_id) = 0;

    // ==================== 成员 ====================
    virtual bool add_member(int community_id, int requester_uid, int target_uid) = 0;
    virtual bool remove_member(int community_id, int requester_uid, int target_uid) = 0;
    virtual bool leave(int community_id, int user_uid) = 0;
    // 申请入社区：落库 relation_apply（apply_type=3, status=0）
    virtual bool send_join_request(int community_id, int requester_uid,
                                   const std::string& message) = 0;
    virtual bool handle_join_request(int community_id, int requester_uid,
                                     int target_uid, bool accept) = 0;
    virtual bool modify_member_role(int community_id, int requester_uid,
                                    int target_uid, bool promote) = 0;
    virtual bool show_requests(int community_id, int requester_uid,
                               std::vector<std::tuple<int, std::string>>& out) = 0;

    // ==================== 频道成员（供发言/删除广播） ====================
    // 频道的成员 = 其所属核心频道的 community_member（一份共享）。
    virtual std::vector<int> get_channel_members(int channel_id) = 0;
};
