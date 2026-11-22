#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H
#include <filesystem>
#include <string>
#include <vector>

#include "common/FilesystemUtils.h"
#include "core/data/GameDataScanner.h"

struct IconFallbackSettings {
    bool enabled = false;
    std::vector<std::string> order;

    bool operator==(const IconFallbackSettings&) const = default;
};

// Reconciles a saved fallback game-ID priority list against currently scanned packages:
// preserves saved relative priority for installed games, removes deleted/uninstalled games,
// and appends newly scanned game IDs in scan order.
std::vector<std::string> mergeIconFallbackOrder(
    const std::vector<std::string>& savedOrder,
    const std::vector<GameDataPackage>& scannedPackages);

struct AppSettings {
    std::filesystem::path executablePath = getExecutableDirectory().value_or(std::filesystem::current_path());
    bool restorePreviousSession = true;
    std::filesystem::path gameDataPath = executablePath / "game_data";
    std::filesystem::path defaultProjectPath = executablePath / "projects";
    std::string themeName = "Dark";
    std::string fontName = "DroidSans.ttf"; // must match ImFont debug name
    float fontSize = 18;
    bool showGrid = false;
    bool showResourceNames = false;
    bool showDebugInfo = false;
    bool autoSaveEnabled = true;
    int autoSaveIntervalMinutes = 5;
    int navigationButtonIndex = 2; // Mouse button index navigate action will react to (0-left, 1-right, 2-middle)
    int maxUndoHistory = 100;
    int maxRecentFiles = 10;

    int windowWidth = 1280;
    int windowHeight = 720;
    bool windowMaximized = false;

    IconFallbackSettings iconFallback;
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
