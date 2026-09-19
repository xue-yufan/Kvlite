#include "kvlite/net.h"
#include "kvlite/thread_pool.h"
#include "kvlite/logger.h"

#include <asio.hpp>

#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <sys/time.h>
#endif

namespace {

using asio::ip::tcp;

// 客户端等待一次响应的总超时；接收超时只作为轮询粒度，用来周期性检查总超时
constexpr int kResponseTimeoutMs = 5000;
constexpr int kReceivePollMs = 500;

// 设置接收超时，让空闲连接能周期性检查停机标志。失败返回 false
bool set_receive_timeout(tcp::socket& socket, int milliseconds) {
    #if defined(_WIN32)
        DWORD timeout = static_cast<DWORD>(milliseconds);
        return setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0;
    #else
        struct timeval tv;
        tv.tv_sec = milliseconds / 1000;
        tv.tv_usec = (milliseconds % 1000) * 1000;
        return setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
    #endif
}

std::size_t default_thread_count() {
    auto n = std::thread::hardware_concurrency();
    return n == 0 ? 4 : n; 
}

// 把 error_code 的文本转成 UTF-8。
// Windows 上 ec.message() 返回的是当前代码页（简中系统为 GBK）编码的系统错误文本，
// 直接拼进 UTF-8 日志会变成乱码，所以这里做一次转换。
std::string utf8_error_message(const asio::error_code& ec) {
#if defined(_WIN32)
    const std::string localized = ec.message();
    if (localized.empty()) {
        return {};
    }

    const int wide_len = MultiByteToWideChar(
        CP_ACP, 0, localized.data(), static_cast<int>(localized.size()), nullptr, 0);
    if (wide_len <= 0) {
        return localized;
    }

    std::wstring wide(static_cast<std::size_t>(wide_len), L'\0');
    MultiByteToWideChar(
        CP_ACP, 0, localized.data(), static_cast<int>(localized.size()), wide.data(), wide_len);

    const int utf8_len = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), wide_len, nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 0) {
        return localized;
    }

    std::string utf8(static_cast<std::size_t>(utf8_len), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), wide_len, utf8.data(), utf8_len, nullptr, nullptr);
    return utf8;
#else
    return ec.message();
#endif
}

// 取对端地址用于日志；拿不到时返回 "unknown"
std::string peer_description(const tcp::socket& socket) {
    asio::error_code ec;
    auto endpoint = socket.remote_endpoint(ec);
    if (ec) {
        return "unknown";
    }
    return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
}

// 在开头进行白名单校验命令名，实现防御性过滤
bool is_valid_command_name(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (char c : name) {
        auto uc = static_cast<unsigned char>(c);
        if (!isalnum(uc) && c != '_') {
            return false;
        }
    }
    return true;
}

}

namespace kvlite {

using asio::ip::tcp;

class Server::Impl {
public:
    Impl(uint16_t port, Storage& storage, Logger& logger, Aof& aof) 
        :port_(port), 
        storage_(storage), 
        logger_(logger),
        aof_(aof),
        io_(),
        acceptor_(io_, tcp::endpoint(tcp::v4(), port)),
        pool_(default_thread_count(), &logger) {
    }

