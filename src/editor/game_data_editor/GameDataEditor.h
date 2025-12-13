#ifndef GAMEDATAEDITOR_H
#define GAMEDATAEDITOR_H

#include "core/data/GameDataManager.h"
#include "core/data/GameDataScanner.h"
#include <string>
#include <vector>

enum class GameDataTab {
    General,
    Resources,
    Machines,
    Recipes
};

class GameDataEditor {
public:
    explicit GameDataEditor(GameDataManager& manager);

    // Main Draw Entry Point
    void Draw();

    void SetOpen(bool open);
    bool IsOpen() const { return m_isOpen; }

    // Focus & Input Routing
    bool IsFocused() const { return m_isFocused; }
    void Save();

private:
    GameDataManager& gameDataManager;
    bool m_isOpen = false;
    bool m_isFocused = false;

    // Layout State
    GameDataTab m_activeTab = GameDataTab::General;
    bool m_showContextPane = false;

    // Search & Filter State
    char m_searchBuffer[256] = "";

    // Package Browser State
    std::vector<GameDataPackage> m_packages;
    int m_selectedPackageIndex = -1;
    bool m_requestNewFilePopup = false; // <--- FIX: Added flag for cross-scope popup triggering
    void RefreshPackageList();

    // Internal Draw Helpers
    void DrawNewFileDialog();
    void DrawPackageBrowser();
    void DrawEditorWorkspace();

    void DrawTopMenuBar();
    void DrawCentralWorkspace();
    void DrawContextPane();

    // Tab Contents
    void DrawGeneralTab();
    void DrawResourceGrid();
    void DrawMachineGrid();
    void DrawRecipeGrid();
    void DrawGridPlaceholder(const char* label);

    template<typename T>
    std::vector<std::pair<std::string, T*>> FilterMap(std::map<std::string, T>& sourceMap);
};

#endif //GAMEDATAEDITOR_H