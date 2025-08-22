#ifndef APPLICATION_H
#define APPLICATION_H

#include <vector>
#include <memory>
#include <string>
#include "FactoryNodeEditor.h"
#include "../common/CopyBuffer.h"

class Application {
public:
    Application();
    ~Application();

    void Draw();
    bool quitRequested = false;
private:
    std::vector<std::unique_ptr<FactoryNodeEditor>> editors;
    GameDataManager gameDataManager;
    std::string gameDataManagerError;
    std::filesystem::path executableDirectory;
    bool dockInitialized = false;
    int activeEditor = -1; // No active editor initially

    CopyBuffer copyBuffer;

    bool showNewProjectDialog = false;
    bool showOpenProjectDialog = false;
    bool showFileAlreadyOpenPopup = false;
    bool showSaveDialog = false;
    bool isSaveAsCopy = false;

    void DrawMenuBar();

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

    std::vector<std::filesystem::path> GetGameDataFiles(const std::filesystem::path &directory);
};
#endif
