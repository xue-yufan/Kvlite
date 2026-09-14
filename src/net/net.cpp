#include "kvlite/net.h"

#include <asio.hpp>

#include <iostream>
#include <stdexcept>
#include <thread>
#include <mutex>

namespace kvlite {

using asio::ip::tcp;

class Server::Impl {
public:
    Impl(uint16_t port, Storage& storage) 
        :port_(port), storage_(storage), acceptor_(io_, tcp::endpoint(tcp::v4(), port)){
    }

    void run() {
        for (;;) {
            tcp::socket socket(io_);
            acceptor_.accept(socket);
            std::thread([this, s = std::move(socket)]() mutable {
                handle_client(std::move(s));
            }).detach();
        }
    }

private:
    uint16_t port_;
    Storage& storage_;
    asio::io_context io_;
    tcp::acceptor acceptor_;
    std::mutex mutex_;

    void handle_client(tcp::socket socket) {
        std::string buffer;
        char temp[4096];

        for (;;) {
            asio::error_code error_code;
            std::size_t bytes_read = socket.read_some(asio::buffer(temp), error_code);
            if (error_code) {
                return ;
            }

            buffer.append(temp, bytes_read);

            while (auto cmd = try_parse_command(buffer)) {
                Response response = dispatch_command(*cmd);
                std::string bytes = encode_response(response);
                asio::error_code error_writer;
                asio::write(socket, asio::buffer(bytes), error_writer);
                if (error_writer) {
                    return ;
                }
            }
        }
    }

    Response dispatch_command(const std::vector<std::string>& cmd) {
        std::lock_guard<std::mutex> lock(mutex_);

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
        : socket_(io_context_) {
        tcp::resolver resolver(io_context_);
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

            asio::error_code error_code;
            std::size_t bytes_read = socket_.read_some(asio::buffer(temp), error_code);
            if (error_code) {
                throw std::runtime_error("connection closed while reading response");
            }
            buffer.append(temp, bytes_read);
        }
    }

private:
    asio::io_context io_context_;
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