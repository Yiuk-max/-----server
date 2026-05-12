#pragma once
#include "total.h"
#include "Text_msg_handler.h"
#include "thread_pool.h"
extern bool running;
extern int epoll_fd;
void epoll_event_loop(int server_fd, ThreadPool& pool);
