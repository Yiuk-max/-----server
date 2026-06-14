#pragma once
#include <unordered_map>
#include <memory>
//#include "total.h"
#include "group.h"
#include "client_session.h"
#include "UID_allocator.h"

class group;

class Group_manager
{
private:
    Group_manager() = default; // 私有构造函数，禁止外部实例化
    Group_manager(const Group_manager &) = delete;            // 禁止拷贝构造
    Group_manager &operator=(const Group_manager &) = delete; // 禁止拷贝赋值
    Group_manager(Group_manager &&) = delete;                 // 禁止移动构造
    Group_manager &operator=(Group_manager &&) = delete;      // 禁止移动赋值

    std::mutex group_manager_mutex; // 保护group_list的互斥锁
    std::unordered_map<int,std::shared_ptr<group>> group_list_;
public:
    static Group_manager &get_instance()
    {
        static Group_manager instance;
        return instance;
    }
    void create_group(int manager_UID,std::string group_name);
    void delete_group(int UID,int sender_fd);
    std::shared_ptr<group> find_group(int group_id);
    void add_group_member(int group_UID,int newmember_UID,int sender_UID);
    void remove_group_member(int group_UID,int member_UID,int sender_UID);
    void show_group_member(int group_UID);
    
};