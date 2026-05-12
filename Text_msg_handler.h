#pragma once
#include "total.h"
#include "user.h"

class Text_msg_handler{
    private:
    std::string in_buffer;       // 未处理原始数据
    int client_fd;
    std::string out_buffer;      // 发送缓冲区
    std::mutex out_mtx;          // 保护缓冲区的锁

    public:
    Text_msg_handler(){};
    Text_msg_handler(int fd):client_fd(fd){}

    void spk_to(std::string name,std::string message);//找到聊天对象

    void handle(std::string message);//判断信息类型
    void login(std::string username,std::string password);//登陆，密码123456
    void show_chatlist();//展示聊天对象（好友、群聊）

    void spk_group(std::shared_ptr<group> chat_group,std::string message);//群聊——发言
    void create_group(std::string group_name);//创建群聊
    void delete_group(std::string group_name);//删除群聊
    void spk_personally(int target_fd,std::string message);//私聊
    void group_add_client(std::string group_name,std::string username);//群聊——添加群成员
    void group_delete_client(std::string group_name,std::string username);//群聊——踢出群成员

    void modify_group_name(std::string old_name,std::string new_name);//群聊——改名

    void process_input(std::string raw_message);//解析包头和包体，调用相应的处理函数
    void send_message(const std::string& message);//打包信息
    void on_write();//EPOLLOUT事件
    
    ~Text_msg_handler();

    void exit_self();//退出系统
};
