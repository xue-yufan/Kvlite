#include "kvlite/protocol.h"

namespace {

enum class ParseStatus {
    Complete,
    Incomplete,
    Malformed
};

struct ParseOutcome {
    ParseStatus status;
    std::vector<std::string> args;
    std::size_t consumed = 0;
};

// 从 pos 开始找 "\r\n"，返回 '\r' 的位置；找不到返回 npos
std::size_t find_crlf(const std::string& buf, std::size_t pos) {
    return buf.find("\r\n", pos);
}

// 把 [start, end) 解析为非负整数。遇到非数字字符返回 nullopt
std::optional<long long> parse_long(const std::string& buf, std::size_t start, std::size_t end) {
    if (start >= end) {
        return std::nullopt;
    }
    long long value = 0;
    for (std::size_t i = start; i < end; ++i) {
        char c = buf[i];
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * 10 + (c - '0');
    }
    return value;
}

ParseOutcome parse_command_internal(const std::string& buffer) {
    if (buffer.empty()) {
        return {ParseStatus::Incomplete};
    }
    if (buffer[0] != '*') {
        return {ParseStatus::Malformed};
    }

    std::size_t pos = 0;

    // 解析 *N
    std::size_t crlf = find_crlf(buffer, pos);
    if (crlf == std::string::npos) {
        return {ParseStatus::Incomplete};
    }

    auto count = parse_long(buffer, pos + 1, crlf);
    if (!count || *count < 0) {
        return {ParseStatus::Malformed};
    }

    pos = crlf + 2;

    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(*count));

    for (long long i = 0; i < *count; ++i) {
        // 每个元素必须以 '$' 开头
        if (pos >= buffer.size()) {
            return {ParseStatus::Incomplete};
        }

        if (buffer[pos] != '$') {
            return {ParseStatus::Malformed};
        }

        std::size_t header_crlf = find_crlf(buffer, pos);
        if (header_crlf == std::string::npos) {
            return {ParseStatus::Incomplete};
        }

        auto len = parse_long(buffer, pos + 1, header_crlf);
        if (!len || *len < 0) {
            return {ParseStatus::Malformed};
        }

        pos = header_crlf + 2;

        // 检查数据段和末尾的 \r\n 是否都在 buffer 里
        auto len_sz = static_cast<std::size_t>(*len);
        if (pos + len_sz + 2 > buffer.size()) {
            return {ParseStatus::Incomplete};
        }
        if (buffer[pos + len_sz] != '\r' || buffer[pos + len_sz + 1] != '\n') {
            return {ParseStatus::Malformed};
        }

        args.emplace_back(buffer, pos, len_sz);
        pos += len_sz + 2;
    }
    
    return {ParseStatus::Complete, std::move(args), pos};
}

}

namespace kvlite {

// 把命令编码为 RESP 数组
std::string encode_command(const std::vector<std::string>& args) {
    std::string out;
    out.reserve(64);

    out += "*";
    out += std::to_string(args.size());
    out += "\r\n";

    for (const auto& arg : args) {
        out += "$";
        out += std::to_string(arg.size());
        out += "\r\n";
        out += arg;
        out += "\r\n";
    }

    return out;
} 

// 把响应编码为 RESP
std::string encode_response(const Response& response) {
    switch (response.type) {
        case Response::Type::Simple:
            return "+" + response.value + "\r\n";
        case Response::Type::Error:
            return "-" + response.value + "\r\n";
        case Response::Type::Integer:
            return ":" + std::to_string(response.integer) + "\r\n";
        case Response::Type::Bulk:
            return "$" + std::to_string(response.value.size()) + "\r\n" + response.value + "\r\n";
        case Response::Type::Nil:
            return "$-1\r\n";
    }
    return "";
}

// 尝试从 buffer 中解析一条完整命令。数据不足返回 nullopt，不消耗 buffer
std::optional<std::vector<std::string>> try_parse_command(std::string& buffer) {
    auto r = parse_command_internal(buffer);
    if (r.status != ParseStatus::Complete) {
        return std::nullopt;
    }
    buffer.erase(0, r.consumed);
    return r.args;
}

// 判断是否等待连接
bool is_command_prefix(const std::string& buffer) {
    auto r = parse_command_internal(buffer);
    return r.status != ParseStatus::Malformed;
}

// 尝试从 buffer 中解析一个响应。数据不足返回 nullopt，不消耗 buffer
std::optional<Response> try_parse_response(std::string& buffer) {
    if (buffer.empty()) {
        return std::nullopt;
    }

    char type = buffer[0];
    std::size_t crlf = find_crlf(buffer, 1);
    if (crlf == std::string::npos) {
        return std::nullopt;
    }

    switch (type) {
        case '+': {
            Response response;
            response.type = Response::Type::Simple;
            response.value = buffer.substr(1, crlf - 1);
            buffer.erase(0, crlf + 2);
            return response;
        }
        case '-': {
            Response response;
            response.type = Response::Type::Error;
            response.value = buffer.substr(1, crlf - 1);
            buffer.erase(0, crlf + 2);
            return response;
        }
        case ':': {
            auto parse_num = parse_long(buffer, 1, crlf);
            if (!parse_num) {
                return std::nullopt;
            }
            Response response;
            response.type = Response::Type::Integer;
            response.integer = *parse_num;
            buffer.erase(0, crlf + 2);
            return response;
        }
        case '$': {
            // 特殊情况：$-1\r\n 表示 Nil
            if (crlf == 3 && buffer.compare(1, 2, "-1") == 0) {
                Response response;
                response.type = Response::Type::Nil;
                buffer.erase(0, crlf + 2);
                return response;
            }

            auto parse_len = parse_long(buffer, 1, crlf);
            if (!parse_len || *parse_len < 0) {
                return std::nullopt;
            }

            auto len_sz = static_cast<std::size_t>(*parse_len);
            std::size_t data_start = crlf + 2;
            if (data_start + len_sz + 2 > buffer.size()) {
                return std::nullopt;
            }
            if (buffer[data_start + len_sz] != '\r' || buffer[data_start + len_sz + 1] != '\n') {
                return std::nullopt;
            }

            Response response;
            response.type = Response::Type::Bulk;
            response.value = buffer.substr(data_start, len_sz);
            buffer.erase(0, data_start + len_sz + 2);
            return response;
        }
        default: {
            return std::nullopt;
        }
    }
}

}
