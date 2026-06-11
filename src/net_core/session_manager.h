#pragma once
#include "total.h"
#include "client_session.h"

class session_manager{
    private:
        session_manager() = default; // 私有构造函数，禁止外部实例化
        session_manager(const session_manager &) = delete;            // 禁止拷贝构造
        session_manager &operator=(const session_manager &) = delete; // 禁止拷贝赋值
        session_manager(session_manager &&) = delete;                 // 禁止移动构造
        session_manager &operator=(session_manager &&) = delete;      // 禁止移动赋值

        std::shared_mutex sessions_mutex; // 保护sessions_的互斥锁
        std::unordered_map<int, std::shared_ptr<client_session>> sessions_; // key: UID, value: client_session对象
    public:
        static session_manager &get_instance()
        {
            static session_manager instance;
            return instance;
        }
        void add_session(int UID, std::shared_ptr<client_session> session);
        void remove_session(int UID);
        std::shared_ptr<client_session> find_session(int UID);
};