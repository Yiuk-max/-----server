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
//   - 字段与 sql/create_table.sql 中 Account 表【严格对齐】：
//         UID / password / nickname / settings(JSON) / language / token / avatar_id
//   - settings.theme 等设置项序列化进 Account.settings JSON 列，
//     language 另有一独立列（便于索引/查询），二者都从这里读写。
// ============================================================
class account
{
public:
    // 无参构造：占位/默认账户（UID=-1 表示无效），字段均为安全默认值。
    account() = default;
    // 带参构造：注册/加载时由 repo 传入 UID/nickname/password，
    //           其余 settings/language 等由 repo 通过 setter 额外填充。
    account(int uid, const std::string& name, const std::string& password);

    // ---- 存取器 ----
    std::string getName() const;
    void setName(const std::string& name);
    std::string get_string_UID() const;
    bool passwd_check(const std::string& password) const;
    std::string passwd_raw() const;                       // 返回密码原值（供 repo 持久化/校验）
    int getUID() const;

    // 设置项存取：theme / language
    std::string get_theme()    const;
    void set_theme(const std::string& theme);
    std::string get_language() const;
    void set_language(const std::string& language);

    // 上次登录时间（存于 settings JSON，登录/注册时由外层写入；空串表示从未登录）
    std::string get_last_login_time() const;
    void set_last_login_time(const std::string& t);

    // 原始 settings JSON（供 repo 落库时使用）
    std::string get_settings_json() const;
    void set_settings_json(const std::string& json);

    // 登录令牌（token，自动登录用）
    std::string get_token() const;
    void set_token(const std::string& token);

    // 头像 file.id（Account.avatar_id，默认 -1 表示未设置）
    int  get_avatar_id() const;
    void set_avatar_id(int avatar_id);

private:
    // 与 Account 表主要列一一对应
    int         UID_      = -1;       // UID（-1 表示无效/占位账户）
    std::string name_;                // nickname
    std::string password_;            // password

    // settings JSON 及其常用子项（theme/language/last_login_time）。
    // theme/language/last_login_time 与 settings_json_ 同步维护，repo 层负责序列化/反序列化。
    std::string settings_json_ = "{}";
    std::string theme_         = "default";
    std::string language_      = "Chinese";
    std::string last_login_time_;     // 空串表示从未登录

    std::string token_;               // 登录令牌（自动登录用）
    int         avatar_id_  = -1;     // Account.avatar_id（file.id），-1 未设置
};
