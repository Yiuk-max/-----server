#pragma once
#include <memory>
#include <unordered_map>
#include <mutex>

class social_module;

class social_manager{
    private:
        social_manager() = default; // 私有构造函数，禁止外部实例化
        social_manager(const social_manager &) = delete;            // 禁止拷贝构造
        social_manager &operator=(const social_manager &) = delete; // 禁止拷贝赋值
        social_manager(social_manager &&) = delete;                 // 禁止移动构造
        social_manager &operator=(social_manager &&) = delete;      // 禁止移动赋值

        std::mutex social_mutex; // 保护互斥锁
        std::unordered_map<int, std::shared_ptr<social_module>> social_module_manager_; 

    public:
        static social_manager &get_instance()
        {
            static social_manager instance;
            return instance;
        }
        void add_social_module(int UID);
        void remove_social_module(int UID);
        std::shared_ptr<social_module> find_relation_manager(int UID);
};