#ifndef GAMEDATAEDITOR_H
#define GAMEDATAEDITOR_H
#include <unordered_set>

#include "GameDataManager.h"


class GameDataEditor {
public:
    GameDataEditor();
    void Draw();

    void SetOpen(bool open) { m_isOpen = open; }
private:
    GameDataManager gameDataManager;
    std::filesystem::path m_currentlyEditingFile;

    std::string m_fileLoadError;

    int selResourceId = -1;
    int selMachineId = -1;
    int selRecipeId = -1;

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
    std::vector<std::pair<int,double>> recInputs;

    std::vector<std::array<char, 128>> recOutputNameBufs;
    std::vector<std::array<char, 128>> recOutputFilterBufs;
    std::vector<std::pair<int,double>> recOutputs;
    // compatible machines set (ids)
    std::unordered_set<int> recMachines;

    // UI flow
    bool showDeleteConfirm = false;
    int deleteTargetType = 0; // 1=res,2=mach,3=rec
    int deleteTargetId = -1;

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

    void DrawLeftSide();

    void DrawNewFileDialog();

    void DrawRightSide();

    void DrawResourcesTab(const GameData &gd);

    void DrawMachinesTab(const GameData &gd);

    void DrawRecipesTab(const GameData &gd);

    static const Resource* findResourceById(const GameData &gd, int id) {
        for (const auto &r : gd.resources) if (r.id == id) return &r;
        return nullptr;
    }
    static const Machine* findMachineById(const GameData &gd, int id) {
        for (const auto &m : gd.machines) if (m.id == id) return &m;
        return nullptr;
    }
};

#endif //GAMEDATAEDITOR_H