#include "client_session.h"
#include "total.h"
#include "account.h"
#include "group.h"
#include "thread_pool.h"
#include "epoller.h"
#include "server_config.h"
#include "mysql_conn_pool.h"
#include <cerrno>


bool running = true;

int main(){
    // 启动时读取 configure.json，判断是否启用心跳包
    ServerConfig::get_instance().load("configure.json");

    // 初始化 MySQL 连接池（数量、连接信息读 configure.json 的 database 配置）
    MySQL_Conn_Pool::get_instance().init();

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

    // 在主线程中创建并运行 Main Reactor
    main_reactor main_react(server_fd);
    
    std::cout << "Main reactor started on thread " << std::this_thread::get_id() << std::endl;
    
    // 主线程将阻塞在这里处理 Accept 事件
    main_react.loop(); 
    /*
    // epoll_event_loop(server_fd, pool);
    // 保持服务器运行
    while(running){
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    */
    // 说明：sub_reactor 各自持有自己的线程池，由 main_reactor 负责其生命周期；
    //       旧的 pool->stop_pool() 引用已不存在的变量，已移除。
    close(server_fd);
    std::cout << "Server stopped." << std::endl;
    return 0;
}

