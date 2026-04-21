#include "total.h"
#include "user.h"
class handle_msg{
    private:
    int client_fd;


    public:
    handle_msg(){};
    handle_msg(int fd):client_fd(fd){}
    void close_server();
    void spk_public(std::string message);
    void spk_private(std::string message);
    void handle();
    void login();
    void show_userlist();
};