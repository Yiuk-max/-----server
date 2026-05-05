#include "handle_msg.h"
#include "total.h"
#include "user.h"
#include "group.h"
#include "thread_pool.h"
#include "reactor.h"
#include <cerrno>


bool running = true;



int main(){
    // Create a socket
    int server_fd = socket(AF_INET6,SOCK_STREAM,0);
    if(server_fd == -1){
        std::cerr << "Failed to create socket" << std::endl;
        return -1;
    }
    //bind the socket to an address and port
    struct sockaddr_in6 server_addr{};
    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_addr = in6addr_any;    //listen on all interfaces
    server_addr.sin6_port = htons(8080);
    int opt = 1;
    if(setsockopt(server_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt)) == -1){
        std::cerr << "Failed to set socket options" << std::endl;
        close(server_fd);
        return 1;
    }
    if(bind(server_fd,(struct sockaddr *)&server_addr,sizeof(server_addr)) == -1){
        std::cerr << "Failed to bind socket" << std::endl;
        close(server_fd);
        return 1;
    }
    // Listen for incoming connections
    if(listen(server_fd,128) == -1){
        std::cerr << "Failed to listen on socket" << std::endl;
        close(server_fd);
        return 1;
    }
    std::cout << "Server is listening on port 8080..." << std::endl;
    
    ThreadPool pool;
    // std::thread accept_thread(accept_connections_thread, server_fd);
    // std::thread recv_thread(recv_msg_thread, std::ref(pool));
    
    epoll_event_loop(server_fd, pool);
    // 保持服务器运行
    while(running){
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    pool.stop_pool();
    
    close(server_fd);
    std::cout << "Server stopped." << std::endl;
    return 0;
}

//to do list:
//修改登陆，通过handle函数处理登录，登录成功后才处理消息    done
//修改main函数，调用epoll_event_loop(pool)替代recv_msg_thread done
//handle模块解析json格式的消息，区分登录、私聊、群聊等不同类型的消息  done
//取消各功能模块通过":"解析原始字符串的方式，直接使用handle模块解析后传递给各功能模块 done
//添加删除群聊功能 done
//群聊增加管理员权限，只有管理员才能添加删除群成员，修改群名称等 done
//创建群聊时，自动将创建者加入群聊，并赋予管理员权限
//增加错误处理机制，针对各种异常情况给出明确的错误提示，并确保服务器的稳定运行
//epoll 模块放入单独文件，提供接口供main调用，提高代码的模块化和可维护性，使主函数更简洁，专注于服务器的整体流程控制

//长期目标：
//解决tcp粘包问题，确保消息的完整性和正确解析
//增加心跳机制，检测客户端的在线状态，及时清理断开连接的客户端资源
//增加日志模块，记录服务器的运行状态、错误信息等，便于调试和维护

/*
    json格式

    type
    sender_name
    target_name
    content
*/