#include "message_handler.h"
#include "client_session.h"

void Chat_handler::handle_message(const chat_proto::Envelope& message, client_session& session, std::string &file_data){
    const std::string type = message.type();
    if(type == "private_chat"){
        int target_UID = message.target_uid();
        std::string msg = message.message();
        session.private_chat(target_UID, msg, message.reply_to_message_id());
        return;
    }
    else if(type == "group_chat"){
        int target_UID = message.target_uid();
        std::string msg = message.message();
        session.group_chat(target_UID, msg, message.reply_to_message_id());
        return;
    }
    else if(type == "delete_message"){
        int message_id = message.message_id();
        session.delete_message(message_id);
        return;
    }
    else if(type == "history_request"){
        // 游标分页拉聊天历史：peer_id = 对方/群 UID；before_id 可选（首屏不传，protobuf 默认为 0）
        session.chat_history(message.peer_id(), message.before_id());
        return;
    }
    else if(type == "add_friend"){
        // 按邮箱定位要添加的好友：email 缺失/未绑定时由 social_module 给出系统提示
        session.send_friend_request(message.email(), message.apply_message());
        return;
    }
    else if(type == "set_friend_remark"){
        int friend_UID = message.friend_uid();
        std::string remark = message.remark();
        session.set_friend_remark(friend_UID, remark);
        return;
    }
    else if(type == "accept_friend"){
        int sender_UID = message.sender_uid();
        session.handle_friend_request(sender_UID, true);
        return;
    }
    else if(type == "reject_friend"){
        int sender_UID = message.sender_uid();
        session.handle_friend_request(sender_UID, false);
        return;
    }
    else if(type == "remove_friend"){
        int friend_UID = message.friend_uid();
        session.remove_friend(friend_UID);
        return;
    }
    else if(type == "show_friend_requests"){
        session.show_friend_requests();
        return;
    }
}
void Group_handler::handle_message(const chat_proto::Envelope& message, client_session& session, std::string &file_data){
    const std::string type = message.type();
    if(type == "create_group"){
        std::string group_name = message.group_name();
        session.create_group(group_name);
        return;
    }
    else if(type == "delete_group"){
        int group_UID = message.group_uid();
        session.delete_group(group_UID);
        return;
    }
    else if(type == "group_add_client"){
        int group_UID = message.group_uid();
        int target_user_UID = message.target_user_uid();
        session.group_add_client(group_UID, target_user_UID);
        return;
    }
    else if(type == "group_delete_client"){
        int group_UID = message.group_uid();
        int target_user_UID = message.target_user_uid();
        session.group_delete_client(group_UID, target_user_UID);
        return;
    }
    else if(type == "modify_group_name"){
        int group_UID = message.group_uid();
        std::string new_name = message.new_name();
        session.modify_group_name(group_UID, new_name);
        return;
    }
    else if(type == "send_join_group"){
        int group_UID = message.group_uid();
        session.send_join_group(group_UID);
        return;
    }
    else if(type == "handle_join_request"){
        int group_UID = message.group_uid();
        int requester_UID = message.requester_uid();
        bool accept = message.accept();
        session.handle_join_request(group_UID, requester_UID, accept);
        return;
    }
    else if(type == "modify_member_role"){
        int group_UID = message.group_uid();
        int target_UID = message.target_uid();
        bool promote = message.promote();
        session.modify_member_role(group_UID, target_UID, promote);
        return;
    }
    else if(type == "show_group_requests"){
        int group_UID = message.group_uid();
        session.show_group_requests(group_UID);
        return;
    }
    else if(type == "show_group_members"){
        int group_UID = message.group_uid();
        session.show_group_members(group_UID);
        return;
    }
}
void Base_handler::handle_message(const chat_proto::Envelope& message, client_session& session, std::string &file_data){
    const std::string type = message.type();
    if(type == "show"){
        session.show_chatlist();
        return;
    }
    else if(type == "logout"){
        session.logout();
        return;
    }
    else if(type == "exit"){
        session.exit_self();
        return;
    }
    else if(type == "login"){
        std::string password = message.password();
        // 登录支持两种方式：带 email 则用邮箱，否则用 UID（老客户端兼容）
        if(!message.email().empty()){
            session.login_by_email(message.email(), password);
        } else {
            int UID = message.uid();
            session.login(UID, password);
        }
        return;
    }
    else if(type == "verify_token"){
        session.verify_token(message.token());
        return;
    }
    else if(type == "register"){
        std::string username = message.username();
        std::string password = message.password();
        std::string email    = message.email();
        session.register_user(username, password, email);
        return;
    }
    else if(type == "set_email"){
        session.set_email(message.email());
        return;
    }
    else if(type == "change_name"){
        std::string new_name = message.new_name();
        session.change_my_name(new_name);
        return;
    }
}
void File_handler::handle_message(const chat_proto::Envelope& message, client_session& session, std::string &file_data){
    const std::string type = message.type();
    if(type == "upload_file") {
        // 文件二进制内容由传输层切帧后通过 file_data 参数传入。
        session.upload_file(message.meta(), file_data);
        return;
    }
     else if(type == "download_file"){
        std::string file_name = message.file_name();
        session.download_file(file_name);
        return;
    }
}
