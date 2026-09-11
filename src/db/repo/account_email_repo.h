#pragma once
// ============================================================
// 账户邮箱仓储 —— 真实的 MySQL 实现（repo 层，唯一允许 SQL 的目录）
// 定位：实现 I_account_email_repo，访问 account_email 表。
// ============================================================
#include "account_email_repository.h"

class account_email_repo : public I_account_email_repo {
public:
    bool set_email(int uid, const std::string& email) override;
    int find_uid_by_email(const std::string& email) override;
    std::string get_email(int uid) override;
};
