#include "total.h"
#include "user.h"
class handle_msg{
    private:
    int client_fd;


    public:
    handle_msg(){};
    handle_msg(int fd):client_fd(fd){}

    void spk_to(std::string name,std::string message);//done

    void close_server();//close server

    void handle(std::string message);//
    void login();//done
    void show_chatlist();//done

    void spk_group(std::shared_ptr<group> chat_group,std::string message);//done
    void create_group(std::string message);//done
    void delete_group(std::string message);//done
    void spk_personally(int target_fd,std::string message);//done
    void group_add_client(std::string message);//done
    void group_delete_client(std::string message);//
    ~handle_msg();

    void exit_self();
};