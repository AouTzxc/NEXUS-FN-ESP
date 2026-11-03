#pragma once

#include "rebootnova/settings.h"

#include <string>
#include <utility>

namespace rebootnova {

class ConfigStore {
public:
    explicit ConfigStore(std::string path) : path_(std::move(path)) {}
    bool save(const Settings& settings, std::string* error = nullptr) const;
    bool load(Settings& settings, std::string* error = nullptr) const;
    bool reset(Settings& settings, std::string* error = nullptr) const;
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

}  // namespace rebootnova
