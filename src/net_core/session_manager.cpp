#include "session_manager.h"

void session_manager::add_session(int UID, std::shared_ptr<client_session> session) {
    std::lock_guard<std::shared_mutex> lock(sessions_mutex);
    sessions_[UID] = std::move(session);
}
void session_manager::remove_session(int UID) {
    std::lock_guard<std::shared_mutex> lock(sessions_mutex);
    sessions_.erase(UID);
}
std::shared_ptr<client_session> session_manager::find_session(int UID) {
    std::shared_lock<std::shared_mutex> lock(sessions_mutex);
    auto it = sessions_.find(UID);
    if (it != sessions_.end()) {    
        return it->second;
    }
    return nullptr; 
}