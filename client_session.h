#pragma once
#include "total.h"
#include "user.h"
#include "receiver_sender.h"
class client_session{
    private:
    int client_fd;
    int epoll_fd_;

    std::unique_ptr<receiver> receiver_;
    std::unique_ptr<sender> sender_;
    public:
    client_session(){};
    client_session(int fd,int epoll_fd):client_fd(fd),epoll_fd_(epoll_fd){
        receiver_ = std::make_unique<receiver>(epoll_fd,fd);
        sender_ = std::make_unique<sender>(epoll_fd,fd);
    }

    void spk_to(std::string name,std::string message);                      //找到聊天对象

    void handle(std::string message);                                       //判断信息类型
    void login(std::string username,std::string password);                  //登陆，密码123456
    void show_chatlist();                                                   //展示聊天对象（好友、群聊）

    void spk_group(std::shared_ptr<group> chat_group,std::string message);  //群聊——发言
    void create_group(std::string group_name);                              //创建群聊
    void delete_group(std::string group_name);                              //删除群聊
    void spk_personally(int target_fd,std::string message);                 //私聊
    void group_add_client(std::string group_name,std::string username);     //群聊——添加群成员
    void group_delete_client(std::string group_name,std::string username);  //群聊——踢出群成员

    void modify_group_name(std::string old_name,std::string new_name);      //群聊——改名

    void preprocess_recv_data(std::string raw_message);//新的处理，仅调用
    void package_message(const std::string& message,std::string type);      //打包信息并等待处理
    void send_msg();                                                        //新版发送
    
    ~client_session();

    void exit_self();//退出系统
};
