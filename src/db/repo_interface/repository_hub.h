#pragma once
#include <memory>
#include "account_repository.h"
#include "friend_repository.h"
#include "group_repository.h"
#include "message_repository.h"

// ============================================================
// RepositoryHub：仓储门面（Facade / 组合根）
// 定位：聚合所有仓储接口，业务层（client_session / social_module ...）只依赖
//       这【一个】门面，避免"每张表一个 repo 成员"导致的 client_session 成员
//       爆炸（上帝类 / God Object）。
// 取用方式：repo_hub_->accounts() 等，按需取用；未来新增仓储只需在 hub 内
//       加一个成员 + 访问器，上层（client_session）不需要再改。
// 说明：
//   - 本层仍属于 repo_interface 契约层：只持有抽象接口，不出现 SQL / mysql 头。
//   - 具体实现（account_repo 等）由组合根装配；构造时默认注入
//     真实 MySQL 实现（见 repository_hub.cpp），上层无感知。
// ============================================================

class RepositoryHub {
public:
    RepositoryHub();

    // 账户仓储（接口），默认已装配 repo 层真实 MySQL 实现（见 repository_hub.cpp）
    std::shared_ptr<I_account_repo> accounts() const { return account_repo_; }
    // 允许运行时替换实现（便于测试注入 mock / 未来切换数据源），业务层默认无需调用
    void set_account_repo(std::shared_ptr<I_account_repo> repo) { account_repo_ = std::move(repo); }

    // 好友仓储（接口），默认已装配 repo 层真实 MySQL 实现（见 repository_hub.cpp）
    std::shared_ptr<I_friend_repo> friends() const { return friend_repo_; }
    // 允许运行时替换实现（便于测试注入 mock / 未来切换数据源），业务层默认无需调用
    void set_friend_repo(std::shared_ptr<I_friend_repo> repo) { friend_repo_ = std::move(repo); }

    // 群聊仓储（接口），默认已装配 repo 层真实 MySQL 实现（见 repository_hub.cpp）
    std::shared_ptr<I_group_repo> groups() const { return group_repo_; }
    // 允许运行时替换实现（便于测试注入 mock / 未来切换数据源），业务层默认无需调用
    void set_group_repo(std::shared_ptr<I_group_repo> repo) { group_repo_ = std::move(repo); }
    // ---- 未来仓储（接口定义就绪后在此扩展，方法同账户）----    
    // std::shared_ptr<I_message_repo> messages() const { return message_repo_; }
    // void set_message_repo(std::shared_ptr<I_message_repo> repo) { message_repo_ = std::move(repo); }

private:
    std::shared_ptr<I_account_repo> account_repo_;          // 账户仓储（构造时装配真实 MySQL 实现）
    std::shared_ptr<I_friend_repo>  friend_repo_;           // 好友仓储（构造时装配真实 MySQL 实现）
    std::shared_ptr<I_group_repo>    group_repo_;
    // std::shared_ptr<I_message_repo>  message_repo_;
};

