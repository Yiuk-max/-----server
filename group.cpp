#include "group.h"

void group::group_spk(std::string message){
    for(int fd:group_clients){
        if(fd == -1) continue;
        write(fd,message.c_str(),message.length());
    }
}
bool group::add_client(std::string name){
    int target_fd=-1;
    for(auto& pair:username_to_fd){
        if(pair.first == name){
            std::lock_guard<std::mutex> lock(group_mutex_);
            target_fd=pair.second;
            break;
        }
    }
    if(target_fd==-1){
        return false;
    }
    client_name_group[name]=target_fd;
    bool replace_one=false;
    for(int i=0;i<group_clients.size();i++){
        if(group_clients[i]==-1){
            std::lock_guard<std::mutex> lock(group_mutex_);
            group_clients[i]=target_fd;
            replace_one=true;
            break;
        }
    }
    if(!replace_one){
        group_clients.push_back(target_fd);
    }
    return true;
}
bool group::delete_client(std::string name){
    bool find=false;
    int target_fd=-1;
    for(auto& pair:client_name_group){
        if(pair.first == name){
            std::lock_guard<std::mutex> lock(group_mutex_);
            find=true;
            target_fd=pair.second;
            client_name_group.erase(name);
            break;
        }
    }
    if(find){
        for(int i=0;i<group_clients.size();i++){
            if(group_clients[i]==target_fd){
                std::lock_guard<std::mutex> lock(group_mutex_);
                group_clients[i]=-1;
                break;
            }
        }
        return true;
    }else{
        return false;
    }
}
bool group::is_manager_fd(int fd){
    return fd == manager_fd_;
}
