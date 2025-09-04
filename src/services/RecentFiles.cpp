#include "RecentFiles.h"
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include "FilesystemUtils.h"
#include "SettingsManager.h"

RecentFiles &RecentFiles::instance() {
    static RecentFiles instance;
    return instance;
}

void RecentFiles::load() {
    std::error_code ec;
    std::filesystem::create_directories(get_executable_directory().value(), ec);
    if (ec) {
        std::cerr << "Failed to create settings directory: " << ec.message() << std::endl;
    }
    loadRecentFiles();
}

void RecentFiles::save() {
    saveRecentFiles();
}

void RecentFiles::addFile(const std::filesystem::path &filePath) {
    files.erase(std::remove(files.begin(), files.end(), filePath), files.end());
    files.insert(files.begin(), filePath);
    const int maxFiles = SettingsManager::instance().getSettings().maxRecentFiles;
    if (static_cast<int>(files.size()) > maxFiles) {
        files.resize(maxFiles);
    }
}

const std::vector<std::filesystem::path> &RecentFiles::getFiles() const {
    return files;
}

void RecentFiles::loadRecentFiles() {
    const auto path = get_executable_directory().value() / "recent.json";
    if (!std::filesystem::exists(path)) {
        std::cerr << "Recent does not exist: " << path << std::endl;
        return;
    }
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Failed to open recent file." << std::endl;
        return;
    }

    try {
        nlohmann::json j;
        f >> j;

        if (j.contains("recentFiles") && j["recentFiles"].is_array()) {
            files.clear();
            for (const auto &item : j["recentFiles"]) {
                if (item.is_string()) {
                    files.push_back(item.get<std::string>());
                }
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "Error parsing recent files: " << e.what() << std::endl;
    }
}

void RecentFiles::saveRecentFiles() const {
    nlohmann::json j;
    j["recentFiles"] = nlohmann::json::array();
    for (const auto &path : files) {
        j["recentFiles"].push_back(path);
    }
    auto file = get_executable_directory().value() / "recent.json";
    try {
        std::ofstream ofs(file);
        if (!ofs) {
            std::cerr << "Failed to open recent file for write: " << file << std::endl;
            return;
        }
        ofs << j.dump(2);
    } catch (const std::exception &e) {
        std::cerr << "Error saving recent files: " << e.what() << std::endl;
    }
}
