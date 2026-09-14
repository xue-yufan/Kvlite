#include "kvlite/protocol.h"
#include "kvlite/net.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

namespace {

struct Options {
    std::string host = "127.0.0.1";
    uint16_t port = 6381;
    std::vector<std::string> command;
};

void print_usage() {
    std::cout << "Usage: kv_cli [--host HOST] [--port PORT] COMMAND [ARGS...]\n"
            << "  --host HOST   server host (default 127.0.0.1)\n"
            << "  --port PORT   server port (default 6381)\n"
            << "\n"
            << "Examples:\n"
            << "  kv_cli SET foo bar\n"
            << "  kv_cli GET foo\n"
            << "  kv_cli DEL foo\n";
}

Options parse_args(int argc, char** argv) {
    Options options;
    int i = 1;
    for (; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--host requires a value");
            }
            options.host = argv[++i];
        } else if (arg == "--port") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--port requires a value");
            }
            int port = std::stoi(argv[++i]);
            if (port <= 0 || port > 65535) {
                throw std::runtime_error("port out of range: " + arg);
            }
            options.port = static_cast<uint16_t>(port);
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            std::exit(0);
        } else {
            break;
        }
    }
    options.command.assign(argv + i, argv + argc);
    return options;
}

int print_response(const kvlite::Response& response) {
    switch (response.type) {
        case kvlite::Response::Type::Simple:
            std::cout << response.value << "\n";
            return 0;
        case kvlite::Response::Type::Bulk:
            std::cout << response.value << "\n";
            return 0;
        case kvlite::Response::Type::Nil:
            std::cout << "(nil)\n";
            return 0;
        case kvlite::Response::Type::Integer:
            std::cout << "(integer) " << response.integer << "\n";
            return 0;
        case kvlite::Response::Type::Error:
            std::cerr << "(error) " << response.value << "\n";
            return 1;
    }
    return 1;
}

}

int main(int argc, char** argv) {
    Options options;
    try {
        options = parse_args(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    if (options.command.empty()) {
        print_usage();
        return 1;
    }

    try {
        kvlite::Client client(options.host, options.port);
        auto response = client.execute(options.command);
        return print_response(response);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
