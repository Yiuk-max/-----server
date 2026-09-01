#pragma once
// ============================================================
// 群聊生命周期管理器（内存缓存 + 引用计数）
//
// 定位：
//   - 统一管理"已加载到内存"的 group 对象及其生命周期。
//   - 业务层需要群聊信息时【优先】到这里取（get/find_group），
//     避免每次都打数据库；只有未加载时才经 I_group_repo 从 DB 加载。
//   - 用户上线时为其所有群聊 load（已加载则只增加引用计数），
//     用户下线时 unload；某群聊的引用计数归零即从内存移除，
//     结束该群聊的内存生命周期。
//
// 线程安全：内部用 shared_mutex 保护 groups_ 与 group_repo_。
// ============================================================
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "group.h"
#include "group_repository.h"

class group_manager {
public:
    static group_manager& get_instance();

    // 注入群聊仓储（用于从 DB 加载；幂等，可在每次会话建立时设置）
    void set_group_repo(std::shared_ptr<I_group_repo> repo);

    // 加载：已加载则引用计数+1；未加载则从 DB 加载并计数=1。失败返回 nullptr。
    std::shared_ptr<group> load(int group_uid);

    // 释放一次引用：引用计数 -1；仅当计数归零（群聊内无任何用户持有）时才从内存移除，
    // 不影响其它仍持有该群的用户。
    void unload(int group_uid);

    // 获取群聊信息：优先返回内存中的对象；内存没有则从 DB 加载并返回（不改变引用计数）。
    std::shared_ptr<group> get(int group_uid);

    // 判断群聊是否已加载到内存。
    bool find_group(int group_uid);

    // 用户上线：批量加载其所有群聊（已加载跳过，未加载加载）。
    void load_groups_for_user(const std::vector<int>& group_uids);

    // 用户下线：对该用户的每个群各释放一次引用（各减一次计数），
    // 不影响其它在线用户；某群计数归零时才真正卸载。
    void unload_groups_for_user(const std::vector<int>& group_uids);

    // 强制从内存移除（群聊被删除时调用，不论引用计数）。
    void remove_group(int group_uid);

private:
    group_manager() = default;
    group_manager(const group_manager&) = delete;
    group_manager& operator=(const group_manager&) = delete;

    struct Entry {
        std::shared_ptr<group> grp;
        int ref_count = 0;
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<int, Entry> groups_;
    std::shared_ptr<I_group_repo> group_repo_;
};
