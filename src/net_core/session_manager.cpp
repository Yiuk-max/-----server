#include "session_manager.h"

std::shared_ptr<client_session> session_manager::replace_online(
    int UID, std::shared_ptr<client_session> session) {
    std::lock_guard<std::shared_mutex> lock(sessions_mutex);

    std::shared_ptr<client_session> old_session;
    auto it = sessions_.find(UID);
    if (it != sessions_.end()) {
        old_session = it->second;
        it->second = std::move(session);
    } else {
        sessions_.emplace(UID, std::move(session));
    }
    return old_session;
}

void session_manager::remove_online_if_same(
    int UID, const client_session* expected) {
    if (UID < 0 || expected == nullptr) {
        return;
    }

    std::lock_guard<std::shared_mutex> lock(sessions_mutex);
    auto it = sessions_.find(UID);
    if (it != sessions_.end() && it->second.get() == expected) {
        sessions_.erase(it);
    }
}

std::shared_ptr<client_session> session_manager::find_session(int UID) {
    std::shared_lock<std::shared_mutex> lock(sessions_mutex);
    auto it = sessions_.find(UID);
    if (it != sessions_.end()) {
        return it->second;
    }
    return nullptr;
}
