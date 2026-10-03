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
    std::filesystem::create_directories(getExecutableDirectory().value(), ec);
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
    m_files.erase(std::remove(m_files.begin(), m_files.end(), filePath), m_files.end());
    m_files.insert(m_files.begin(), std::move(filePath));
    const int maxFiles = SettingsManager::instance().getSettings().maxRecentFiles;
    if (static_cast<int>(m_files.size()) > maxFiles) {
        m_files.resize(maxFiles);
    }

    save();
}

const std::vector<std::filesystem::path> &RecentFiles::getFiles() const {
    return m_files;
}

void RecentFiles::loadRecentFiles() {
    VLOG(2) << "Loading recent files.";
    const auto path = getExecutableDirectory().value() / "recent.json";
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
            m_files.clear();
            for (const auto &item : j["recentFiles"]) {
                if (item.is_string()) {
                    bool alreadyExists = m_files.end() != std::find(m_files.begin(), m_files.end(), item.get<std::string>());
                    if (!alreadyExists) {
                        m_files.push_back(item.get<std::string>());
                    }
                }
            }
        }
    } catch (const std::exception &e) {
        LOG(ERROR) << "Error parsing recent files: " << e.what();
    }
    LOG(INFO) << "Loaded " << m_files.size() << " recent files.";
}

void RecentFiles::saveRecentFiles() const {
    VLOG(2) << "Saving recent files.";
    nlohmann::json j;
    j["recentFiles"] = nlohmann::json::array();
    for (const auto &path : m_files) {
        j["recentFiles"].push_back(path);
    }
    auto file = getExecutableDirectory().value() / "recent.json";
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
