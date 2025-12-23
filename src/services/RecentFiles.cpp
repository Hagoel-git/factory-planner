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

void RecentFiles::addFile(std::filesystem::path filePath) {
    VLOG(2) << "Adding recent file: " << filePath;
    files.erase(std::remove(files.begin(), files.end(), filePath), files.end());
    files.insert(files.begin(), std::move(filePath));
    const int maxFiles = SettingsManager::instance().getSettings().maxRecentFiles;
    if (static_cast<int>(files.size()) > maxFiles) {
        files.resize(maxFiles);
    }
}

const std::vector<std::filesystem::path> &RecentFiles::getFiles() const {
    return files;
}

void RecentFiles::loadRecentFiles() {
    VLOG(2) << "Loading recent files.";
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
                    bool alreadyExists = files.end() != std::find(files.begin(), files.end(), item.get<std::string>());
                    if (!alreadyExists) {
                        files.push_back(item.get<std::string>());
                    }
                }
            }
        }
    } catch (const std::exception &e) {
        LOG(ERROR) << "Error parsing recent files: " << e.what();
    }
    LOG(INFO) << "Loaded " << files.size() << " recent files.";
}

void RecentFiles::saveRecentFiles() const {
    VLOG(2) << "Saving recent files.";
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
    LOG(INFO) << "Recent files saved to: " << file;
}
