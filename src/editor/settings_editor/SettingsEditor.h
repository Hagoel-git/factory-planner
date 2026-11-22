#ifndef SETTINGSEDITOR_H
#define SETTINGSEDITOR_H
#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include "core/data/GameDataScanner.h"
#include "services/SettingsManager.h"

class SettingsEditor {
public:

    void draw();
    void setOpen(bool open) { m_isOpen = open; startEditing();}

    struct FolderDialogState {
        std::mutex mutex;
        std::string resultPath;
        std::atomic<bool> isRunning{false};
        std::atomic<bool> isReady{false};
        std::string target; // "DEFAULT_PROJECT" or "GAME_DATA"
    };

    bool isFolderDialogRunning() const { return m_folderDialog ? m_folderDialog->isRunning.load() : false; }
    void simulateFolderDialogResult(const std::string& target, const std::string& path) {
        if (!m_folderDialog) return;
        std::lock_guard<std::mutex> lock(m_folderDialog->mutex);
        m_folderDialog->target = target;
        m_folderDialog->resultPath = path;
        m_folderDialog->isReady = true;
    }
    const std::string& getDefaultProjectPathBuffer() const { static std::string s; s = m_bufDefaultProjectPath.data(); return s; }
    const std::string& getGameDataPathBuffer() const { static std::string s; s = m_bufGameDataPath.data(); return s; }
    const std::vector<std::string>& getDisplayFallbackOrder() const { return m_displayFallbackOrder; }
    bool getEditIconFallbackEnabled() const { return m_editSettings.iconFallback.enabled; }
    void setEditIconFallbackEnabled(bool enabled) { m_editSettings.iconFallback.enabled = enabled; m_isDirty = true; }
    bool moveFallbackGame(size_t index, int delta);
    void triggerRescanForTest() { rescanGameDataForFallback(); }
    void applyChangesForTest() { applyChanges(); }

private:
    AppSettings m_savedSettings;
    AppSettings m_editSettings;

    std::vector<std::string> m_categories = { "General", "Editor", "Appearance" };
    std::string m_selCategory = "General";

    std::array<char, 1024> m_bufDefaultProjectPath{};
    std::array<char, 1024> m_bufGameDataPath{};

    std::vector<GameDataPackage> m_scannedPackages;
    std::unordered_map<std::string, std::string> m_scannedGameNames;
    std::vector<std::string> m_displayFallbackOrder;
    std::filesystem::path m_lastScannedGameDataPath;

    std::shared_ptr<FolderDialogState> m_folderDialog = std::make_shared<FolderDialogState>();

    bool m_isDirty = false;

    bool m_isOpen = false;
    void drawLeftPanel();
    void drawRightPanel();

    void drawBottomPanel();

    void rescanGameDataForFallback();

    void startEditing();

    void revertChanges();

    void applyChanges();
};

#endif //SETTINGSEDITOR_H
