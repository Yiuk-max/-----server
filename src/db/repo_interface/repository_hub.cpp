#include "repository_hub.h"
#include "account_repo.h"   // 组合根：装配真实的 MySQL 账户仓储（.cpp 实现感知具体 repo，头文件仍只面向接口）
#include "friend_repo.h"    // 组合根：装配真实的 MySQL 好友仓储
#include "group_repo.h"     // 组合根：装配真实的 MySQL 群聊仓储

// 构造函数：默认给账户/好友/群聊仓储装配真实实现（MySQL）。
// 组合根（RepositoryHub）是唯一感知具体 repo 实现的位置；
// 业务层（client_session / social_module）仍只依赖 repo_interface 接口，无需改动。
RepositoryHub::RepositoryHub()
    : account_repo_(std::make_shared<account_repo>()),  // 装配真实 MySQL 实现
      friend_repo_(std::make_shared<friend_repo>()),    // 装配真实 MySQL 实现
      group_repo_(std::make_shared<group_repo>())       // 装配真实 MySQL 实现
{
    // 预留：message_repo_ 待接口定义后在此装配
}
