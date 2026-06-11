#pragma once
#include <iostream>
#include <string>
#include <mutex>

class UID_allocator {
private:
    int current_uid = 0;
    int current_group_id = 0;
    int current_file_id = 0;
    std::mutex uid_mutex;
    int uid_length = 8; // 定义UID的长度，默认为8位
    int file_id_length = 16; // 定义文件ID的长度，默认为16位
    
    UID_allocator() : current_uid(0), current_group_id(0), current_file_id(0) {} // 私有构造函数，禁止外部实例化
    UID_allocator(const UID_allocator &) = delete;            // 禁止拷贝构造
    UID_allocator &operator=(const UID_allocator &) = delete; // 禁止拷贝赋值
    UID_allocator(UID_allocator &&) = delete;                 // 禁止移动构造
    UID_allocator &operator=(UID_allocator &&) = delete;      // 禁止移动赋值
public:
    static UID_allocator &get_instance()
    {
        static UID_allocator instance;
        return instance;
    }
    int request_uid();
    int request_group_id();
    int request_file_id();
    std::string get_string_UID(int uid);// 将整数UID转换为8字符字符串形式，便于使用
    std::string get_string_file_ID(int file_id);// 将整数File_ID转换为16字符字符串形式，便于使用
};