#include "SettingsManager.h"

#include <iostream>
#include <fstream>
#include <absl/log/log.h>
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
        LOG(ERROR) << "Failed to create settings directory: " << ec.message();
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
    VLOG(2) << "Loading app settings from file.";
    const auto path = get_executable_directory().value() / "app_settings.json";
    if (!std::filesystem::exists(path)) {
        LOG(WARNING) << "Settings file does not exist, using defaults.";
        return;
    }

    std::ifstream f(path);
    if (!f) {
        LOG(ERROR) << "Failed to open settings file: " << path;
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
        if (j.contains("showGrid") && j["showGrid"].is_boolean()) {
            m_settings.showGrid = j["showGrid"].get<bool>();
        }
        if (j.contains("showResourceNames") && j["showResourceNames"].is_boolean()) {
            m_settings.showResourceNames = j["showResourceNames"].get<bool>();
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
        LOG(ERROR) << "Failed to parse settings file: " << e.what();
    }
    LOG(INFO) << "App settings loaded successfully.";
}

void SettingsManager::saveAppSettings() const {
    VLOG(2) << "Saving app settings to file.";
    json j;
    j["gameDataPath"] = m_settings.gameDataPath.string();
    j["defaultProjectPath"] = m_settings.defaultProjectPath.string();
    j["restorePreviousSession"] = m_settings.restorePreviousSession;
    j["themeName"] = m_settings.themeName;
    j["fontName"] = m_settings.fontName;
    j["fontSize"] = m_settings.fontSize;
    j["showGrid"] = m_settings.showGrid;
    j["showResourceNames"] = m_settings.showResourceNames;
    j["autoSaveEnabled"] = m_settings.autoSaveEnabled;
    j["autoSaveIntervalMinutes"] = m_settings.autoSaveIntervalMinutes;
    j["maxUndoHistory"] = m_settings.maxUndoHistory;
    j["maxRecentFiles"] = m_settings.maxRecentFiles;
    const auto path = get_executable_directory().value() / "app_settings.json";
    std::ofstream o(path);
    if (!o.is_open()) {
        LOG(ERROR) << "Failed to open settings file for writing: " << path;
        return;
    }
    o << j.dump(4);
    o.close();
    LOG(INFO) << "App settings saved successfully.";
}
