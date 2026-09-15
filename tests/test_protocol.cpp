#include <kvlite/protocol.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using kvlite::Response;
using kvlite::encode_command;
using kvlite::encode_response;
using kvlite::try_parse_command;
using kvlite::try_parse_response;

// encode_command
TEST(EncodeCommand, Simple) {
    EXPECT_EQ(encode_command({"Get", "foo"}),
              "*2\r\n$3\r\nGet\r\n$3\r\nfoo\r\n");
}

TEST(EncodeCommand, SetThreeArgs) {
    EXPECT_EQ(encode_command({"Set", "foo", "bar"}),
              "*3\r\n$3\r\nSet\r\n$3\r\nfoo\r\n$3\r\nbar\r\n");
}

TEST(EncodeCommand, EmptyValue) {
    EXPECT_EQ(encode_command({"SET", "k", ""}),
              "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$0\r\n\r\n");
}

// try_parse_command
TEST(ParseCommand, Complete) {
    std::string buffer = "*2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n";
    auto response = try_parse_command(buffer);
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(*response, std::vector<std::string>({"GET", "foo"}));
    EXPECT_TRUE(buffer.empty());
}

TEST(ParseCommand, Incomplete) {
    // 数据不足时必须返回 nullopt，且 buffer 不被修改
    std::string buffer = "*2\r\n$3\r\nGE";
    auto response = try_parse_command(buffer);
    EXPECT_FALSE(response.has_value());
    EXPECT_EQ(buffer, "*2\r\n$3\r\nGE");
}

TEST(ParseCommand, TwoCommands) {
    std::string buffer = "*1\r\n$4\r\nPING\r\n*1\r\n$4\r\nPING\r\n";
    auto first = try_parse_command(buffer);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, std::vector<std::string>({"PING"}));
    EXPECT_EQ(buffer, "*1\r\n$4\r\nPING\r\n");

    auto second = try_parse_command(buffer);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, std::vector<std::string>({"PING"}));
    EXPECT_TRUE(buffer.empty());
}

TEST(ParseCommand, BinarySafe) {
    std::string payload = "a\r\nb";
    std::string buffer = "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$4\r\n" + payload + "\r\n";

    auto response = try_parse_command(buffer);
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(*response, std::vector<std::string>({"SET", "k", payload}));
    EXPECT_TRUE(buffer.empty());
}

TEST(ParseCommand, EmptyArgs) {
    std::string buffer = "*0\r\n";
    auto response = try_parse_command(buffer);
    ASSERT_TRUE(response.has_value());
    EXPECT_TRUE(response->empty());
    EXPECT_TRUE(buffer.empty());
}

TEST(ParseCommand, RejectNonArray) {
    std::string buffer = "GET foo\r\n";
    auto response = try_parse_command(buffer);
    EXPECT_FALSE(response.has_value());
    EXPECT_EQ(buffer, "GET foo\r\n");
}

// encode_response
TEST(EncodeResponse, Simple) {
    EXPECT_EQ(encode_response({Response::Type::Simple, "OK"}),
              "+OK\r\n");
}

TEST(EncodeResponse, Error) {
    EXPECT_EQ(encode_response({Response::Type::Error, "ERR bad"}),
              "-ERR bad\r\n");
}

TEST(EncodeResponse, Integer) {
    Response response;
    response.type = Response::Type::Integer;
    response.integer = 42;
    EXPECT_EQ(encode_response(response), ":42\r\n");
}

TEST(EncodeResponse, Bulk) {
    EXPECT_EQ(encode_response({Response::Type::Bulk, "bar"}),
              "$3\r\nbar\r\n");
}

TEST(EncodeResponse, Nil) {
    EXPECT_EQ(encode_response({Response::Type::Nil}),
              "$-1\r\n");
}

// try_parse_response
TEST(ParseResponse, Simple) {
    std::string buffer = "+OK\r\n";
    auto response = try_parse_response(buffer);
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->type, Response::Type::Simple);
    EXPECT_EQ(response->value, "OK");
    EXPECT_TRUE(buffer.empty());
}

TEST(ParseResponse, Error) {
    std::string buffer = "-ERR unknown command\r\n";
    auto response = try_parse_response(buffer);
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->type, Response::Type::Error);
    EXPECT_EQ(response->value, "ERR unknown command");
}

TEST(ParseResponse, Integer) {
    std::string buffer = ":42\r\n";
    auto response = try_parse_response(buffer);
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->type, Response::Type::Integer);
    EXPECT_EQ(response->integer, 42);
}

TEST(ParseResponse, Bulk) { 
    std::string buffer = "$3\r\nbar\r\n";
    auto response = try_parse_response(buffer);
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->type, Response::Type::Bulk);
    EXPECT_EQ(response->value, "bar");
}

TEST(ParseResponse, Nil) {
    std::string buffer = "$-1\r\n";
    auto response = try_parse_response(buffer);
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->type, Response::Type::Nil);
}

TEST(ParseResponse, Incomplete) {
    std::string buffer = "$3\r\nba";
    auto response = try_parse_response(buffer);
    EXPECT_FALSE(response.has_value());
    EXPECT_EQ(buffer, "$3\r\nba");
}

}