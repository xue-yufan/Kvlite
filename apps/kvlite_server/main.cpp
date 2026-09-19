#include "kvlite/storage.h"
#include "kvlite/net.h"
#include "kvlite/console_logger.h"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    uint16_t port = 6381;
    std::string aof_path = "aof/kvlite.aof";
};

Options parse_args(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--port requires a value");
            }
            int port = std::stoi(argv[++i]);
            if (port < 0 || port > 65535) {
                throw std::runtime_error("port out of range: " + std::to_string(port));
            }
            options.port = static_cast<uint16_t>(port);
        } else if (arg == "--aof-path") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--aof-path requires a value");
            }
            options.aof_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: kvlite_server [--port PORT] [--aof-path PATH]\n"
                    << "  --port PORT      listening port (default 6381)\n"
                    << "  --aof-path PATH  AOF file path (default kvlite.aof)\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown arg: " + arg);
        }
    }
    return options;
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

    try {
        kvlite::Storage storage;
        kvlite::ConsoleLogger logger;
        kvlite::Aof aof(options.aof_path);

        std::size_t replayed = aof.replay([&storage](const std::vector<std::string>& cmd) {
            if (cmd.size() == 3 && cmd[0] == "SET") {
                storage.set(cmd[1], cmd[2]);
            } else if (cmd.size() == 2 && cmd[0] == "DEL") {
                storage.del(cmd[1]);
            }
        });

        logger.info("replayed " + std::to_string(replayed) + " commands");

        kvlite::Server server(options.port, storage, logger, aof);

        server.run();
        logger.info("server stopped");
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
