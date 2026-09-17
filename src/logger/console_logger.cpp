#include <kvlite/console_logger.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace kvlite {

    namespace {

    const char* level_name(Logger::Level level) {
        switch (level) {
            case Logger::Level::Debug:
                return "DEBUG";
            case Logger::Level::Info: 
                return "INFO";
            case Logger::Level::Warning: 
                return "WARNING";
            case Logger::Level::Error:
                return "ERROR";
        }
        return "UNKNOWN";
    }

    std::string format_timestamp() {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
        auto t = system_clock::to_time_t(now);

        std::tm tm_buf;
        #if defined(_WIN32)
            localtime_s(&tm_buf, &t);
        #else
            localtime_r(&t, &tm_buf);
        #endif

        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

    }

    ConsoleLogger::ConsoleLogger(Level min_level) : min_level_(min_level) {
    }

    void ConsoleLogger::log(Level level, const std::string& message) {
        if (level < min_level_) {
            return;
        }

        std::ostringstream oss;
        oss << '[' << format_timestamp() << ']'
            << " [" << level_name(level) << ']'
            << " [tid=" << std::this_thread::get_id() << ']'
            << ' ' << message << '\n';

        std::lock_guard<std::mutex> lock(mutex_);
        std::cerr << oss.str();
    }

}