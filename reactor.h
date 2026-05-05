#include "total.h"
#include "handle_msg.h"
#include "thread_pool.h"

void epoll_event_loop(int server_fd, ThreadPool& pool);