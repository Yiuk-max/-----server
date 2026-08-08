#pragma once
#include <memory>
#include <string>
#include "account.h"

// ============================================================
// 账户仓储接口层（repo_interface）
// 定位：定义"账户增删改查"的契约，未来由 repo 层 MySQL 实现。
// 当前提供空实现占位（account_repo_placeholder），用于迁移：
//   先把调用点从 account_manager 迁到本接口，暂不真正写数据库；
//   待接入 MySQL 后用真实实现替换占位类，上层代码无需改动。
//
// 硬性约定：
//   - 本层只声明接口 / 空实现占位，禁止出现任何 SQL 与 mysql 头。
//   - 业务层（net_core / logic）只 include 本文件，不 include repo。
// ============================================================

class I_account_repo {
public:
    virtual ~I_account_repo() = default;

    // 注册：新建账户并返回账户对象。
    // 迁移占位：返回 nullptr；接入 MySQL 后由数据库分配 UID 并落库。
    virtual std::shared_ptr<account> register_account(const std::string& name,
                                                      const std::string& password) = 0;

    // 注销：删除指定账户。
    // 迁移占位：暂无操作；接入 MySQL 后执行 DELETE。
    virtual void remove_account(int uid) = 0;

    // 加载：按 UID 查询账户。
    // 用于登录加载自己 / social 查他人资料。
    // 迁移占位：返回 nullptr；接入 MySQL 后 SELECT Account WHERE UID=?。
    virtual std::shared_ptr<account> load_account(int uid) = 0;

    // 修改：更新账户个人数据（改名 / 改设置等）。
    // 迁移占位：暂无操作；接入 MySQL 后 UPDATE。
    virtual void update_account(const std::shared_ptr<account>& acc) = 0;
};

// 空实现占位类：只留函数壳子，不访问数据库。
// 接入 MySQL 后应新建真实实现并将其替换到此（注入位置见 client_session）。
class account_repo_placeholder : public I_account_repo {
public:
    std::shared_ptr<account> register_account(const std::string& name,
                                              const std::string& password) override {
        (void)name; (void)password;
        return nullptr; // TODO: 接入 MySQL 后实现 INSERT Account
    }
    void remove_account(int uid) override {
        (void)uid; // TODO: 接入 MySQL 后实现 DELETE Account
    }
    std::shared_ptr<account> load_account(int uid) override {
        (void)uid;
        return nullptr; // TODO: 接入 MySQL 后实现 SELECT Account WHERE UID=?
    }
    void update_account(const std::shared_ptr<account>& acc) override {
        (void)acc; // TODO: 接入 MySQL 后实现 UPDATE Account
    }
};