    void run() {
        logger_.info("server listening on 0.0.0.0:" + std::to_string(port_)
            + " with " + std::to_string(default_thread_count()) + " worker thread(s)");

        // 1. 注册信号处理
        asio::signal_set signals(io_, SIGINT, SIGTERM);
        signals.async_wait([this](const asio::error_code& ec, int) {
            if (!ec) {
                shutdown_requested_ = true;
                asio::error_code ignored;
                // 唤醒阻塞的 accept
                acceptor_.close(ignored);
            }
        });

        // 2. 专用信号线程跑 io_context
        std::thread signal_thread([this]() {
            io_.run();
        });

        // 3. 主线程 accept 循环
        while (!shutdown_requested_) {
            tcp::socket socket(io_);
            asio::error_code ec;
            acceptor_.accept(socket, ec);

            if (ec) {
                if (shutdown_requested_) {
                    break;
                }
                // 其他错误，记录后继续监听
                logger_.warning("accept failed: " + utf8_error_message(ec));
                // fd 耗尽之类的持续错误会在这里空转烧 CPU，退让一下
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            const std::string peer = peer_description(socket);
            logger_.debug("accepted connection from " + peer);

            try {
                pool_.enqueue([this, s = std::move(socket)]() mutable {
                    handle_client(std::move(s));
                });
            } catch (const std::exception& e) {
                // 线程池已停机；丢弃这条连接，但不要让它无声消失
                logger_.warning("dropping connection from " + peer + ": " + e.what());
            }
        }

        // 4. 关闭连接池
        logger_.info("shutdown requested");

        // 等待所有连接处理完
        pool_.wait_idle();  
        // 停止线程池，join 工作线程     
        pool_.shutdown();        

        io_.stop();
        signal_thread.join();

        logger_.info("shutdown complete");
    }

private:
    uint16_t port_;
    Storage& storage_;
    Logger& logger_;
    Aof& aof_;
    asio::io_context io_;
    tcp::acceptor acceptor_;
    ThreadPool pool_;
    std::atomic<bool> shutdown_requested_{false};

    void handle_client(tcp::socket socket) {
        const std::string peer = peer_description(socket);

        if (!set_receive_timeout(socket, 1000)) {
            logger_.warning("failed to set receive timeout for client " + peer);
        }

        std::string buffer;
        char temp[4096];

        for (;;) {
            // 检查停机标志，让空闲连接及时退出
            if (shutdown_requested_) {
                return;
            }

            asio::error_code ec;
            std::size_t bytes_read = socket.read_some(asio::buffer(temp), ec);

            if (ec == asio::error::would_block || ec == asio::error::try_again || ec == asio::error::timed_out) {
                continue;
            }

            if (ec) {
                if (ec == asio::error::eof) {
                    logger_.debug("client " + peer + " closed the connection");
                } else {
                    logger_.warning("read from client " + peer + " failed: " + utf8_error_message(ec));
                }
                return ;
            }

            buffer.append(temp, bytes_read);

            if (!is_command_prefix(buffer)) {
                // 结构错误，回错误并断开
                logger_.warning("protocol error");
                asio::write(socket, asio::buffer(std::string("-ERR Protocol error\r\n")), ec);
                return;
            }

            while (auto cmd = try_parse_command(buffer)) {
                const auto started_at = std::chrono::steady_clock::now();
                Response response = dispatch_command(*cmd);
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started_at).count();

                // 只记命令名，不记 key/value，避免日志被大 value 撑爆
                logger_.debug("client " + peer + " ran " + (*cmd)[0] + " in " + std::to_string(elapsed) + "ms");
                if (elapsed >= 100) {
                    logger_.warning("slow command " + (*cmd)[0] + " from " + peer + " took " + std::to_string(elapsed) + "ms");
                }

                std::string bytes = encode_response(response);
                asio::error_code ew;
                asio::write(socket, asio::buffer(bytes), ew);
                if (ew) {
                    logger_.warning("write to client " + peer + " failed: " + utf8_error_message(ew));
                    return ;
                }
            }
        }
    }

    // 每条命令期望的参数个数（不含命令名本身）。未知命令返回 nullopt
    static std::optional<std::size_t> expected_arg_count(const std::string& name) {
        if (name == "SET") {
            return 2;
        }
        if (name == "GET" || name == "DEL") {
            return 1;
        }
        return std::nullopt;
    }

