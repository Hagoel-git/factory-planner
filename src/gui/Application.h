#ifndef APPLICATION_H
#define APPLICATION_H

#include <vector>
#include <memory>
#include <string>
#include "FactoryNodeEditor.h"
#include "imgui.h"

class Application {
public:
    Application();
    ~Application();

    void Draw();
    bool quitRequested = false;
private:
    std::vector<std::unique_ptr<FactoryNodeEditor>> editors;
    bool dockInitialized = false;
    int activeEditor = -1; // No active editor initially

    bool showNewProjectDialog = false;
    bool showOpenProjectDialog = false;

    void DrawMenuBar();

    void DrawNewProjectDialog();
    void DrawOpenProjectDialog();

    void CreateNewEditor(const std::string &gameDataFilePath, const std::string &location, const std::string &name);

    bool SaveActiveEditor();


    void CloseActiveEditor();

    void CloseEditor(int index);
    // Utility
    std::string GenerateDefaultEditorName();
    std::vector<std::string> GetGameDataFiles(const std::string &directory);
};
#endif
