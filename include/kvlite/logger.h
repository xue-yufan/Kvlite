#pragma once

#include <string>

namespace kvlite {

class Logger {
public:
    enum class Level {
        Debug,
        Info,
        Warning,
        Error
    };

    virtual ~Logger() = default;

    virtual void log(Level level, const std::string& message) = 0;

    void debug(const std::string& msg) {
        log(Level::Debug, msg);
    }

    void info(const std::string& msg) {
        log(Level::Info, msg);
    }

    void warning(const std::string& msg) {
        log(Level::Warning, msg);
    }

    void error(const std::string& msg) {
        log(Level::Error, msg);
    }
};

}