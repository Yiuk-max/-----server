#pragma once
#include <unordered_map>
#include <memory>
#include "group.h"
#include "UID_allocator.h"

class group;

class Group_manager //类似的manager应该是为维护活跃群对象的生命周期而存在，而不是为群聊的创建和删除提供接口
{                   //引入数据库后逐步改造，对接repo_interface接口
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
    //group相关内存管理接口
    void active_group(int group_UID);//群聊第一个成员加入时，创建群聊对象并加入group_list_
    void inactive_group(int group_UID);//群聊最后一个成员退出时，销毁群

    //下面的接口应该是对repo_interface的封装，提供给上层逻辑使用
    void create_group(int manager_UID,std::string group_name, int& out_group_uid);
    void delete_group(int UID,int sender_fd);
    std::shared_ptr<group> find_group(int group_id);
    void add_group_member(int group_UID,int newmember_UID,int sender_UID);
    void remove_group_member(int group_UID,int member_UID,int sender_UID);
    void show_group_member(int group_UID);
    
};