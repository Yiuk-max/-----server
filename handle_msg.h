#include "total.h"
#include "user.h"
class handle_msg{
    private:
    int client_fd;


    public:
    handle_msg(){};
    handle_msg(int fd):client_fd(fd){}

    void spk_to(std::string name);//done

    void close_server();//close server
    //old version
    void spk_public(std::string message);
    void spk_private(std::string message);

    void handle();//done
    void login();//done
    void show_chatlist();//done

    void spk_group(std::shared_ptr<group> chat_group);//done
    void create_group();//done
    void delete_group();
    void spk_personally(int target_fd);//done
};