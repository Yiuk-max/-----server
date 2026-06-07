#pragma once
#include "total.h"
#include "user.h"
#include "receiver_sender.h"
#include "message_handler.h"
class client_session{
    private:
    //===============基本信息===============
    int client_fd;
    int epoll_fd_;
    //===============发送模块、接收模块===============
    std::unique_ptr<receiver> receiver_;
    std::unique_ptr<sender> sender_;
    //===============消息处理模块===============
    std::unordered_map<std::string,std::unique_ptr<Message_handler>> handlers_; 
    //
    void init_handlers(){
        handlers_["spk"]                    = std::make_unique<Spk_handler>();
        handlers_["show"]                   = std::make_unique<Show_handler>();
        handlers_["create_group"]           = std::make_unique<Create_group_handler>();
        handlers_["group_add_client"]       = std::make_unique<Group_add_client_handler>();
        handlers_["group_delete_client"]    = std::make_unique<Group_delete_client_handler>();
        handlers_["exit"]                   = std::make_unique<Exit_handler>();
        handlers_["download_file"]          = std::make_unique<Download_file_handler>();
        handlers_["delete_group"]           = std::make_unique<Delete_group_handler>();
        handlers_["modify_group_name"]      = std::make_unique<Modify_group_name_handler>();
        handlers_["login"]                  = std::make_unique<Login_handler>();
    }   
    public:
    //===============构造、析构函数===============
    client_session(){};
    client_session(int fd,int epoll_fd):client_fd(fd),epoll_fd_(epoll_fd){
        receiver_ = std::make_unique<receiver>(epoll_fd,fd);
        sender_ = std::make_unique<sender>(epoll_fd,fd);
        init_handlers();
    }    
    ~client_session();
    //===============注册、登录、退出===============
    void register_user(std::string username,std::string password);          //注册新用户                                                   //注册新用户，分配UID
    void login(std::string username,std::string password);                  //登陆，密码123456
    void exit_self();                                                       //退出系统
    //===============消息处理===============
    void handle(std::string message);                                       //判断信息类型
    //===============业务逻辑===============
    void show_chatlist();                                                   //展示聊天对象（好友、群聊）    
    void spk_to(std::string name,std::string message);                      //找到聊天对象
    void spk_group(std::shared_ptr<group> chat_group,std::string message);  //群聊——发言
    void create_group(std::string group_name);                              //创建群聊
    void delete_group(std::string group_name);                              //删除群聊
    void spk_personally(int target_fd,std::string message);                 //私聊
    void group_add_client(std::string group_name,std::string username);     //群聊——添加群成员
    void group_delete_client(std::string group_name,std::string username);  //群聊——踢出群成员
    void modify_group_name(std::string old_name,std::string new_name);      //群聊——改名
    //void request_file(int file_name);                                              //请求文件
    //===============数据处理===============
    void preprocess_recv_data(std::string raw_message);                     //新的处理，仅调用
    void package_message(const std::string& message,std::string type);      //打包信息并等待处理
    void send_msg();                                                        //新版发送
};
