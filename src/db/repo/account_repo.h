#pragma once
// ============================================================
// 账户仓储 —— 真实的 MySQL 实现（repo 层，唯一允许 SQL 的目录）
// 定位：实现 repo_interface 层 I_account_repo 声明的接口，
//       真正访问 Account 表完成注册/查询/注销/更新。
// 约束：
//   - 本文件只是实现，不修改 repo_interface 的接口契约。
//   - 所有 SQL 仅出现在本目录；连接经 MySQL_Conn_Pool 分配/归还。
//   - UID 由数据库 AUTO_INCREMENT 分配（方案B），INSERT 后取 LAST_INSERT_ID。
// ============================================================
#include "account_repository.h"

class account_repo : public I_account_repo {
public:
    // 注册：INSERT Account，返回携带数据库分配 UID 的新账户对象；
    // 失败（如 nickname 重名 / DB 不可用）返回 nullptr。
    std::shared_ptr<account> register_account(const std::string& name,
                                              const std::string& password) override;

    // 注销：DELETE Account WHERE UID=?
    void remove_account(int uid) override;

    // 加载：SELECT Account WHERE UID=?，填充账户对象；不存在返回 nullptr
    std::shared_ptr<account> load_account(int uid) override;

    // 更新：UPDATE Account 个人资料（改名/设置等）；成功返回 true
    bool update_account(const std::shared_ptr<account>& acc) override;
};