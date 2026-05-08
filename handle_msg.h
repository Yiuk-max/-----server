#pragma once
#include "total.h"
#include "user.h"
class handle_msg{
    private:
    int client_fd;
    std::string out_buffer;      // 发送缓冲区
    std::mutex out_mtx;          // 保护缓冲区的锁

    public:
    handle_msg(){};
    handle_msg(int fd):client_fd(fd){}

    void spk_to(std::string name,std::string message);//done

    void handle(std::string message);//
    void login(std::string username,std::string password);//
    void show_chatlist();//done

    void spk_group(std::shared_ptr<group> chat_group,std::string message);//done
    void create_group(std::string group_name);//done
    void delete_group(std::string group_name);//done
    void spk_personally(int target_fd,std::string message);//done
    void group_add_client(std::string group_name,std::string username);//done
    void group_delete_client(std::string group_name,std::string username);

    void modify_group_name(std::string old_name,std::string new_name);

    void send_message(const std::string& message);

    void on_write();
    
    ~handle_msg();

    void exit_self();
};
