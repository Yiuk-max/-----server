#include "Text_msg_handler.h"
#include "total.h"
#include "user.h"
#include "group.h"
#include "thread_pool.h"
#include "epoller.h"
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
    
    auto pool = std::make_shared<ThreadPool>(8);
    
    auto sub1 = std::make_shared<sub_reactor>(pool);
    auto sub2 = std::make_shared<sub_reactor>(pool);
    auto sub3 = std::make_shared<sub_reactor>(pool);

    std::thread t1([sub1](){ sub1->loop(); });
    std::thread t2([sub2](){ sub2->loop(); });
    std::thread t3([sub3](){ sub3->loop(); });
    
    // 分离线程，让它们在后台运行
    t1.detach();
    t2.detach();
    t3.detach();

    // 4. 在主线程中创建并运行 Main Reactor
    // 注意：构造函数现在需要传入 server_fd
    main_reactor main_react(server_fd, sub1, sub2, sub3);
    
    std::cout << "Main reactor started on thread " << std::this_thread::get_id() << std::endl;
    
    // 主线程将阻塞在这里处理 Accept 事件
    main_react.loop(); 
    /*
    //epoll_event_loop(server_fd, pool);
    // 保持服务器运行
    while(running){
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    */
    pool->stop_pool();
    
    close(server_fd);
    std::cout << "Server stopped." << std::endl;
    return 0;
}
