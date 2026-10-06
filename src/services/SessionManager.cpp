#include "SessionManager.h"

#include <fstream>
#include <iostream>
#include <absl/log/log.h>
#include <nlohmann/json.hpp>

#include "common/FilesystemUtils.h"

SessionManager &SessionManager::instance() {
    static SessionManager instance;
    return instance;
}

SessionManager::SessionManager() = default;

void SessionManager::load() {
    std::error_code ec;
    std::filesystem::create_directories(getExecutableDirectory().value_or(std::filesystem::current_path()), ec);
    if (ec) {
        LOG(ERROR) << "Failed to create settings directory: " << ec.message();
    }
    loadSession();
}

void SessionManager::save() {
    saveSession();
}

const SessionState &SessionManager::getSessionState() const {
    return m_state;
}

void SessionManager::setSessionState(const SessionState &state) {
    m_state = state;
}

void SessionManager::loadSession() {
    VLOG(2) << "Loading session from file.";
    const auto path = getExecutableDirectory().value_or(std::filesystem::current_path()) / "session.json";
    if (!std::filesystem::exists(path)) {
        LOG(INFO) << "Session file does not exist, using defaults.";
        return;
    }
    std::ifstream f(path);
    if (!f) {
        LOG(ERROR) << "Failed to open session file: " << path;
        return;
    }

    try {
        nlohmann::json j;
        f >> j;

        if (j.contains("openProjectPaths") && j["openProjectPaths"].is_array()) {
            m_state.openProjectPaths.clear();
            for (const auto &item : j["openProjectPaths"]) {
                if (item.is_string()) {
                    m_state.openProjectPaths.emplace_back(item.get<std::string>());
                }
            }
        }
        if (j.contains("activeProjectIndex") && j["activeProjectIndex"].is_number_integer()) {
            m_state.activeProjectIndex = j["activeProjectIndex"].get<int>();
        }
    } catch (const std::exception &e) {
        LOG(ERROR) << "Failed to parse session file: " << e.what();
    }
    LOG(INFO) << "Session loaded: " << m_state.openProjectPaths.size()
               << " open projects, active index: " << m_state.activeProjectIndex;
}

void SessionManager::saveSession() const {
    VLOG(2) << "Saving session to file.";
    auto file = getExecutableDirectory().value_or(std::filesystem::current_path()) / "session.json";
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    std::error_code dirEc;
    std::filesystem::create_directories(file.parent_path(), dirEc);
    try {
        nlohmann::json j;
        j["openProjectPaths"] = nlohmann::json::array();
        for (const auto &path : m_state.openProjectPaths) {
            j["openProjectPaths"].push_back(path.string());
        }
        j["activeProjectIndex"] = m_state.activeProjectIndex;
        {
            std::ofstream ofs(tmp);
            if (!ofs) {
                LOG(ERROR) << "Failed to open temporary session file for write: " << tmp;
                return;
            }
            ofs << j.dump(2);
            ofs.flush();
            if (!ofs.good()) {
                LOG(ERROR) << "Failed to write session data to temporary file: " << tmp;
                ofs.close();
                std::error_code ec;
                std::filesystem::remove(tmp, ec);
                return;
            }
        }
        std::error_code ec;
        std::filesystem::rename(tmp, file, ec);
        if (ec) {
            LOG(ERROR) << "Failed to rename temp session file to final path: " << ec.message();
            std::filesystem::remove(tmp, ec);
            return;
        }
    } catch (const std::exception &e) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        LOG(ERROR) << "Exception while saving session: " << e.what();
        return;
    }
    LOG(INFO) << "Session saved.";
}
