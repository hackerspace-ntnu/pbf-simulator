#include "file_io.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <filesystem>
#include <iostream>

std::string readTextFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error(std::string("Failed to open file: ") + path);
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string fetchShaderFile(const std::string& name) {
    const std::filesystem::path shadersDir = "shaders";

    if (!std::filesystem::is_directory(shadersDir)) {
        std::cerr << "Error: shaders directory not found at "
                  << std::filesystem::absolute(shadersDir) << "\n";
        return {};
    }

    std::filesystem::path file = shadersDir / name;
    if (file.extension().empty())
        file += ".comp";

    if (!std::filesystem::is_regular_file(file)) {
        std::cerr << "Error: shader not found: " << file << "\n";
        return {};
    }

    return file.string();
}