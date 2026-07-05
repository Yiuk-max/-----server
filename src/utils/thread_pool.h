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

public:
    ThreadPool();
    ThreadPool(int num);
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
            tasks.emplace([task]()
                          { (*task)(); });
        }
        cv_.notify_one();
        return result;
    }
};