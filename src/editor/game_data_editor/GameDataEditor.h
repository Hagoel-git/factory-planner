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
    void draw();

    void setOpen(bool open);
    bool isOpen() const { return m_isOpen; }

    bool isFocused() const { return m_isFocused; }
    void save();

private:
    GameDataManager& m_gameDataManager;
    bool m_isOpen = false;
    bool m_isFocused = false;

    // Layout State
    GameDataTab m_activeTab = GameDataTab::General;
    bool m_isDirty = false;

    // Search & Filter State
    char m_searchBuffer[256] = "";

    // Package Browser State
    std::vector<GameDataPackage> m_packages;
    std::filesystem::path m_selectedFilePath;
    std::filesystem::path m_fileToDelete;
    bool m_requestNewFilePopup = false;
    bool m_requestDeletePopup = false;

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

    struct DraftResource {
        char nameBuf[256] = "";
        std::string iconPath;
    } m_draftResource;

    struct DraftMachine {
        char nameBuf[256] = "";
        char speedBuf[64] = "1.0";
        std::string iconPath;
    } m_draftMachine;

    struct DraftRecipe {
        char nameBuf[256] = "";
        char timeBuf[64] = "1.0";
        std::vector<RecipePort> inputs;
        std::vector<RecipePort> outputs;
        std::vector<std::string> producedIn;
    } m_draftRecipe;

    void refreshPackageList();

    // Internal Draw Helpers
    void drawNewFileDialog();
    void drawDeleteFileConfirmation();
    void drawPackageBrowser();
    void drawEditorWorkspace();
    bool drawTokenList(const char* str_id, std::vector<RecipePort>& ports, bool isInput);
    bool drawMachineList(const char* str_id, std::vector<std::string>& machineKeys);

    void drawTopMenuBar();
    void drawCentralWorkspace();

    // Tab Contents
    void drawResourceCreator();
    void drawMachineCreator();
    void drawRecipeCreator();
    void drawGeneralTab();
    void drawResourceGrid();
    void drawMachineGrid();
    void drawRecipeGrid();

    template<typename T>
    std::vector<std::pair<std::string, T*>> filterMap(std::map<std::string, T>& sourceMap, const std::string& filterStr);
};

#endif //GAMEDATAEDITOR_H