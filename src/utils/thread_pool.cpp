#include "thread_pool.h"

ThreadPool::ThreadPool() : ThreadPool(8, 10000) {}
ThreadPool::ThreadPool(int num) : ThreadPool(num, 10000) {}
ThreadPool::ThreadPool(int num, std::size_t max_tasks)
    : is_exit_(false), num_threads_(num), max_tasks_(max_tasks) {
    if (num_threads_ <= 0) {
        num_threads_ = 8;
    }
    if (max_tasks_ == 0) {
        max_tasks_ = 10000;
    }
    std::cout << "initializing thread pool ... " << std::endl;
    for (int i = 0; i < num_threads_; ++i) {
        workers.emplace_back(&ThreadPool::run, this);
    }
    std::cout << "thread pool initialized with " << num_threads_
              << " threads, max queue " << max_tasks_ << "." << std::endl;
}

void ThreadPool::set_max_tasks(std::size_t max_tasks) {
    std::lock_guard<std::mutex> lock(th_pool_mtx_);
    max_tasks_ = (max_tasks == 0) ? 10000 : max_tasks;
}

ThreadPool::~ThreadPool(){
    stop_pool();
}
void ThreadPool::stop_pool(){
    {
        std::unique_lock<std::mutex> lock(th_pool_mtx_);
        is_exit_ = true;
    }
    cv_.notify_all();
    for(auto& worker:workers){
        if(worker.joinable()){
            worker.join();
        }
    }
}
void ThreadPool::run(){
    while(true){
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(th_pool_mtx_);
            cv_.wait(lock,[this](){return is_exit_ || !tasks.empty();});
            if(is_exit_ && tasks.empty()){
                return;
            }
            task = std::move(tasks.front());
            tasks.pop();
        }
        // 业务任务异常不得逃出 worker 线程（否则 std::thread 入口以异常退出会 std::terminate），
        // 这里统一吞掉并记录日志，保证单个坏任务不会拖垮整个进程。
        try {
            task();
        } catch (const std::exception& e) {
            std::cerr << "[ThreadPool] task exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[ThreadPool] task exception: unknown error." << std::endl;
        }
    }
}