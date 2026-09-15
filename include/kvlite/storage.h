#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <shared_mutex>

namespace kvlite {

class Storage {
public:
    void set(const std::string& key, const std::string& value);

    std::optional<std::string> get(const std::string& key) const;

    bool del(const std::string& key);

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> data_;
};

}