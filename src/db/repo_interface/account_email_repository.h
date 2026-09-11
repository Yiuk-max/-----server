#pragma once
#include <string>

// ============================================================
// 账户邮箱仓储接口层（repo_interface）
// 定位：只定义"邮箱 <-> 账户UID"绑定关系的契约（抽象接口），不含 SQL。
//
// 分层约定：
//   - 本文件只声明接口契约，禁止出现 SQL 与 mysql 头。
//   - 真正实现位于 repo 层 class account_email_repo（见 repo/account_email_repo.h）。
//   - 由 RepositoryHub 组合根装配，业务层经 repo_hub_->emails() 调用。
//
// 表结构约定（见 sql/account_email.sql）：
//   - account_email(email PK, UID UNIQUE)：一个账户一个邮箱，邮箱唯一。
// ============================================================
class I_account_email_repo {
public:
    virtual ~I_account_email_repo() = default;

    // 绑定 / 换绑邮箱（一个 UID 一个邮箱，事务内先删旧再插新）。
    // 成功返回 true；邮箱已被【其他】UID 占用或 DB 失败返回 false。
    virtual bool set_email(int uid, const std::string& email) = 0;

    // 按邮箱查 UID；不存在返回 -1。
    virtual int find_uid_by_email(const std::string& email) = 0;

    // 查 UID 绑定的邮箱；未绑定返回空串。
    virtual std::string get_email(int uid) = 0;
};
