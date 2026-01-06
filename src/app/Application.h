#ifndef APPLICATION_H
#define APPLICATION_H

#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include "common/CopyBuffer.h"
#include "editor/game_data_editor/GameDataEditor.h"
#include "core/data/GameDataManager.h"
#include "editor/settings_editor/SettingsEditor.h"

class FactoryNodeEditor;
enum class SaveAsMode;

struct DialogState {
    std::mutex mutex; // Protects the result string
    std::string resultPath;
    std::string errorMessage;
    bool hasError = false;
    bool isRunning = false;
    bool isCancelled = false;
};

class Application {
public:
    Application();
    ~Application();


    void draw();

    void applyThemeToAllEditors();

    void drawDebugWindow();

    bool quitRequested = false;
private:
    std::vector<std::unique_ptr<FactoryNodeEditor>> m_editors;
    std::vector<std::filesystem::path> m_closedEditorHistory;
    GameDataManager m_gameDataManager;
    GameDataEditor m_gameDataEditor;
    SettingsEditor m_settingsEditor;
    std::string m_gameDataManagerError;
    std::string m_currentTheme;
    bool m_currentShowGrid = false;
    int m_currentNavButtonIndex = -1;

    bool m_dockInitialized = false;
    int m_activeEditor = -1; // No active editor initially
    int m_focusRequested = -1; // No focus request initially

    std::vector<GameDataPackage> m_cachedGameDataPackages;
    CopyBuffer m_copyBuffer;

    bool m_showDebugWindow = false;
    bool m_showNewProjectDialog = false;
    bool m_showOpenProjectDialog = false;
    bool m_showFileAlreadyOpenPopup = false;
    bool m_showSaveDialog = false;
    bool m_isSaveAsCopy = false;

    bool m_restoringSession = false;

    void drawMenuBar();

    void handleShortcuts();

    void drawNewProjectDialog();
    void drawOpenProjectDialog();
    void drawSaveAsDialog();

    void restoreSession();
    void saveSession();
    void reopenLastClosedEditor();

    void createNewEditor(const std::filesystem::path &gameDataFilePath, const std::filesystem::path &location, const std::string &name, bool addToRecent = true);

    bool saveActiveEditor();
    bool saveActiveEditorAs(const std::string &newFilePath, SaveAsMode mode);
    void saveAll();

    void copyActiveEditor();
    void cutActiveEditor();
    void pasteActiveEditor(bool mapExternalConnections);

    void undoActiveEditor();
    void redoActiveEditor();

    void selectAllActiveEditor();

    void showFlowActiveEditor();

    void fitViewActiveEditor();

    void closeActiveEditor();

    void closeEditor(int index);

    void closeEditorByName(const std::string &name);

    // Utility
    std::string generateDefaultEditorName();
};
#endif
