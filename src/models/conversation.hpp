#pragma once

#include "message.hpp"
#include <vector>
#include <map>
#include <memory>

struct MemoryFile {
    std::string name;
    std::string content;
    std::string description;
    int64_t created = 0;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MemoryFile, name, content, description, created)
