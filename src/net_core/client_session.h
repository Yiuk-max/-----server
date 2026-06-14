#pragma once
#include "total.h"
#include "account.h"
#include "receiver_sender.h"
#include "message_handler.h"
#include "social_module_manager.h"
#include "account_manager.h"
#include "group.h"
#include "group_manager.h"

class client_session{
private:
    //===============基本信息===============
    int client_fd;
    int epoll_fd_;
    int session_key_;           // 当前在 session_manager 中的 key（未登录时为 fd，登录后为 UID）
    bool online = true; // 用户在线状态
    std::shared_ptr<account> current_account_;                  // 当前用户的账户信息
    std::shared_ptr<social_module> social_manager_;   // 社交关系管理器
    //===============发送模块、接收模块===============
public:
    std::unique_ptr<receiver> receiver_;
    std::unique_ptr<sender> sender_;
    //===============消息处理模块===============
    std::unordered_map<std::string,std::unique_ptr<Message_handler>> handlers_; 
    //初始化消息处理器，后续可以根据需要添加更多类型的消息处理器
    void init_();
    //============Account模块===============
    public:
    //===============构造、析构函数===============
    client_session(){};
    client_session(int fd,int epoll_fd);  // 实现在 .cpp，避免循环依赖    
    ~client_session();
    //===============注册、登录、退出===============
    void register_user(std::string username,std::string password);          //注册新用户                                                   //注册新用户，分配UID
    void login(int UID,std::string password);                  //登陆，密码123456
    void logout();                                                          //登出
    void exit_self();                                                       //退出系统
    //=============效验==============
    bool target_UID_is_exit(int target_UID);
    bool target_UID_is_online(int target_UID);
    //===============消息处理===============
    void handle(std::string raw_message);                                       //判断信息类型
    //===============业务逻辑===============
    void show_chatlist();                                                   //展示聊天对象（好友、群聊）   

    void private_chat(int target_UID,std::string message);                 //私聊    
    void group_chat(int target_UID,std::string message);  //群聊——发言

    void create_group(std::string group_name);                              //创建群聊
    void delete_group(int group_UID);                              //删除群聊
    void group_add_client(int target_group_UID,int target_user_UID);     //群聊——添加群成员
    void group_delete_client(int target_group_UID,int target_user_UID);  //群聊——踢出群成员
    void modify_group_name(int group_UID,std::string new_name);      //群聊——改名


    //===============发送===============

    void package_message(const std::string& message,std::string type);      //打包信息并等待处理

};
    //void spk_to(std::string name,std::string message);                      //找到聊天对象
    //void send_msg();  已改为网络层直接调用发送模块                              //调用sender模块发送
    //void request_file(int file_name);                                     //请求文件
    //void preprocess_recv_data(std::string raw_message);  改为由handle调用接收模块预处理//处理接收到的信息


