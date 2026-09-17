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
    std::atomic<int> count{0};

    for (int i = 0; i < 100; ++i) {
        pool.enqueue([&count]() {
            ++count;
        });
    }

    pool.wait_idle();
    EXPECT_EQ(count, 100);
    pool.shutdown();
}

}