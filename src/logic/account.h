#pragma once
#include <iostream>
#include <string>

// ============================================================
// 账户数据模型（纯数据 holder，无 DB / SQL 依赖）
// 定位：逻辑层只有这一个 account 类，既是业务层持有/展示的数据模型，
//       也是 db/repo 层 register/load/update 时使用的载体。
// 分层约定：
//   - 本类【不含任何 SQL / 不持有仓库】。数据库读写由 repo 层完成，
//     本类只提供存取器（getter/setter）供 repo 填充/业务层读取。
//   - 结构体字段与 sql/create_table.sql 中 Account 表【严格对齐】：
//         UID / password / nickname / settings(JSON) / language / ...
//   - settings.theme 等设置项序列化进 Account.settings JSON 列，
//     language 另有一独立列（便于索引/查询），二者都从这里读写。
// ============================================================
namespace account_info
{
    // 账户设置项
    struct account_settings
    {
        std::string theme    = "white";     // 主题（默认 white，存于 settings JSON）
        std::string language = "Chinese";   // 语言（Account.language 列 / settings JSON）
        std::string last_login_time;        // 上次登录时间（存于 settings JSON，登录/注册时更新）
        // 后续可扩展：通知开关、头像等，均作为 settings JSON 的额外键
    };

    // 账户基础信息（主要列），与 Account 表一一对齐
    struct account_base_info
    {
        std::string name;                 // nickname
        std::string password;             // password
        int         UID;                  // UID（系统分配，AUTO_INCREMENT）
        std::string settings_json;        // Account.settings 原始 JSON（回写时用）

    };
}

class account
{
private:
    account_info::account_settings settings;
    account_info::account_base_info base_info;

public:
    // 无参构造：仅用于“占位/默认”账户，写入安全默认值（UID=-1 表示无效）。
    account();
    // 带参构造：注册/加载时由 repo 传入 UID/nickname/password，
    //           其余 settings/language 由 repo 通过 setter 额外填充（见 load_account_info）。
    account(int uid, const std::string& name, const std::string& password);

    // ---- 加载：由 repo 在 SELECT 后显式调用，把 settings / language 等填入本对象 ----
    void load_account_info();   // 历史接口保留：现交给 repo 层在构造后通过 setter 填充数据

    // ---- 存取器 ----
    std::string getName();
    void setName(const std::string& name);
    std::string get_string_UID();
    bool passwd_check(std::string password);
    std::string passwd_raw();                       // 返回密码原值（供 repo 持久化/校验）
    void set_password(const std::string& password); // 修改密码
    int getUID() const;

    // 设置项存取：theme / language（供 repo 填充，业务层读取）
    std::string get_theme()    const;
    void set_theme(const std::string& theme);
    std::string get_language() const;
    void set_language(const std::string& language);
    // 上次登录时间（存于 settings JSON，登录/注册时由外层写入；失败时为空串）
    std::string get_last_login_time() const;
    void set_last_login_time(const std::string& t);
    // 原始 settings JSON（供 repo 落库时使用）
    std::string get_settings_json() const;
    void set_settings_json(const std::string& json);

    std::string get_info();     // 展示用：昵称:补零UID
};

