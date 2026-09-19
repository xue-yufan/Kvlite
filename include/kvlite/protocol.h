#pragma once
#include <string>
#include <optional>
#include <vector>

namespace kvlite {

struct Response {
    enum class Type {
        Simple,
        Bulk,
        Nil,
        Error,
        Integer
    };
    Type type;
    std::string value;
    long long integer;
};

// 把命令编码为 RESP 数组
std::string encode_command(const std::vector<std::string>& args);

// 把响应编码为 RESP
std::string encode_response(const Response& response);

// 尝试从 buffer 中解析一条完整命令。数据不足返回 nullopt，不消耗 buffer
std::optional<std::vector<std::string>> try_parse_command(std::string& buffer);

// 判断是否等待连接
bool is_command_prefix(const std::string& buffer) noexcept;

// 尝试从 buffer 中解析一个响应。数据不足返回 nullopt，不消耗 buffer
std::optional<Response> try_parse_response(std::string& buffer);

}