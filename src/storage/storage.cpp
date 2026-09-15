#include "kvlite/storage.h"

namespace kvlite {

void Storage::set(const std::string& key, const std::string& value) {
    std::unique_lock lock(mutex_);
    data_[key] = value;
}

std::optional<std::string> Storage::get(const std::string& key) const {
    std::shared_lock lock(mutex_);
    auto it = data_.find(key);
    if (it == data_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool Storage::del(const std::string& key) {
    std::unique_lock lock(mutex_);
    return data_.erase(key) > 0;
}

}