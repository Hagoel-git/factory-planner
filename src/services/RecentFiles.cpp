#include "RecentFiles.h"
#include <iostream>
#include <fstream>
#include <absl/log/log.h>
#include <nlohmann/json.hpp>
#include "common/FilesystemUtils.h"
#include "services/SettingsManager.h"

RecentFiles &RecentFiles::instance() {
    static RecentFiles instance;
    return instance;
}

void RecentFiles::load() {
    std::error_code ec;
    std::filesystem::create_directories(get_executable_directory().value(), ec);
    if (ec) {
        LOG(ERROR) << "Failed to create settings directory: " << ec.message();
    }
    loadRecentFiles();
}

void RecentFiles::save() {
    saveRecentFiles();
}

void RecentFiles::addFile(const std::filesystem::path &filePath) {
    DLOG(INFO) << "Adding recent file: " << filePath;
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
    DLOG(INFO) << "Loading recent files.";
    const auto path = get_executable_directory().value() / "recent.json";
    if (!std::filesystem::exists(path)) {
        LOG(INFO) << "Recent files does not exist: " << path;
        return;
    }
    std::ifstream f(path);
    if (!f) {
        LOG(ERROR) << "Failed to open recent file: " << path;
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
        LOG(ERROR) << "Error parsing recent files: " << e.what();
    }
    DLOG(INFO) << "Loaded " << files.size() << " recent files.";
}

void RecentFiles::saveRecentFiles() const {
    DLOG(INFO) << "Saving recent files.";
    nlohmann::json j;
    j["recentFiles"] = nlohmann::json::array();
    for (const auto &path : files) {
        j["recentFiles"].push_back(path);
    }
    auto file = get_executable_directory().value() / "recent.json";
    try {
        std::ofstream ofs(file);
        if (!ofs) {
            LOG(ERROR) << "Failed to open recent file for write: " << file;
            return;
        }
        ofs << j.dump(2);
    } catch (const std::exception &e) {
        LOG(ERROR) << "Error saving recent files: " << e.what();
    }
    DLOG(INFO) << "Recent files saved to: " << file;
}
