#include "utils/memory_manager.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>

MemoryManager& MemoryManager::instance() {
    static MemoryManager mgr;
    return mgr;
}

void MemoryManager::init(const std::string& dir) {
    memory_dir_ = dir;
    active_path_ = dir + "/active_memory.md";
    db_dir_ = dir + "/memory_db";
    mkdir(dir.c_str(), 0755);
    mkdir(db_dir_.c_str(), 0755);
}

std::string MemoryManager::get_active_memory() {
    std::ifstream f(active_path_);
    if (!f.is_open()) return "";
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

void MemoryManager::set_active_memory(const std::string& content) {
    std::ofstream f(active_path_);
    f << content;
}

void MemoryManager::update_active_memory(const std::string& delta, const std::string& mode) {
    std::string current = get_active_memory();
    if (mode == "replace") {
        set_active_memory(delta);
    } else if (mode == "append") {
        set_active_memory(current + "\n" + delta);
    } else if (mode == "prepend") {
        set_active_memory(delta + "\n" + current);
    } else if (mode == "patch") {
        // Simple string replacement
        size_t pos = current.find(delta);
        if (pos != std::string::npos) {
            current.erase(pos, delta.length());
            set_active_memory(current);
        }
    }
}

std::string MemoryManager::resolve_path(const std::string& name) {
    // Prevent path traversal
    std::string clean;
    for (char c : name) {
        if (c == '/' || c == '\\' || c == '.' || c == '~') continue;
        clean += c;
    }
    if (clean.empty()) clean = "untitled";
    return db_dir_ + "/" + clean + ".md";
}

std::vector<MemoryFile> MemoryManager::list_files() {
    std::vector<MemoryFile> result;
    // Simple glob via popen
    std::string cmd = "ls \"" + db_dir_ + "\"/*.md 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return result;
    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        std::string path(buf);
        path.erase(path.find_last_not_of(" \n\r") + 1);
        if (path.empty()) continue;
        MemoryFile mf;
        mf.name = path.substr(path.rfind('/') + 1);
        mf.name = mf.name.substr(0, mf.name.rfind('.'));
        std::ifstream in(path);
        if (in.is_open()) {
            std::ostringstream oss;
            oss << in.rdbuf();
            mf.content = oss.str();
        }
        result.push_back(mf);
    }
    pclose(f);
    return result;
}

MemoryFile MemoryManager::read_file(const std::string& name) {
    MemoryFile mf;
    mf.name = name;
    std::string path = resolve_path(name);
    std::ifstream f(path);
    if (f.is_open()) {
        std::ostringstream oss;
        oss << f.rdbuf();
        mf.content = oss.str();
    }
    return mf;
}

void MemoryManager::write_file(const std::string& name, const std::string& content, const std::string& desc) {
    std::string path = resolve_path(name);
    std::ofstream f(path);
    f << content;
}

void MemoryManager::delete_file(const std::string& name) {
    std::string path = resolve_path(name);
    unlink(path.c_str());
}

bool MemoryManager::file_exists(const std::string& name) {
    std::string path = resolve_path(name);
    return access(path.c_str(), F_OK) == 0;
}
