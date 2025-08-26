#ifndef GAMEDATAEDITOR_H
#define GAMEDATAEDITOR_H
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

    int m_selectedResourceId = -1;
    int m_selectedMachineId = -1;
    int m_selectedRecipeId = -1;

    bool m_isOpen = false;
    bool m_showNewFilePopup = false;

    void DrawLeftSide();

    void DrawNewFileDialog();

    void DrawRightSide();
};



#endif //GAMEDATAEDITOR_H
