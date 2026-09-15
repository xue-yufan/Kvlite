#include "kvlite/storage.h"

#include <gtest/gtest.h>

namespace {

using kvlite::Storage;

TEST(Storage, SetAndGet) {
    Storage storage;
    storage.set("foo", "bar");
    EXPECT_EQ(storage.get("foo"), "bar");
}

TEST(Storage, GetMissing) {
    Storage storage;
    EXPECT_EQ(storage.get("nonexistent"), std::nullopt);
}

TEST(Storage, Overwrite) {
    Storage storage;
    storage.set("foo", "bar");
    storage.set("foo", "baz");
    EXPECT_EQ(storage.get("foo"), "baz");
}

TEST(Storage, Delete) {
    Storage storage;
    storage.set("foo", "bar");
    EXPECT_TRUE(storage.del("foo"));
    EXPECT_EQ(storage.get("foo"), std::nullopt);
}

TEST(Storage, DeleteMissing) {
    Storage storage;
    EXPECT_FALSE(storage.del("nonexisteant"));
}

TEST(Storage, EmptyValue) {
    // 关键用例：区分"key 存在但值为空"和"key 不存在"
    Storage storage;
    storage.set("foo", "");
    auto value = storage.get("foo");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "");
}

TEST(Storage, EmptyKey) {
    Storage storage;
    storage.set("", "value");
    EXPECT_EQ(storage.get(""), "value");
}

TEST(Storage, BinaryValue) {
    // 值里包含 \0，验证 std::string 的二进制安全性
    Storage storage;
    std::string value("a\0b", 3);
    storage.set("k", value);
    auto get_value = storage.get("k");
    ASSERT_TRUE(get_value.has_value());
    EXPECT_EQ(get_value->size(), 3u);
    EXPECT_EQ(*get_value, value);
}

}