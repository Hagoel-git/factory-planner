#ifndef GAMEDATAEDITOR_H
#define GAMEDATAEDITOR_H
#include <unordered_set>

#include "core/data/GameDataManager.h"

struct GameDataPackage;

class GameDataEditor {
public:
    explicit GameDataEditor(GameDataManager& manager);
    void Draw();

    void SetOpen(bool open) { m_isOpen = open; }
private:
    GameDataManager& gameDataManager;
    std::vector<GameDataPackage> m_cachedPackages;
    std::filesystem::path m_currentlyEditingFile;

    std::string m_fileLoadError;

    std::string selResourceKey;
    std::string selMachineKey;
    std::string selRecipeKey;

    char gameNameBuf[128] = "";

    char resNameBuf[128] = "";

    // Machine editing
    char machNameBuf[128] = "";
    double machBaseSpeed = 1.0;

    // Recipe editing
    char recNameBuf[128] = "";
    double recTimeSeconds = 0.0;
    // Lists for inputs/outputs: vector of pairs (resource_id, amount)
    std::vector<std::array<char, 128>> recInputNameBufs;
    std::vector<std::array<char, 128>> recInputFilterBufs;
    std::vector<std::pair<std::string,double>> recInputs;

    std::vector<std::array<char, 128>> recOutputNameBufs;
    std::vector<std::array<char, 128>> recOutputFilterBufs;
    std::vector<std::pair<std::string,double>> recOutputs;
    // compatible machines set (ids)
    std::unordered_set<std::string> recMachines;

    // UI flow
    bool showDeleteConfirm = false;
    int deleteTargetType = 0; // 1=res,2=mach,3=rec
    std::string deleteTargetKey;

    // search filters
    char resourceFilter[128] = "";
    char machineFilter[128] = "";
    char recipeFilter[128] = "";

    // dirty flags (to indicate unsaved edits in current form)
    bool resourceDirty = false;
    bool machineDirty = false;
    bool recipeDirty = false;

    bool m_isOpen = false;
    bool m_showNewFilePopup = false;

    void RefreshPackageList();

    void DrawLeftSide();

    void DrawNewFileDialog();

    void DrawRightSide();

    void DrawRenameFileDialog();

    void DrawDeleteFileDialog();

    void DrawResourcesTab(const GameData &gd);

    void DrawMachinesTab(const GameData &gd);

    void DrawRecipesTab(const GameData &gd);
};

#endif //GAMEDATAEDITOR_H