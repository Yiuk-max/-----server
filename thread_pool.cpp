#include "thread_pool.h"

ThreadPool::ThreadPool():is_exit_(false),num_threads_(8){
    std::cout <<"initializing thread pool ... "<<std::endl;
    for(int i=0;i<num_threads_;++i){
        workers.emplace_back(std::make_unique<std::thread>(&ThreadPool::run,this));

    }

}
ThreadPool::ThreadPool(int num):is_exit_(false),num_threads_(num){
    if(num_threads_ <= 0){
        num_threads_ = 8;
    }
    std::cout <<"initializing thread pool ... "<<std::endl;
    for(int i=0;i<num_threads_;++i){
        workers.emplace_back(std::make_unique<std::thread>(&ThreadPool::run,this));

    }

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
        if(worker->joinable()){
            worker->join();
        }
    }
}
void ThreadPool::add_task(std::function<void()> task){
    {
        std::unique_lock<std::mutex> lock(th_pool_mtx_);
        tasks.push(std::move(task));
    }
    cv_.notify_one();
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
        task();
    }
}