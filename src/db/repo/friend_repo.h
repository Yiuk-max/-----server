#pragma once
// ============================================================
// 好友仓储 —— 真实的 MySQL 实现（repo 层，唯一允许 SQL 的目录）
// 定位：实现 repo_interface 层 I_friend_repo 声明的接口，
//       真正访问 friend_relation / relation_apply 两张表，
//       完成好友关系与好友申请的增删改查。
// 约束：
//   - 本文件只是实现，不修改 repo_interface 的接口契约。
//   - 所有 SQL 仅出现在本目录；连接经 MySQL_Conn_Pool 分配/归还。
//   - friend_relation 采用双向同步：建立/删除好友时在事务内双写
//     (A,B) 与 (B,A) 两行，保证双向一致（见 sql/create_table.sql）。
// ============================================================
#include "friend_repository.h"

class friend_repo : public I_friend_repo {
public:
    // ==================== friend_relation 好友关系 ====================

    // 建立好友：事务内双写 (uid_a, uid_b) 与 (uid_b, uid_a)
    bool add_friend(int uid_a, int uid_b) override;

    // 删除好友：事务内同时删除 (uid_a, uid_b) 与 (uid_b, uid_a)
    bool remove_friend(int uid_a, int uid_b) override;

    // 查询某用户的所有好友 UID
    std::vector<int> get_friend_list(int uid) override;

    // 判断 uid_a 与 uid_b 是否已是好友
    bool is_friend(int uid_a, int uid_b) override;

    // 设置某用户对某好友的备注名
    bool set_remark(int uid, int friend_uid, const std::string& remark) override;

    // 查询某用户对某好友的备注名（未设置/不存在返回空串）
    std::string get_remark(int uid, int friend_uid) override;

    // ==================== relation_apply 好友申请（apply_type=1） ====================

    // 发送好友申请（sender -> receiver），status 默认 0（等待）
    bool send_friend_request(int sender_uid, int receiver_uid,
                             const std::string& message) override;

    // 查询发给 user_uid 且 status=0 的所有等待中申请
    bool get_friend_requests(int user_uid,
                             std::vector<std::tuple<int, std::string>>& out) override;

    // 处理申请：accept=true 置 status=1（同意）/ false 置 status=2（拒绝）
    bool handle_friend_request(int sender_uid, int receiver_uid, bool accept) override;
};
