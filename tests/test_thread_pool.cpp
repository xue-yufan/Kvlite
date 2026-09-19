#include <kvlite/thread_pool.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {

using kvlite::ThreadPool;

TEST(ThreadPool, EnqueueAndExecute) {
    ThreadPool pool(4);
    std::atomic<int> counter{0};

    for (int i = 0; i < 100; ++i) {
        pool.enqueue([&counter]() {
            ++counter;
        });
    }

    pool.wait_idle();
    EXPECT_EQ(counter.load(), 100);
    pool.shutdown();
}

TEST(ThreadPool, WaitIdleReturnsWhenDone) {
    ThreadPool pool(4);
    std::atomic<int> counter{0};

    for (int i = 0; i < 10; ++i) {
    pool.enqueue([&counter]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ++counter;
    });
    }

    pool.wait_idle();
    EXPECT_EQ(counter.load(), 10);
    pool.shutdown();
}

TEST(ThreadPool, EmplyPoolWaitIdleReturnsImmediately) {
    ThreadPool pool(4);
    // 没有任务时 wait_idle() 函数应该立即返回
    pool.wait_idle();
    SUCCEED();
    pool.shutdown();
}

TEST(ThreadPool, ShutdownWaitsForPendingTasks) {
    ThreadPool pool(2);
    std::atomic<int> counter{0};

    for (int i = 0; i < 20; ++i) {
        pool.enqueue([&counter]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            ++counter;
        });
    }

    pool.shutdown();
    EXPECT_EQ(counter.load(), 20);
}

TEST(ThreadPool, EnqueueAfterShutdownThrows) {
    ThreadPool pool(2);
    pool.shutdown();
    EXPECT_THROW(pool.enqueue([]() {}), std::runtime_error);
}

TEST(ThreadPool, DoubleShutdownIsSafe) {
    ThreadPool pool(2);
    pool.shutdown();
    EXPECT_NO_THROW(pool.shutdown());
}

TEST(ThreadPool, ActuallyRunsInParallel) {
    // 如果只有一个线程，4 个 100ms 的任务需要 400ms
    // 如果有 4 个线程，需要大约 100ms
    ThreadPool pool(4);

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < 4; ++i) {
        pool.enqueue([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        });
    }

    pool.wait_idle();
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // 4 个任务在 4 个线程上并行，总耗时应该远小于 400ms
    // 留出足够余量，只断言 < 300ms
    EXPECT_LT(ms, 300);
    pool.shutdown();
}

TEST(ThreadPool, ConcurrentCounterIncrement) {
    ThreadPool pool(8);
    std::atomic<int> counter{0};
    constexpr int kTasks = 1000;

    for (int i = 0; i < kTasks; ++i) {
        pool.enqueue([&counter]() { 
            ++counter; 
        });
    }

    pool.wait_idle();
    EXPECT_EQ(counter.load(), kTasks);
    pool.shutdown();
}

TEST(ThreadPool, AcceptsMoveOnlyCallable) {
    // 用 unique_ptr 构造一个 move-only lambda
    ThreadPool pool(2);
    std::atomic<bool> executed{false};

    auto ptr = std::make_unique<int>(42);
    pool.enqueue([p = std::move(ptr), &executed]() {
        EXPECT_EQ(*p, 42);
        executed = true;
    });

    pool.wait_idle();
    EXPECT_TRUE(executed);
    pool.shutdown();
}

TEST(ThreadPool, TaskExceptionDoesNotCrashPool) {
    ThreadPool pool(2);
    std::atomic<int> counter{0};

    pool.enqueue([]() {
        throw std::runtime_error("task failed");
    });

    pool.enqueue([&counter]() { 
        ++counter; 
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    pool.shutdown();
}

}