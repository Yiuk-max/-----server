#pragma once
#include "total.h"
#include "account.h"
#include "connection.h"
#include "message_handler.h"
#include "social_module.h"
#include "repository_hub.h"
#include "group.h"

class client_session{
private:
    //===============基本信息===============
    int session_key_;                                           // 当前在 session_manager 中的 key（未登录时为 fd，登录后为 UID）
    bool online = true;                                         // 用户在线状态
    std::shared_ptr<account> current_account_;                  // 当前用户的账户信息（会话模块，登录时经 repo 加载）
    std::shared_ptr<social_module> social_manager_;             // 自己的社交关系模块（登录时创建，随会话生命周期）
    std::shared_ptr<RepositoryHub> repo_hub_ = std::make_shared<RepositoryHub>(); // 仓储门面（构造时装配真实 MySQL repo，业务层经 accounts() 调用）
    void send_offline_messages(const std::string& since_time);  // 查询并推送自 since_time 之后的离线消息
    //===============收发模块（方案 3b 重拆：connection 彻底独立，此处仅存弱引用回指）===============
public:
    std::weak_ptr<connection> conn_;                            // 非拥有弱引用回指 connection；生命周期由 sub_reactor 持有
    void set_connection(const std::shared_ptr<connection>& c);  // 绑定 connection（存弱引用）
    connection& conn();                                         // 供 message_handler 访问收发模块（仅在本连接消息处理期间调用）
    //===============消息处理模块===============
    std::unordered_map<std::string,std::unique_ptr<Message_handler>> handlers_; 
    //初始化消息处理器，后续可以根据需要添加更多类型的消息处理器
    void init_();
    public:
    //===============构造、析构函数===============

    client_session(): session_key_(-1){ init_(); };                         // 会话由 sub_reactor 创建；init_ 初始化消息处理器；-1 表示尚未绑定连接
    ~client_session();
    //===============注册、登录、退出===============
    void register_user(std::string username,std::string password);          //注册新用户，分配UID
    void login(int UID,std::string password);                               //登陆
    void logout();                                                          //登出
    void exit_self();                                                       //退出系统
    void kick_offline();                                                    //被顶下线：通知并关闭本连接（由新登录的另一会话调用）
    //=============效验==============
    bool target_UID_is_exit(int target_UID);                                //校验目标UID是否存在（个人或群聊UID）
    bool target_UID_is_online(int target_UID);                              //校验目标UID是否在线（个人或群聊UID）
    //===============消息处理===============
    // 接收驱动：由 connection::process_incoming 回调；负责解析 JSON 并策略分发到 handlers_
    void on_message(const std::string& json_data, std::string file_data);
    //===============业务逻辑===============
    void show_chatlist();                                                   //展示聊天对象（好友、群聊）   
    //聊天
    void private_chat(int target_UID,std::string message);                  //私聊    
    void group_chat(int target_UID,std::string message);                    //群聊——发言
    void delete_message(int message_id);                                    //删除消息（仅限发送后3分钟内，通知在线接收方）
    void refresh_offline_messages();                                        //手动刷新离线消息
    //好友相关
    void send_friend_request(int target_UID,std::string apply_message);     //添加好友(通过social_manager_)
    void set_friend_remark(int friend_UID,std::string remark);              //给好友设置备注名
    void handle_friend_request(int sender_UID,bool accept);                 //处理好友申请(同意/拒绝)
    void remove_friend(int friend_UID);                                     //删除好友
    void add_friend_to_list(int friend_UID);                                //同步更新内存好友列表（对方同意加好友后调用）
    void remove_friend_from_list(int friend_UID);                           //同步更新内存好友列表（被对方删除后调用）
    void show_friend_requests();                                            //查看待处理的好友申请
    void change_my_name(std::string new_name);                              //修改自己的昵称
    //群聊相关        
    void create_group(std::string group_name);                                  //创建群聊
    void delete_group(int group_UID);                                           //删除群聊
    void group_add_client(int target_group_UID,int target_user_UID);            //群聊——添加群成员
    void group_delete_client(int target_group_UID,int target_user_UID);         //群聊——踢出群成员
    void modify_group_name(int group_UID,std::string new_name);                 //群聊——改名
    void send_join_group(int group_UID);                                        //申请加入群聊
    void handle_join_request(int group_UID,int requester_UID,bool accept);      //处理群聊加入申请（同意/拒绝）
    void add_group_to_list(int group_UID);                                      //同步更新内存群列表（被拉入群/申请通过后调用）
    void remove_group_from_list(int group_UID);                                 //同步更新内存群列表（被踢出群后调用）
    void modify_member_role(int group_UID,int target_UID,bool promote);         //修改群成员身份（提升/降级）
    void show_group_requests(int group_UID);                                                //查看群聊待处理的入群申请 todo
    void show_group_members(int group_UID);                                     //查看群成员

    //===============发送===============

    void package_message(const std::string& message,std::string type);      //打包信息并等待处理
    void package_chat_message(const std::string& message,std::string type,int message_id); //打包聊天消息（带数据库 message_id）

};