    Response dispatch_command(const std::vector<std::string>& cmd) {
        if (cmd.empty()) {
            logger_.warning("received an empty command");
            return { Response::Type::Error, "ERR empty command" };
        }

        const std::string& cmd_name = cmd[0];

        if (!is_valid_command_name(cmd_name)) {
            logger_.warning("invalid command name");
            return {Response::Type::Error, "ERR invalid command name"};
        }

        const auto expected = expected_arg_count(cmd_name);
        if (!expected) {
            logger_.warning("unknown command '" + cmd_name + "'");
            return { Response::Type::Error, "ERR unknown command '" + cmd_name + "'" };
        }

        // 参数个数不对时要回专门的消息，不能让客户端以为命令不存在
        if (cmd.size() != *expected + 1) {
            logger_.warning("wrong number of arguments for " + cmd_name + " ("
                + std::to_string(cmd.size() - 1) + " given, "
                + std::to_string(*expected) + " expected)");
            return { Response::Type::Error,
                "ERR wrong number of arguments for '" + cmd_name + "' command" };
        }

        if (cmd_name == "SET") {
            if (!aof_.append(cmd)) {
                logger_.error("AOF append failed for SET");
                return {Response::Type::Error, "ERR persistence failure"};
            }
            storage_.set(cmd[1], cmd[2]);
            return { Response::Type::Simple, "OK" };
        }

        if (cmd_name == "GET") {
            auto value = storage_.get(cmd[1]);
            if (!value) {
                return { Response::Type::Nil, "" };
            }
            return { Response::Type::Bulk, *value };
        }

        if (cmd_name == "DEL") {
            if (!aof_.append(cmd)) {
                logger_.error("AOF append failed for DEL");
                return {Response::Type::Error, "ERR persistence failure"};
            }
            bool del = storage_.del(cmd[1]);
            Response response;
            response.type = Response::Type::Integer;
            response.integer = del ? 1 : 0;
            return response;
        }

        // 理论上到不了：expected_arg_count 认识它，就说明上面必须有分支处理。
        // 留着是为了以后往 expected_arg_count 加命令却忘了加 handler 时能立刻暴露。
        logger_.error("command '" + cmd_name + "' passed the arity check but has no handler");
        return { Response::Type::Error, "ERR internal error" };
    }
};

Server::Server(uint16_t port, Storage& storage, Logger& logger, Aof& aof)
    : impl_(std::make_unique<Impl>(port, storage, logger, aof)) {
}

Server::~Server() = default;

void Server::run() {
    impl_->run();
}

class Client::Impl {
public:
    Impl(const std::string& host, uint16_t port)
        : socket_(io_) {
        tcp::resolver resolver(io_);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        asio::connect(socket_, endpoints);

        // 让 read_some 定期返回，好在 execute() 里检查总超时；设不上就宁可失败，
        // 否则"不会永久挂住"这个承诺就没了
        if (!set_receive_timeout(socket_, kReceivePollMs)) {
            throw std::runtime_error("failed to set receive timeout on client socket");
        }
    }

    Response execute(const std::vector<std::string>& command) {
        std::string bytes = encode_command(command);
        asio::write(socket_, asio::buffer(bytes));

        std::string buffer;
        char temp[4096];
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(kResponseTimeoutMs);

        for (;;) {
            if (auto response = try_parse_response(buffer)) {
                return *response;
            }

            if (std::chrono::steady_clock::now() >= deadline) {
                throw std::runtime_error("timed out waiting for a response from the server");
            }

            asio::error_code ec;
            std::size_t bytes_read = socket_.read_some(asio::buffer(temp), ec);

            if (ec == asio::error::would_block || ec == asio::error::try_again || ec == asio::error::timed_out) {
                // 单次接收超时是正常的轮询节拍，回到循环顶部重新判断总超时
                continue;
            }
            if (ec) {
                throw std::runtime_error("connection closed while reading response");
            }
            buffer.append(temp, bytes_read);
        }
    }

private:
    asio::io_context io_;
    tcp::socket socket_;
};

Client::Client(const std::string& host, uint16_t port)
    : impl_(std::make_unique<Impl>(host, port)) {
}

Client::~Client() = default;

Response Client::execute(const std::vector<std::string>& command) {
    return impl_->execute(command);
}

}
