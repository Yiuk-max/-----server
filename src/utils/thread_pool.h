#pragma once
#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <type_traits>
#include <cstddef>
#include <exception>

class ThreadPool
{
private:
    // std::vector<std::unique_ptr<std::thread>> workers;
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex th_pool_mtx_;
    std::condition_variable cv_;
    bool is_exit_;
    int num_threads_ = 8;
    std::size_t max_tasks_ = 10000;   // 任务队列上限，防止洪峰时无限堆积导致 OOM

public:
    ThreadPool();
    ThreadPool(int num);
    ThreadPool(int num, std::size_t max_tasks);   // 指定线程数与任务队列上限
    void set_max_tasks(std::size_t max_tasks);    // 调整队列上限
    // void add_task(std::function<void()> task);已删除
    void run();
    void stop_pool();
    ~ThreadPool();

    template <typename F, typename... Args>
    auto submit_task(F &&f, Args &&...args)
    {
        using return_type = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<return_type> result = task->get_future();
        {
            std::unique_lock<std::mutex> lock(th_pool_mtx_);
            if (is_exit_)
            {
                throw std::runtime_error("ThreadPool is stopped, cannot add new tasks.");
            }
            if (tasks.size() >= max_tasks_)
            {
                throw std::runtime_error("ThreadPool task queue is full.");
            }
            tasks.emplace([task]()
                          { (*task)(); });
        }
        cv_.notify_one();
        return result;
    }
};