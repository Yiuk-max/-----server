#pragma once
#include "total.h"
#include "handle_msg.h"
#include "thread_pool.h"
extern bool running;
extern int epoll_fd;
void epoll_event_loop(int server_fd, ThreadPool& pool);
