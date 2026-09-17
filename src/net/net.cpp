#include "kvlite/net.h"
#include "kvlite/thread_pool.h"

#include <asio.hpp>

#include <iostream>
#include <stdexcept>
#include <thread>
#include <csignal>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <sys/time.h>
#endif

namespace {

using asio::ip::tcp;

void set_receive_timeout(tcp::socket& socket, int milliseconds) {
    #if defined(_WIN32)
        DWORD timeout = static_cast<DWORD>(milliseconds);
        setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    #else
        struct timeval tv;
        tv.tv_sec = milliseconds / 1000;
        tv.tv_usec = (milliseconds % 1000) * 1000;
        setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    #endif
}

std::size_t default_thread_count() {
    auto n = std::thread::hardware_concurrency();
    return n == 0 ? 4 : n; 
}

}

namespace kvlite {

using asio::ip::tcp;

class Server::Impl {
public:
    Impl(uint16_t port, Storage& storage) 
        :port_(port), 
        storage_(storage), 
        pool_(default_thread_count()),
        acceptor_(io_, tcp::endpoint(tcp::v4(), port)) {
    }

    void run() {
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
                // 其他错误，继续监听
                continue;
            }

            pool_.enqueue([this, s = std::move(socket)]() mutable {
                handle_client(std::move(s));
            });
        }

        // 4. 关闭连接池
        std::cout << "shutting down, waiting for tasks...\n";

        // 等待所有连接处理完
        pool_.wait_idle();  
        // 停止线程池，join 工作线程     
        pool_.shutdown();        

        io_.stop();
        signal_thread.join();

        std::cout << "shutdown complete\n";
    }

private:
    uint16_t port_;
    Storage& storage_;
    asio::io_context io_;
    tcp::acceptor acceptor_;
    ThreadPool pool_;
    std::atomic<bool> shutdown_requested_{false};

    void handle_client(tcp::socket socket) {
        set_receive_timeout(socket, 1000);

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
                return ;
            }

            buffer.append(temp, bytes_read);

            while (auto cmd = try_parse_command(buffer)) {
                Response response = dispatch_command(*cmd);
                std::string bytes = encode_response(response);
                asio::error_code ew;
                asio::write(socket, asio::buffer(bytes), ew);
                if (ew) {
                    return ;
                }
            }
        }
    }

    Response dispatch_command(const std::vector<std::string>& cmd) {
        if (cmd.empty()) {
            return { Response::Type::Error, "ERR empty command" };
        }

        const std::string& cmd_name = cmd[0];

        if (cmd_name == "SET" && cmd.size() == 3) {
            storage_.set(cmd[1], cmd[2]);
            return { Response::Type::Simple, "OK" };
        }

        if (cmd_name == "GET" && cmd.size() == 2) {
            auto value = storage_.get(cmd[1]);
            if (!value) {
                return { Response::Type::Nil, "" };
            }
            return { Response::Type::Bulk, *value };
        }

        if (cmd_name == "DEL" && cmd.size() == 2) {
            bool del = storage_.del(cmd[1]);
            Response response;
            response.type = Response::Type::Integer;
            response.integer = del ? 1 : 0;
            return response;
        }

        return { Response::Type::Error, "ERR unknown command '" + cmd_name + "'" };
    }
};

Server::Server(uint16_t port, Storage& storage)
    : impl_(std::make_unique<Impl>(port, storage)) {
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
    }

    Response execute(const std::vector<std::string>& command) {
        std::string bytes = encode_command(command);
        asio::write(socket_, asio::buffer(bytes));

        std::string buffer;
        char temp[4096];

        for (;;) {
            auto response = try_parse_response(buffer);
            if (response) {
                return *response;
            }

            asio::error_code ec;
            std::size_t bytes_read = socket_.read_some(asio::buffer(temp), ec);
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