#include "UID_allocator.h"
int UID_allocator::request_uid() {
    std::lock_guard<std::mutex> lock(uid_mutex);
    return current_uid++;
}
std::string UID_allocator::get_string_UID(int uid) {
    std::lock_guard<std::mutex> lock(uid_mutex);
    std::string str_uid = std::to_string(uid);
    // 补零到uid_length位
    while (str_uid.length() < uid_length) {
        str_uid = "0" + str_uid;
    }
    return str_uid;
}
int UID_allocator::request_group_id() {
    std::lock_guard<std::mutex> lock(uid_mutex);
    return current_group_id++;
}
int UID_allocator::request_file_id() {
    std::lock_guard<std::mutex> lock(uid_mutex);
    return current_file_id++;
}