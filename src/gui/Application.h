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

private:
    std::vector<std::unique_ptr<FactoryNodeEditor>> editors;
    bool dockInitialized = false;
    int activeEditor = -1; // No active editor initially
     // Drawing functions
    void DrawMenuBar();

    void CreateNewEditor(const std::string& name = "");

    void CloseActiveEditor();

    void CloseEditor(int index);
    void RenameEditor(int index, const std::string& newName);

    // Utility
    std::string GenerateDefaultEditorName();
};
#endif
