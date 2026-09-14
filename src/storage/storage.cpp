#include "kvlite/storage.h"

namespace kvlite {

void Storage::set(const std::string& key, const std::string& value) {
    data_[key] = value;
}

std::optional<std::string> Storage::get(const std::string& key) const {
    auto it = data_.find(key);
    if (it == data_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool Storage::del(const std::string& key) {
    return data_.erase(key) > 0;
}

}