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
    bool m_requestNewFilePopup = false;

    struct IconDialogState {
        std::atomic<bool> isRunning = false; // Is the dialog currently open?
        std::atomic<bool> isReady = false;   // Did the user pick a file?
        std::string resultPath;              // The path selected
        std::string targetKey;               // Which resource are we editing?
    } m_iconDialog;

    struct TokenPopupState {
        bool isOpen = false;
        std::vector<RecipePort>* targetVector = nullptr;
        bool isInput = false;
        char searchBuf[128] = "";
    } m_tokenPopupState;

    void RefreshPackageList();

    // Internal Draw Helpers
    void DrawNewFileDialog();
    void DrawPackageBrowser();
    void DrawEditorWorkspace();
    bool DrawTokenList(const char* str_id, std::vector<RecipePort>& ports, bool isInput);
    bool DrawMachineList(const char* str_id, std::vector<std::string>& machineKeys);

    void DrawTopMenuBar();
    void DrawCentralWorkspace();
    void DrawContextPane();

    // Tab Contents
    void DrawGeneralTab();
    void DrawResourceGrid();
    void DrawMachineGrid();
    void DrawRecipeGrid();

    template<typename T>
    std::vector<std::pair<std::string, T*>> FilterMap(std::map<std::string, T>& sourceMap, const std::string& filterStr);
};

#endif //GAMEDATAEDITOR_H