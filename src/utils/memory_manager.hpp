#pragma once

#include <string>
#include <vector>

struct MemoryFile {
    std::string name;
    std::string content;
    std::string description;
    int64_t created = 0;
};

class MemoryManager {
public:
    static MemoryManager& instance();
    void init(const std::string& memory_dir);

    std::string get_active_memory();
    void set_active_memory(const std::string& content);
    void update_active_memory(const std::string& content, const std::string& mode);

    std::vector<MemoryFile> list_files();
    MemoryFile read_file(const std::string& name);
    void write_file(const std::string& name, const std::string& content, const std::string& desc = "");
    void delete_file(const std::string& name);
    bool file_exists(const std::string& name);

private:
    MemoryManager() = default;
    std::string memory_dir_;
    std::string active_path_;
    std::string db_dir_;
    std::string resolve_path(const std::string& name); // prevents traversal
};
