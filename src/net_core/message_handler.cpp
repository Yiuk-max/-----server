#include "message_handler.h"
#include "client_session.h"
void Chat_handler::handle_message(const json& message,client_session& session,std::string &file_data){
    std::string type = message["type"];
    if(type == "private_chat"){
        int target_UID = message["target_UID"];
        std::string msg = message["message"];
        session.private_chat(target_UID, msg);
        return;
    }
    else if(type == "group_chat"){
        int target_UID = message["target_UID"];
        std::string msg = message["message"];
        session.group_chat(target_UID, msg);
        return;
    }
    else if(type == "delete_message"){
        int message_id = message["message_id"];
        session.delete_message(message_id);
        return;
    }
    else if(type == "refresh_offline_messages"){
        session.refresh_offline_messages();
        return;
    }
    else if(type == "add_friend"){
        int target_UID = message["target_UID"];
        std::string apply_message = message["apply_message"];
        session.send_friend_request(target_UID, apply_message);
        return;
    }
    else if(type == "set_friend_remark"){
        int friend_UID = message["friend_UID"];
        std::string remark = message["remark"];
        session.set_friend_remark(friend_UID, remark);
        return;
    }
    else if(type == "accept_friend"){
        int sender_UID = message["sender_UID"];
        session.handle_friend_request(sender_UID, true);
        return;
    }
    else if(type == "reject_friend"){
        int sender_UID = message["sender_UID"];
        session.handle_friend_request(sender_UID, false);
        return;
    }
    else if(type == "remove_friend"){
        int friend_UID = message["friend_UID"];
        session.remove_friend(friend_UID);
        return;
    }
    else if(type == "show_friend_requests"){
        session.show_friend_requests();
        return;
    }
}
void Group_handler::handle_message(const json& message,client_session& session,std::string &file_data){
    std::string type = message["type"];
    if(type == "create_group"){
        std::string group_name = message["group_name"];
        session.create_group(group_name);
        return;
    }
    else if(type == "delete_group"){
        int group_UID = message["group_UID"];
        session.delete_group(group_UID);
        return;
    }
    else if(type == "group_add_client"){
        int group_UID = message["group_UID"];
        int target_user_UID = message["target_user_UID"];
        session.group_add_client(group_UID, target_user_UID);
        return;
    }
    else if(type == "group_delete_client"){
        int group_UID = message["group_UID"];
        int target_user_UID = message["target_user_UID"];
        session.group_delete_client(group_UID, target_user_UID);
        return;
    }
    else if(type == "modify_group_name"){
        int group_UID = message["group_UID"];
        std::string new_name = message["new_name"];
        session.modify_group_name(group_UID, new_name);
        return;
    }
    else if(type == "send_join_group"){
        int group_UID = message["group_UID"];
        session.send_join_group(group_UID);
        return;
    }
    else if(type == "handle_join_request"){
        int group_UID = message["group_UID"];
        int requester_UID = message["requester_UID"];
        bool accept = message["accept"];
        session.handle_join_request(group_UID, requester_UID, accept);
        return;
    }
    else if(type == "modify_member_role"){
        int group_UID = message["group_UID"];
        int target_UID = message["target_UID"];
        bool promote = message["promote"];
        session.modify_member_role(group_UID, target_UID, promote);
        return;
    }
    else if(type == "show_group_requests"){
        int group_UID = message["group_UID"];
        session.show_group_requests(group_UID);
        return;
    }
    else if(type == "show_group_members"){
        int group_UID = message["group_UID"];
        session.show_group_members(group_UID);
        return;
    }
}
void Base_handler::handle_message(const json& message,client_session& session,std::string &file_data){
    std::string type = message["type"];
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
        std::string password = message["password"];
        // 登录支持两种方式：带 email 则用邮箱，否则用 UID（老客户端兼容）
        if(message.contains("email")){
            session.login_by_email(message.value("email", std::string()), password);
        } else {
            int UID = message["UID"];
            session.login(UID, password);
        }
        return;
    }
    else if(type == "register"){
        std::string username = message.value("username", std::string());
        std::string password = message.value("password", std::string());
        std::string email    = message.value("email", std::string());
        session.register_user(username, password, email);
        return;
    }
    else if(type == "set_email"){
        session.set_email(message.value("email", std::string()));
        return;
    }
    else if(type == "change_name"){
        std::string new_name = message["new_name"];
        session.change_my_name(new_name);
        return;
    }
}
void File_handler::handle_message(const json& message,client_session& session,std::string &file_data){
    std::string type = message["type"];
    if(type == "upload_file") {
        // 文件二进制内容由传输层切帧后通过 file_data 参数传入。
        json meta = message["meta"];
        session.upload_file(meta, file_data);
        return;
    }
     else if(type == "download_file"){
        std::string file_name = message["file_name"];
        session.download_file(file_name);
        return;
    }
}