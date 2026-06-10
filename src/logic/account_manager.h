#pragma once
#include <iostream>
#include <string>
#include "account.h"
#include "UID_allocator.h"
class account_manager{
    private:
        account_manager() = default; // 私有构造函数，禁止外部实例化
        account_manager(const account_manager &) = delete;            // 禁止拷贝构造
        account_manager &operator=(const account_manager &) = delete; // 禁止拷贝赋值
        account_manager(account_manager &&) = delete;                 // 禁止移动构造
        account_manager &operator=(account_manager &&) = delete;      // 禁止移动赋值

        std::mutex accounts_mutex; // 保护accounts_的互斥锁
        std::unordered_map<int, std::shared_ptr<account>> accounts_; // key: UID, value: user对象


    public:
        static account_manager &get_instance()
        {
            static account_manager instance;
            return instance;
        }
        void register_account(const std::string& name, const std::string& password);
        void remove_account(int UID);
        std::shared_ptr<account> find_account(int UID);
};