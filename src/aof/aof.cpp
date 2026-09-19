#include "kvlite/aof.h"
#include "kvlite/protocol.h"

#include <stdexcept>
#include <filesystem>

namespace kvlite {

Aof::Aof(const std::string& path)
        :path_(path),
        file_(path, std::ios::binary | std::ios::app) {
    if (!file_) {
        throw std::runtime_error("failed to open path: " + path_);
    }
}

Aof::~Aof() = default;

bool Aof::append(const std::vector<std::string>& command) {
    // 空命令不写
    if (command.empty()) {
        return true;
    }

    std::string bytes = encode_command(command);

    std::lock_guard<std::mutex> lock(mutex_);
    file_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file_.flush();
    return file_.good();
}

std::size_t Aof::replay(const std::function<void(const std::vector<std::string>&)>& callback) {
    std::error_code ec;
    auto status = std::filesystem::status(path_, ec);

    if (ec) {
        throw std::runtime_error("cannot stat AOF file: " + ec.message());
    }

    if (status.type() == std::filesystem::file_type::not_found) {
        // 文件不存在：首次启动，正常
        return 0;   
    }

    if (!std::filesystem::is_regular_file(status)) {
        throw std::runtime_error("AOF path is not a regular file: " + path_);
    }

    std::ifstream in(path_, std::ios::binary); 
    if (!in) {
        throw std::runtime_error("failed to open AOF file for replay: " + path_);
    }

    std::string buffer;
    char temp[4096];
    std::size_t count = 0;

    while (in) {
        in.read(temp, sizeof(temp));
        buffer.append(temp, static_cast<std::size_t>(in.gcount()));

        while (auto cmd = try_parse_command(buffer)) {
            callback(*cmd);
            ++count;
        }
    }

    // 若文件读完 buffer 还有残留，则说明最后命令不完整
    if (!buffer.empty()) {
        if (is_command_prefix(buffer)) {
            // 尾部半条命令：崩溃留下的，正常丢弃
            // v4 里先静默丢弃，v5 可以引入 ReplayReport 记入日志
        } else {
            // 真正的结构错误
            throw std::runtime_error("AOF file corrupted: malformed command");
        }
    }

    return count;
}

}