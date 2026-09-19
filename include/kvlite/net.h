#pragma once
// net.h —— 网络层接口
//
// 设计原则：
//   - 公共头文件只暴露标准库类型，不暴露 Asio
//   - Server 和 Client 不可拷贝、不可移动
//   - Asio 类型全部藏在 Impl 类里，保持 kv_core 的 PRIVATE 依赖语义

#include <kvlite/protocol.h>
#include <kvlite/storage.h>
#include <kvlite/logger.h>
#include <kvlite/aof.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kvlite {

// 同步阻塞式 KV 服务器。构造函数创建监听 socket，run() 进入 accept 循环
class Server {
public:
    // 创建监听 0.0.0.0:port 的服务器。logger 由调用方持有，生命周期必须覆盖 Server
    Server(uint16_t port, Storage& storage, Logger& logger, Aof& aof);
    ~Server();

    // 禁止用另一个 Server 构造新对象
    Server(const Server&) = delete;

    // 禁止把另一个 Server 赋给已存在的对象
    Server& operator=(const Server&) = delete;

    // 禁止用另一个即将销毁的 Server 构造新对象
    Server(Server&&) = delete;

    // 禁止把另一个即将销毁的 Server 赋给已存在的对象
    Server& operator=(Server&&) = delete;

    // 进入 accept 循环，阻塞直到进程被终止（SIGINT / SIGTERM）。
    void run();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class Client {
public:
    // 连接到 host:port。失败时抛 std::system_error
    Client(const std::string& host, uint16_t port);
    ~Client();

    Client(const Client&) = delete;

    Client& operator=(const Client&) = delete;

    Client(Client&&) = delete;
    
    Client& operator=(Client&&) = delete;

    // 发送一条命令并等待响应。默认 5 秒不回应则抛 std::runtime_error（不会永久阻塞）
    Response execute(const std::vector<std::string>& command);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}
