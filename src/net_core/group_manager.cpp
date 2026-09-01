#include "group_manager.h"
#include <iostream>

group_manager& group_manager::get_instance() {
    static group_manager instance;
    return instance;
}

void group_manager::set_group_repo(std::shared_ptr<I_group_repo> repo) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    group_repo_ = std::move(repo);
}

std::shared_ptr<group> group_manager::load(int group_uid) {
    // 1. 先查内存：已加载则引用计数 +1 并复用
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = groups_.find(group_uid);
        if (it != groups_.end()) {
            ++it->second.ref_count;
            return it->second.grp;
        }
    }

    // 2. 未加载：从 DB 加载（取出 repo 副本，避免锁外访问共享指针）
    std::shared_ptr<I_group_repo> repo;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        repo = group_repo_;
    }
    std::shared_ptr<group> grp;
    if (repo) {
        grp = repo->load_group(group_uid);
    }
    if (!grp) {
        return nullptr;
    }

    // 3. 插入内存（并发下可能已被加载，此时丢弃刚加载的、复用已有对象）
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = groups_.find(group_uid);
    if (it != groups_.end()) {
        ++it->second.ref_count;
        return it->second.grp;
    }
    groups_[group_uid] = Entry{grp, 1};
    return grp;
}

void group_manager::unload(int group_uid) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = groups_.find(group_uid);
    if (it == groups_.end()) {
        return;
    }
    if (--it->second.ref_count <= 0) {
        groups_.erase(it); // 无人在内存中持有，结束该群聊的生命周期
    }
}

std::shared_ptr<group> group_manager::get(int group_uid) {
    // 1. 内存优先
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = groups_.find(group_uid);
        if (it != groups_.end()) {
            return it->second.grp;
        }
    }
    // 2. 内存没有：从 DB 加载并返回（不缓存；缓存/生命周期由 load/unload 管理）
    std::shared_ptr<I_group_repo> repo;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        repo = group_repo_;
    }
    if (repo) {
        return repo->load_group(group_uid);
    }
    return nullptr;
}

bool group_manager::find_group(int group_uid) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return groups_.find(group_uid) != groups_.end();
}

void group_manager::load_groups_for_user(const std::vector<int>& group_uids) {
    for (int uid : group_uids) {
        load(uid);
    }
}

void group_manager::unload_groups_for_user(const std::vector<int>& group_uids) {
    for (int uid : group_uids) {
        unload(uid);
    }
}

void group_manager::remove_group(int group_uid) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    groups_.erase(group_uid);
}
