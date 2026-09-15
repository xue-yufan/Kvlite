#include "kvlite/storage.h"

#include "gtest/gtest.h"

#include <string>
#include <thread>
#include <atomic>

namespace {

using kvlite::Storage;

// 多个线程并发读，所有读取必须成功
TEST(StorageConcurrent, ConcurrentRead) {
    Storage storage;
    constexpr int kKeys = 100;
    constexpr int kThreads = 10;

    for (int i = 0; i < kKeys; ++i) {
        storage.set("key" + std::to_string(i), "value" + std::to_string(i));
    }

    std::atomic<int> success{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&storage, &success]() {
            for (int i = 0; i < kKeys; ++i) {
                auto value = storage.get("key" + std::to_string(i));
                if (value && *value == "value" + std::to_string(i)) {
                    ++success;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(success.load(), kKeys * kThreads);
}

// 多个线程写不同的 key，最终所有 key 都正确
TEST(StorageConcurrent, ConcurrentWrites) {
    Storage storage;
    constexpr int kKeys = 100;
    constexpr int kThreads = 8;

    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&storage, t]() {
            for (int i = 0; i < kKeys; ++i) {
                storage.set("t" + std::to_string(t) + "_key" + std::to_string(i),
                      std::to_string(i));
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    for (int t = 0; t < kThreads; ++t) {
        for(int i = 0; i < kKeys; ++i) {
            auto value = storage.get("t" + std::to_string(t) + "_key" + std::to_string(i));
            ASSERT_TRUE(value.has_value()) << "t" << t << " key" << i;
            EXPECT_EQ(*value, std::to_string(i));
        }
    }
}

TEST(StorageConcurrent, MixedReadWrite) {
    Storage storage;
    constexpr int kKeys = 100;
    constexpr int kWriters = 4;
    constexpr int kReaders = 4;

    for (int i = 0; i < kKeys; ++i) {
        storage.set("key" + std::to_string(i), "0");
    }

    std::vector<std::thread> threads;

    for (int t = 0; t < kWriters; ++t) {
        threads.emplace_back([&storage, t]() {
            for (int round = 0; round < 100; ++round) {
                for (int i = 0; i < kKeys; ++i) {
                    storage.set("key" + std::to_string(i), std::to_string(t));
                }
            }
        });
    }

    for (int t = 0; t < kReaders; ++t) {
        threads.emplace_back([&storage]() {
            for (int round = 0; round < 100; ++round) {
                for (int i = 0; i < kKeys; ++i) {
                    auto value = storage.get("key" + std::to_string(i));
                    (void)value;  // 只验证不崩溃
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }
    SUCCEED();
}

// 同一个 key 被多个线程反复覆盖，最终值是某个写入过的值
TEST(StorageConcurrent, SameKeyContention) {
    Storage storage;
    constexpr int kThreads = 8;
    constexpr int kRounds = 1000;

    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&storage, t]() {
            for (int i = 0; i < kRounds; ++i) {
                storage.set("shared", std::to_string(t));
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto value = storage.get("shared");
    ASSERT_TRUE(value.has_value());
    int get_value = std::stoi(*value);
    EXPECT_GE(get_value, 0);
    EXPECT_LT(get_value, kThreads);
}

}