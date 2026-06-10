#include "message_handler.h"
void Chat_handler::handle_message(const json& message,client_session& session,std::string &file_data){
    std::string type = message["cmd"];
    if(type == "spk"){
        std::string name = message["name"];
        std::string msg = message["message"];
        session.spk_to(name,msg);
        return;
    }
}
void Group_handler::handle_message(const json& message,client_session& session,std::string &file_data){
    std::string type = message["cmd"];
    if(type == "create_group"){
        std::string group_name = message["group_name"];
        session.create_group(group_name);
        return;
    }
    else if(type == "delete_group"){
        std::string group_name = message["group_name"];
        session.delete_group(group_name);
        return;
    }
    else if(type == "group_add_client"){
        std::string group_name = message["group_name"];
        std::string username = message["username"];
        session.group_add_client(group_name,username);
        return;
    }
    else if(type == "group_delete_client"){
        std::string group_name = message["group_name"];
        std::string username = message["username"];
        session.group_delete_client(group_name,username);
        return;
    }
    else if(type == "modify_group_name"){
        std::string old_name = message["old_group_name"];
        std::string new_name = message["new_group_name"];
        session.modify_group_name(old_name,new_name);
        return;
    }
}
void Base_handler::handle_message(const json& message,client_session& session,std::string &file_data){
    std::string type = message["cmd"];
    if(type == "show"){
        session.show_chatlist();
        return;
    }
    else if(type == "exit"){
        session.exit_self();
        return;
    }
    else if(type == "login"){
        std::string username = message["username"];
        std::string password = message["password"];
        session.login(username,password);
        return;
    }
    else if(type == "register"){
        std::string username = message["username"];
        std::string password = message["password"];
        session.register_user(username,password);
        return;
    }
}
void File_handler::handle_message(const json& message,client_session& session,std::string &file_data){
    std::string type = message["cmd"];
    if(type == "upload_file") {
        // 这里的content是文件数据，实际应用中可能还需要文件路径等信息
        std::string file_data = message["file_data"];
        json meta = message["meta"]; // 包含文件名、大小等元信息
        session.receiver_->upload_file(meta, file_data);
        return;
    }
     else if(type == "download_file"){
        std::string file_name = message["file_name"];
        session.sender_->send_file(file_name);
        return;
    }
}