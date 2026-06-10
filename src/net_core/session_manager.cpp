#include "session_manager.h"

void session_manager::add_session(int client_fd, std::shared_ptr<client_session> session) {
    std::lock_guard<std::mutex> lock(sessions_mutex);
    sessions_[client_fd] = std::move(session);
}
void session_manager::remove_session(int client_fd) {
    std::lock_guard<std::mutex> lock(sessions_mutex);
    sessions_.erase(client_fd);
}
std::shared_ptr<client_session> session_manager::find_session(int client_fd) {
    std::lock_guard<std::mutex> lock(sessions_mutex);
    auto it = sessions_.find(client_fd);
    if (it != sessions_.end()) {    
        return it->second;
    }
    return nullptr; 
}