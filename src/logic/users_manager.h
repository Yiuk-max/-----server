#pragma once
#include <iostream>
#include <string>
#include "user.h"

class users_manager{
    private:
        users_manager() = default; // 私有构造函数，禁止外部实例化
        users_manager(const users_manager &) = delete;            // 禁止拷贝构造
        users_manager &operator=(const users_manager &) = delete; // 禁止拷贝赋值
        users_manager(users_manager &&) = delete;                 // 禁止移动构造
        users_manager &operator=(users_manager &&) = delete;      // 禁止移动赋值

        std::mutex users_mutex; // 保护users的互斥锁
        std::unordered_map<int, std::shared_ptr<user>> users; // key: client_fd, value: user对象
    public:
        static users_manager &get_instance()
        {
            static users_manager instance;
            return instance;
        }
        void add_user(int client_fd, const std::string& name, const std::string& password);
        void remove_user(int client_fd);
        std::shared_ptr<user> find_user(int client_fd);
};