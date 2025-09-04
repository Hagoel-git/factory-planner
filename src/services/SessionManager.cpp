#include "SessionManager.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "FilesystemUtils.h"

SessionManager &SessionManager::instance() {
    static SessionManager instance;
    return instance;
}

SessionManager::SessionManager() = default;

void SessionManager::load() {
    std::error_code ec;
    std::filesystem::create_directories(get_executable_directory().value(), ec);
    if (ec) {
        std::cerr << "Failed to create settings directory: " << ec.message() << std::endl;
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
    const auto path = get_executable_directory().value() / "session.json";
    if (!std::filesystem::exists(path)) {
        std::cerr << "Session does not exist: " << path << std::endl;
        return;
    }
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Failed to open settings file." << std::endl;
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
        std::cerr << "Failed to parse session file, using defaults: " << e.what() << std::endl;
    }
}

void SessionManager::saveSession() const {
    auto file = get_executable_directory().value() / "session.json";
    try {
        nlohmann::json j;
        j["openProjectPaths"] = nlohmann::json::array();
        for (const auto &path : m_state.openProjectPaths) {
            j["openProjectPaths"].push_back(path.string());
        }
        j["activeProjectIndex"] = m_state.activeProjectIndex;
        std::ofstream ofs(file);
        if (!ofs) {
            std::cerr << "Failed to open session file for write: " << file << std::endl;
            return;
        }
        ofs << j.dump(2);
    } catch (const std::exception &e) {
        std::cerr << "Exception while saving session: " << e.what() << std::endl;
    }
}
