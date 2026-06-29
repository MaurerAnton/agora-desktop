#pragma once

#include <string>
#include <vector>

struct ExportResult {
    bool success = false;
    std::string error;
    int conversations = 0;
    int messages = 0;
    int prompts = 0;
    int memories = 0;
};

ExportResult export_data(const std::string& output_path,
                         const std::string& db_path,
                         const std::string& config_path,
                         const std::string& memory_dir,
                         bool include_api_keys = false);

struct ImportResult {
    bool success = false;
    std::string error;
    int conversations = 0;
    int messages = 0;
    int prompts = 0;
    int memories = 0;
};

ImportResult import_data(const std::string& input_path,
                         const std::string& db_path,
                         const std::string& config_path,
                         const std::string& memory_dir);

// Read manifest from a .agora file without full import
std::string read_manifest(const std::string& input_path);
