#ifndef SETTINGSEDITOR_H
#define SETTINGSEDITOR_H
#include <array>
#include <string>
#include <vector>
#include "services/SettingsManager.h"

class SettingsEditor {
public:

    void draw();
    void setOpen(bool open) { m_isOpen = open; startEditing();}
private:
    AppSettings m_savedSettings;
    AppSettings m_editSettings;

    std::vector<std::string> m_categories = { "General", "Editor", "Appearance" };
    std::string m_selCategory = "General";

    std::array<char, 1024> m_bufDefaultProjectPath{};
    std::array<char, 1024> m_bufGameDataPath{};

    bool m_isDirty = false;

    bool m_isOpen = false;
    void drawLeftPanel();
    void drawRightPanel();

    void drawBottomPanel();

    void startEditing();

    void revertChanges();

    void applyChanges();
};

#endif //SETTINGSEDITOR_H
