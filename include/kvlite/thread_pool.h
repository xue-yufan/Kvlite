#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace kvlite {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 投递一个任务。如果线程池已停止，抛出 std::runtime_error
    void enqueue(std::move_only_function<void()> task);

    // 等待所有已投递的任务完成，不停止线程池
    void wait_idle();

    // 停止线程池：拒绝新任务，等待已投递任务完成，join 所有线程
    void shutdown();

private:
    std::vector<std::thread> workers_;
    std::queue<std::move_only_function<void()>> tasks_;
    std::mutex mutex_;
    // 唤醒工作线程
    std::condition_variable task_cv_;
    // 唤醒等待空闲的调用方
    std::condition_variable idle_cv_;
    bool stopping_ = false;
    std::size_t active_tasks_ = 0;

    void worker_loop();
};

}
