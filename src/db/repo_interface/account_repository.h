#pragma once
#include <memory>
#include <string>
#include "account.h"

// ============================================================
// 账户仓储接口层（repo_interface）
// 定位：只定义"账户增删改查"的契约（抽象接口 I_account_repo），不包含任何实现。
//
// 分层约定：
//   - 本文件只声明接口契约，禁止出现任何 SQL 与 mysql 头。
//   - 真正的 MySQL 实现位于 repo 层：class account_repo（见 repo/account_repo.h）。
//     由组合根 RepositoryHub 在构造时注入（见 repository_hub.cpp），上层无需改动。
//   - 业务层（net_core/client_session 与 logic/social_module）只 include/依赖
//     本文件（I_account_repo 接口），对具体 MySQL 实现完全无感知。
//
// 调用链路：client_session / social_module
//        --> repo_hub_->accounts()                    （细粒度门面，见 repository_hub.h）
//        --> I_account_repo::register/load/...    （本文件接口契约）
//        --> account_repo::xxx()                  （repo 层真实 MySQL 实现）
// ============================================================

class I_account_repo {
public:
    virtual ~I_account_repo() = default;

    // 注册：新建账户，由数据库 AUTO_INCREMENT 分配 UID 并落库，返回带 UID 的账户对象。
    // 成功返回账户对象；失败（如 nickname 重名 / DB 不可用）返回 nullptr。
    virtual std::shared_ptr<account> register_account(const std::string& name,
                                                      const std::string& password) = 0;

    // 注销：删除指定账户（依赖外键 ON DELETE CASCADE 清理关联数据）。
    virtual void remove_account(int uid) = 0;

    // 加载：按 UID 查询账户（登录加载自己 / social 查他人资料）。
    // 存在返回账户对象，不存在返回 nullptr。
    virtual std::shared_ptr<account> load_account(int uid) = 0;

    // 修改：更新账户个人数据（改名 / 改设置等）。
    // 成功返回 true；失败（如 nickname 违反唯一约束 / 账户不存在 / DB 不可用）返回 false。
    virtual bool update_account(const std::shared_ptr<account>& acc) = 0;
};

// 说明：具体实现 class account_repo 在 repo/account_repo.h（repo 层），
//       由 RepositoryHub 组合根装配，这里不再提供占位实现。

