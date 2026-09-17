#include "kvlite/thread_pool.h"

#include <stdexcept>

namespace kvlite {

ThreadPool::ThreadPool(std::size_t num_threads) {
    if (num_threads == 0) {
        throw std::invalid_argument("ThreadPool requires at least 1 thread");
    }

    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this]() {
            worker_loop();
        });
    }
}

ThreadPool::~ThreadPool() {
    if (!stopping_) {
        shutdown();
    }
}

void ThreadPool::enqueue(std::move_only_function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("ThreadPool is shutting down");
        }
        tasks_.push(std::move(task));
        ++active_tasks_;
    }
    task_cv_.notify_one();
}

void ThreadPool::wait_idle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_cv_.wait(lock, [this]() {
        return active_tasks_ == 0;
    });
}

void ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return ;
        }
        stopping_ = true;
    }
    task_cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::worker_loop() {
    for (;;) {
        std::move_only_function<void()> task;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            task_cv_.wait(lock, [this]() {
                return stopping_ || !tasks_.empty();
            });

            if (stopping_ && tasks_.empty()) {
                // 停机且没有待处理任务，退出
                return;   
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        task();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            --active_tasks_;
            if (active_tasks_ == 0) {
                idle_cv_.notify_all();
            }
        }
    }
}

}
