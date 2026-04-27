#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

class ThreadPool {
    private:
        std::vector<std::unique_ptr<std::thread>> workers;
        std::queue<std::function<void()>> tasks;
        std::mutex th_pool_mtx_;
        std::condition_variable cv_;
        bool is_exit_ ;
        int num_threads_ = 8;
    public:
        ThreadPool();
        ThreadPool(int num);
        void add_task(std::function<void()> task);
        void run();
        void stop_pool();
        ~ThreadPool();
};