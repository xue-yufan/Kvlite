#pragma once

#include "kvlite/logger.h"

#include <mutex>

namespace kvlite {

class ConsoleLogger : public Logger {
public:
    explicit ConsoleLogger(Level min_level = Level::Info);

    void log(Level level, const std::string& message);

private:
    Level min_level_;
    std::mutex mutex_;
};

}
