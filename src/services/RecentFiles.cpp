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
    std::filesystem::create_directories(getExecutableDirectory().value_or(std::filesystem::current_path()), ec);
    if (ec) {
        LOG(ERROR) << "Failed to create settings directory: " << ec.message();
    }
    loadRecentFiles();
}

void RecentFiles::save() {
    saveRecentFiles();
}

void RecentFiles::addFile(std::filesystem::path filePath) {
    if (filePath.empty()) {
        return;
    }
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
    const auto path = getExecutableDirectory().value_or(std::filesystem::current_path()) / "recent.json";
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
                    std::string str = item.get<std::string>();
                    if (str.empty()) continue;
                    bool alreadyExists = m_files.end() != std::find(m_files.begin(), m_files.end(), str);
                    if (!alreadyExists) {
                        m_files.push_back(std::move(str));
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
        j["recentFiles"].push_back(path.string());
    }
    auto file = getExecutableDirectory().value_or(std::filesystem::current_path()) / "recent.json";
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    std::error_code dirEc;
    std::filesystem::create_directories(file.parent_path(), dirEc);
    try {
        {
            std::ofstream ofs(tmp);
            if (!ofs) {
                LOG(ERROR) << "Failed to open temporary recent file for write: " << tmp;
                return;
            }
            ofs << j.dump(2);
            ofs.flush();
            if (!ofs.good()) {
                LOG(ERROR) << "Failed to write recent files data to temporary file: " << tmp;
                ofs.close();
                std::error_code ec;
                std::filesystem::remove(tmp, ec);
                return;
            }
        }
        std::error_code ec;
        std::filesystem::rename(tmp, file, ec);
        if (ec) {
            LOG(ERROR) << "Failed to rename temp recent file to final path: " << ec.message();
            std::filesystem::remove(tmp, ec);
            return;
        }
    } catch (const std::exception &e) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        LOG(ERROR) << "Error saving recent files: " << e.what();
        return;
    }
    LOG(INFO) << "Recent files saved to: " << file;
}
