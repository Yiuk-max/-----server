#pragma once
#include <memory>
#include <string>
#include <vector>
#include <tuple>

// ============================================================
// 好友仓储接口层（repo_interface）
// 定位：只定义"好友关系(friend_relation) + 关系申请(relation_apply)"
//       两张表的增删改查契约（抽象接口 I_friend_repo），不包含任何实现。
//
// 分层约定：
//   - 本文件只声明接口契约，禁止出现任何 SQL 与 mysql 头。
//   - 真正的 MySQL 实现位于 repo 层：class friend_repo（见 repo/friend_repo.h）。
//     由组合根 RepositoryHub 在构造时注入（见 repository_hub.cpp），上层只需调用。
//   - 业务层（net_core/client_session 与 logic/social_module）只 include/依赖
//     本文件（I_friend_repo 接口），对具体 MySQL 实现完全无感知。
//
// 表结构约定（见 sql/create_table.sql）：
//   - friend_relation：双向同步 —— 建立/删除好友时在事务内双写 (A,B) 与 (B,A) 两行。
//   - relation_apply ：申请表，apply_type=1 好友申请 / 2 群聊申请；
//     status 0=等待 1=同意 2=拒绝；好友申请同意后由业务层负责落好友关系。
// ============================================================
class I_friend_repo {
public:
    virtual ~I_friend_repo() = default;

    // ==================== friend_relation 好友关系（增删改查） ====================

    // 建立好友：事务内双写 (uid_a, uid_b) 与 (uid_b, uid_a)，保证双向一致。
    // 成功返回 true；任一行插入失败则回滚并返回 false。
    virtual bool add_friend(int uid_a, int uid_b) = 0;

    // 删除好友：事务内同时删除 (uid_a, uid_b) 与 (uid_b, uid_a) 两行。
    // 成功返回 true（若本就不存在也视为成功）。
    virtual bool remove_friend(int uid_a, int uid_b) = 0;

    // 查询某用户的所有好友 UID（SELECT friend_UID WHERE UID=?），返回 UID 列表。
    virtual std::vector<int> get_friend_list(int uid) = 0;

    // 判断 uid_a 与 uid_b 是否已是好友（存在 (A,B) 或 (B,A) 任一行即视为好友）。
    virtual bool is_friend(int uid_a, int uid_b) = 0;

    // 设置某用户对某好友的备注名（UPDATE remark_name WHERE UID=? AND friend_UID=?）。
    virtual bool set_remark(int uid, int friend_uid, const std::string& remark) = 0;

    // ==================== relation_apply 好友申请（apply_type=1） ====================

    // 发送好友申请（sender -> receiver），status 默认 0（等待）。
    // 若同对用户已存在"等待中"申请（唯一约束），返回 false。
    virtual bool send_friend_request(int sender_uid, int receiver_uid,
                                     const std::string& message) = 0;

    // 查询【发给 user_uid】且 status=0 的所有"等待中"申请。
    // out 输出 (sender_UID, message) 列表；成功返回 true。
    virtual bool get_friend_requests(int user_uid,
                                     std::vector<std::tuple<int, std::string>>& out) = 0;

    // 处理申请：sender 申请被 receiver 处理，accept=true 置 status=1（同意）
    // / false 置 status=2（拒绝）。成功返回 true。
    virtual bool handle_friend_request(int sender_uid, int receiver_uid, bool accept) = 0;
};

// 说明：具体实现 class friend_repo 在 repo/friend_repo.h（repo 层），
//       由 RepositoryHub 组合根装配，这里不再提供占位实现。
