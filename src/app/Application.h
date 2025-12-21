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


    void Draw();

    void ApplyThemeToAllEditors();

    void DrawDebugWindow();

    bool quitRequested = false;
private:
    std::vector<std::unique_ptr<FactoryNodeEditor>> editors;
    GameDataManager gameDataManager;
    GameDataEditor gameDataEditor;
    SettingsEditor settingsEditor;
    std::string gameDataManagerError;
    std::string currentTheme;
    bool currentShowGrid = false;

    bool dockInitialized = false;
    int activeEditor = -1; // No active editor initially
    int focusRequested = -1; // No focus request initially

    std::vector<GameDataPackage> cachedGameDataPackages;
    CopyBuffer copyBuffer;

    bool showDebugWindow = false;
    bool showNewProjectDialog = false;
    bool showOpenProjectDialog = false;
    bool showFileAlreadyOpenPopup = false;
    bool showSaveDialog = false;
    bool isSaveAsCopy = false;

    bool restoringSession = false;

    void DrawMenuBar();

    void HandleShortcuts();

    void DrawNewProjectDialog();
    void DrawOpenProjectDialog();

    void DrawSaveAsDialog();

    void RestoreSession();

    void SaveSession();

    void CreateNewEditor(const std::filesystem::path &gameDataFilePath, const std::filesystem::path &location, const std::string &name);

    bool SaveActiveEditor();
    bool SaveActiveEditorAs(const std::string &newFilePath, SaveAsMode mode);
    void SaveAll();

    void CopyActiveEditor();

    void CutActiveEditor();

    void PasteActiveEditor(bool mapExternalConnections);

    void UndoActiveEditor();
    void RedoActiveEditor();

    void SelectAllActiveEditor();

    void ShowFlowActiveEditor();

    void FitViewActiveEditor();

    void CloseActiveEditor();

    void CloseEditor(int index);

    void CloseEditorByName(const std::string &name);

    // Utility
    std::string GenerateDefaultEditorName();
};
#endif
