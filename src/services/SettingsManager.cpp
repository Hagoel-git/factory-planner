#include "SettingsManager.h"

#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

SettingsManager& SettingsManager::instance() {
    static SettingsManager instance;
    return instance;
}

void SettingsManager::load() {
    std::error_code ec;
    std::filesystem::create_directories(get_executable_directory().value(), ec);
    if (ec) {
        std::cerr << "Failed to create settings directory: " << ec.message() << std::endl;
    }
    loadAppSettings();
}

void SettingsManager::save() {
    saveAppSettings();
}

const AppSettings& SettingsManager::getSettings() const {
    return m_settings;
}

void SettingsManager::setSettings(const AppSettings &settings) {
    m_settings = settings;
}

SettingsManager::SettingsManager() = default;

void SettingsManager::loadAppSettings() {
    const auto path = get_executable_directory().value() / "app_settings.json";
    if (!std::filesystem::exists(path)) {
        std::cerr << "Settings file does not exist, using defaults." << std::endl;
        return;
    }

    std::ifstream f(path);
    if (!f) {
        std::cerr << "Failed to open settings file, using defaults." << std::endl;
        return;
    }
    std::filesystem::path executablePath = get_executable_directory().value_or(std::filesystem::current_path());
    try {
        json j;
        f >> j;

        if (j.contains("gameDataPath") && j["gameDataPath"].is_string()) {
            m_settings.gameDataPath = j["gameDataPath"].get<std::string>();
        }
        if (j.contains("defaultProjectPath") && j["defaultProjectPath"].is_string()) {
            m_settings.defaultProjectPath = j["defaultProjectPath"].get<std::string>();
        }
        if (j.contains("restorePreviousSession") && j["restorePreviousSession"].is_boolean()) {
            m_settings.restorePreviousSession = j["restorePreviousSession"].get<bool>();
        }
        if (j.contains("themeName") && j["themeName"].is_string()) {
            m_settings.themeName = j["themeName"].get<std::string>();
        }
        if (j.contains("fontName") && j["fontName"].is_string()) {
            m_settings.fontName = j["fontName"].get<std::string>();
        }
        if (j.contains("fontSize") && j["fontSize"].is_number_float()) {
            m_settings.fontSize = j["fontSize"].get<float>();
        }
        if (j.contains("autoSaveEnabled") && j["autoSaveEnabled"].is_boolean()) {
            m_settings.autoSaveEnabled = j["autoSaveEnabled"].get<bool>();
        }
        if (j.contains("autoSaveIntervalMinutes") && j["autoSaveIntervalMinutes"].is_number_integer()) {
            m_settings.autoSaveIntervalMinutes = j["autoSaveIntervalMinutes"].get<int>();
        }
        if (j.contains("maxUndoHistory") && j["maxUndoHistory"].is_number_integer()) {
            m_settings.maxUndoHistory = j["maxUndoHistory"].get<int>();
        }
        if (j.contains("maxRecentFiles") && j["maxRecentFiles"].is_number_integer()) {
            m_settings.maxRecentFiles = j["maxRecentFiles"].get<int>();
        }

    } catch (const std::exception& e) {
        std::cerr << "Failed to parse settings file, using defaults. Error: " << e.what() << std::endl;
    }
}

void SettingsManager::saveAppSettings() const {
    json j;
    j["gameDataPath"] = m_settings.gameDataPath.string();
    j["defaultProjectPath"] = m_settings.defaultProjectPath.string();
    j["restorePreviousSession"] = m_settings.restorePreviousSession;
    j["themeName"] = m_settings.themeName;
    j["fontName"] = m_settings.fontName;
    j["fontSize"] = m_settings.fontSize;
    j["autoSaveEnabled"] = m_settings.autoSaveEnabled;
    j["autoSaveIntervalMinutes"] = m_settings.autoSaveIntervalMinutes;
    j["maxUndoHistory"] = m_settings.maxUndoHistory;
    j["maxRecentFiles"] = m_settings.maxRecentFiles;
    const auto path = get_executable_directory().value() / "app_settings.json";
    std::ofstream o(path);
    if (!o.is_open()) {
        std::cerr << "Failed to open settings file for writing: " << path << std::endl;
        return;
    }
    o << j.dump(4);
    o.close();
}
