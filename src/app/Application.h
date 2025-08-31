#ifndef APPLICATION_H
#define APPLICATION_H

#include <vector>
#include <memory>
#include <string>
#include "CopyBuffer.h"
#include "GameDataEditor.h"
#include "GameDataManager.h"
#include "settings_editor/SettingsEditor.h"

class FactoryNodeEditor;
enum class SaveAsMode;

class Application {
public:
    Application();
    ~Application();


    void Draw();

    void DrawDebugWindow();

    bool quitRequested = false;
private:
    std::vector<std::unique_ptr<FactoryNodeEditor>> editors;
    GameDataEditor gameDataEditor;
    SettingsEditor settingsEditor;
    GameDataManager gameDataManager;
    std::string gameDataManagerError;
    bool dockInitialized = false;
    int activeEditor = -1; // No active editor initially

    CopyBuffer copyBuffer;

    bool showDebugWindow = false;
    bool showNewProjectDialog = false;
    bool showOpenProjectDialog = false;
    bool showFileAlreadyOpenPopup = false;
    bool showSaveDialog = false;
    bool isSaveAsCopy = false;

    void DrawMenuBar();

    void HandleShortcuts();

    void DrawNewProjectDialog();
    void DrawOpenProjectDialog();

    void DrawSaveAsDialog();

    void CreateNewEditor(const std::string &gameDataFilePath, const std::string &location, const std::string &name);

    bool SaveActiveEditor();
    bool SaveActiveEditorAs(const std::string &newFilePath, SaveAsMode mode);
    void SaveAll();

    void CopyActiveEditor();

    void CutActiveEditor();

    void PasteActiveEditor(bool mapExternalConnections);

    void UndoActiveEditor();
    void RedoActiveEditor();

    void SelectAllActiveEditor();

    void CloseActiveEditor();

    void CloseEditor(int index);

    void CloseEditorByName(const std::string &name);

    // Utility
    std::string GenerateDefaultEditorName();
};
#endif
