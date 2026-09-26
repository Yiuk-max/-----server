#pragma once
#include "total.h"
#include "account.h"
#include "client_transport.h"
#include "chat_message_data.h"
#include "message_handler.h"
#include "social_module.h"
#include "repository_hub.h"
#include "group.h"
#include "message.pb.h"
#include "memory_pool.h"

// ============================================================
// client_session：单个连接的「会话上下文 + 生命周期 + 发送端口 + 分发入口」。
// 具体业务逻辑已下沉到 src/logic/handlers/ 下的各 Message_handler 实现，
// 本类只提供业务 handler 所需的只读上下文、发送能力与会话状态切换。
// ============================================================
class FileService;

class client_session : public std::enable_shared_from_this<client_session>{
private:
    //===============基本信息===============
    int logged_in_uid_ = -1;                                    // 当前登录 UID；-1 表示未登录
    bool online = true;                                         // 连接/用户在线状态
    std::mutex lifecycle_mtx_;                                  // 串行化登录/登出/断线/顶号状态切换
    std::shared_ptr<account> current_account_;                  // 当前用户的账户信息（会话模块，登录时经 repo 加载）
    std::shared_ptr<social_module> social_manager_;             // 自己的社交关系模块（登录时创建，随会话生命周期）
    std::shared_ptr<RepositoryHub> repo_hub_ = std::make_shared<RepositoryHub>(); // 仓储门面（构造时装配真实 MySQL repo，业务层经 accounts() 调用）
    std::shared_ptr<FileService> file_service_;                 // 文件业务（上传/下载/分片/暂停续传），随会话生命周期
    //===============传输端口===============
    // 会话只依赖抽象传输，不感知 TCP 帧头；弱引用避免 transport -> session -> transport 成环。
    std::weak_ptr<IClientTransport> transport_;

    //===============消息处理器===============
    std::unordered_map<std::string,std::unique_ptr<Message_handler>> handlers_;
    void init_();

public:
    //===============传输端口===============
    void set_transport(const std::shared_ptr<IClientTransport>& transport);
    void on_disconnected();                                     // 连接断开后的幂等在线表/会话状态清理

    //===============会话上下文（供 logic 层 handler 读取）===============
    bool is_online() const { return online; }
    int logged_in_uid() const { return logged_in_uid_; }
    std::shared_ptr<account> current_account() const { return current_account_; }
    std::shared_ptr<social_module> social_manager() const { return social_manager_; }
    std::shared_ptr<RepositoryHub> repo_hub() const { return repo_hub_; }
    std::shared_ptr<FileService> file_service() const { return file_service_; }

    // 登录成功时在锁内切换会话状态并替换在线表，返回被顶掉的旧会话（无则 nullptr）。
    // 连接已断开时返回 false，不建立会话。
    bool activate_session(const std::shared_ptr<account>& account,
                          std::shared_ptr<social_module> social,
                          std::shared_ptr<client_session>* old_session);

    //===============生命周期===============
    void logout();                                              // 登出当前账号但保留连接
    void exit_self();                                           // 退出系统并关闭连接
    void kick_offline();                                        // 被顶下线：通知并关闭本连接

    //===============跨会话内存列表同步（只改 social_module 内存，不写库）===============
    void add_friend_to_list(int friend_UID);
    void remove_friend_from_list(int friend_UID);
    void add_group_to_list(int group_UID);
    void remove_group_from_list(int group_UID);
    void add_community_to_list(int community_id);
    void remove_community_from_list(int community_id);
    void add_channel_to_list(int channel_id);
    void remove_channel_from_list(int channel_id);
    void reload_community_state();

    //===============消息分发===============
    // 接收驱动：由 connection::process_incoming 回调；负责解析 protobuf 并策略分发到 handlers_
    void on_message(const std::string& payload, std::string file_data);

    //===============发送端口===============
    void package_message(const std::string& message,std::string type);
    void package_chat_message(const ChatMessageData& data);
    void package_envelope(const chat_proto::Envelope& message);
    void send_file_packet(const chat_proto::Envelope& message, std::string file_data);
    void send_serialized_packet(const std::string& payload);

    //===============内存池===============
    // client_session 对象的分配/回收统一走按类型池化的 ClassMemoryPool，
    // 池耗尽时自动回退到全局 ::operator new/delete（仅单个对象路径，数组 new 仍走全局）。
    static void* operator new(std::size_t size);
    static void operator delete(void* p) noexcept;

    //===============构造、析构===============
    client_session(){ init_(); }
    ~client_session();
};

// 构造聊天消息 Envelope：把单发(package_chat_message)与广播(NoticeService)共用的字段填充逻辑收敛到一处。
void build_chat_envelope(chat_proto::Envelope& env, const ChatMessageData& data);
