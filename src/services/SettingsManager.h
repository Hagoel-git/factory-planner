#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H
#include <filesystem>

#include "FilesystemUtils.h"

struct AppSettings {
    std::filesystem::path executablePath = get_executable_directory().value_or(std::filesystem::current_path());
    std::filesystem::path gameDataPath = executablePath / "game_data";
    std::filesystem::path defaultProjectPath = executablePath / "projects";
    std::string themeName = "Dark"; //todo
    std::string fontName = "DroidSans.ttf"; // must match ImFont debug name
    float fontSize = 18;
    bool autoSaveEnabled = true;
    int autoSaveIntervalMinutes = 5;
    int maxUndoHistory = 100;
};

class SettingsManager {
public:
    static SettingsManager& instance();

    void load();
    void save();

    const AppSettings& getSettings() const;

    void setSettings(const AppSettings& settings);

private:
    SettingsManager();
    ~SettingsManager() = default;

    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    void loadAppSettings();
    void saveAppSettings() const;
    AppSettings m_settings;

};



#endif //SETTINGSMANAGER_H
