#include "kvlite/aof.h"
#include "kvlite/protocol.h"

#include <stdexcept>

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
    std::ifstream in(path_, std::ios::binary); 
    if (!in) {
        // 文件不存在或无法打开，则视为空 Aof
        return 0;
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
        throw std::runtime_error("AOF file corrupted: trailing bytes");
    }

    return count;
}

}