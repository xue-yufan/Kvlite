#pragma once

#include <cstddef>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace kvlite {

class Aof {
public:
    // 打开或创建 AOF 文件。失败抛 std::runtime_error
    explicit Aof(const std::string& path);
    ~Aof();

    Aof(const Aof&) = delete;
    Aof& operator=(const Aof&) = delete;

    // 追加一条命令到 AOF 文件并 flush ，返回 false 表示写入失败（磁盘满、权限等）
    bool append(const std::vector<std::string>& command);

    // 从文件重放所有命令。callback 对每条命令调用一次，返回重放的命令数。文件损坏时抛 std::runtime_error
    std::size_t replay(const std::function<void(const std::vector<std::string>&)>& callback);

private:
    std::string path_;
    std::ofstream file_;
    std::mutex mutex_;
};

}