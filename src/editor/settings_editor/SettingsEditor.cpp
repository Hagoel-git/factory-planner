#include "SettingsEditor.h"
#include "services/SettingsManager.h"
#include "imgui.h"
#include "nfd.h"

#include <cstring>
#include <iostream>
#include <thread>

void SettingsEditor::Draw() {
    if (!m_isOpen) return;

    int flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(800, 600));
    ImGui::Begin("Game Data Editor", &m_isOpen, flags);
    ImGui::PopStyleVar();

    DrawLeftPanel();
    ImGui::SameLine();
    DrawRightPanel();

    DrawBottomPanel();
    ImGui::End();
}

void SettingsEditor::DrawLeftPanel() {
    ImGui::BeginChild("LeftPanel", ImVec2(220, -40), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextUnformatted("Settings");
    ImGui::Separator();

    for (const auto &cat : m_categories) {
        bool isSelected = (m_selCategory == cat);
        if (ImGui::Selectable(cat.c_str(), isSelected, 0, ImVec2(0, 0))) {
            m_selCategory = cat;
        }
    }

    ImGui::EndChild();
}


void SettingsEditor::DrawRightPanel() {
    ImGui::BeginChild("RightPanel", ImVec2(0, -40), false);

    if (m_selCategory == "General") {
        ImGui::TextWrapped("General application settings.");
        ImGui::Spacing();

        float totalWidth = ImGui::GetContentRegionAvail().x;
        float buttonWidth = 60.0f;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float inputWidth = totalWidth - buttonWidth - spacing;


        // Default Project Path
        ImGui::TextUnformatted("Default Project Path");
        ImGui::PushID("DefaultProjectPath");
        ImGui::PushItemWidth(inputWidth);
        ImGui::InputText("", m_bufDefaultProjectPath.data(), (int)m_bufDefaultProjectPath.size());
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(buttonWidth, 0))) {
            std::thread([this]() {
            nfdu8char_t *outPath;
            nfdpickfolderu8args_t args = {0};
            args.defaultPath = m_bufDefaultProjectPath.data();
            nfdresult_t result = NFD_PickFolderU8_With(&outPath, &args);
            if (result == NFD_OKAY) {
                std::strncpy(m_bufDefaultProjectPath.data(), outPath, m_bufDefaultProjectPath.size() - 1);
                m_bufDefaultProjectPath[m_bufDefaultProjectPath.size() - 1] = '\0';
                NFD_FreePathU8(outPath);
            }
        }).detach();
        }
        // update settings from buffer if user typed
        std::string newDefaultPath = std::string(m_bufDefaultProjectPath.data());
        if (newDefaultPath != m_editSettings.defaultProjectPath.string()) {
            m_editSettings.defaultProjectPath = newDefaultPath;
            m_isDirty = true;
        }
        ImGui::PopID();

        ImGui::Spacing();

        // Game Data Path
        ImGui::TextUnformatted("Game Data Path");
        ImGui::PushID("GameDataPath");
        ImGui::PushItemWidth(inputWidth);
        ImGui::InputText("", m_bufGameDataPath.data(), (int)m_bufGameDataPath.size());
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(buttonWidth, 0))) {
            std::thread([this]() {
            nfdu8char_t *outPath;
            nfdpickfolderu8args_t args = {0};
            args.defaultPath = m_bufGameDataPath.data();
            nfdresult_t result = NFD_PickFolderU8_With(&outPath, &args);
            if (result == NFD_OKAY) {
                std::strncpy(m_bufGameDataPath.data(), outPath, m_bufGameDataPath.size() - 1);
                m_bufGameDataPath[m_bufGameDataPath.size() - 1] = '\0';
                NFD_FreePathU8(outPath);
            }
        }).detach();
        }
        std::string newGameDataPath = std::string(m_bufGameDataPath.data());
        if (newGameDataPath != m_editSettings.gameDataPath.string()) {
            m_editSettings.gameDataPath = newGameDataPath;
            m_isDirty = true;
        }

        ImGui::Spacing();

        if (ImGui::Checkbox("Open previous projects on startup", &m_editSettings.restorePreviousSession)) {
            m_isDirty = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("If enabled, the application will reopen the projects that were open when it was last closed.");

        ImGui::Spacing();

        if (ImGui::InputInt("Max Recent Files", &m_editSettings.maxRecentFiles)) {
            m_editSettings.maxRecentFiles = std::max(1, std::min(50, m_editSettings.maxRecentFiles));
            m_isDirty = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(1 - 50)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Maximum number of recent files to track in the File menu.");

        ImGui::PopID();
    }
    else if (m_selCategory == "Editor") {
        ImGui::TextWrapped("Editor behaviour settings.");
        ImGui::Spacing();

        // Auto-save toggle
        if (ImGui::Checkbox("Enable Auto-Save", &m_editSettings.autoSaveEnabled)) {
            m_isDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Automatically save projects in background.");

        // Auto-save interval (only when enabled)
        if (m_editSettings.autoSaveEnabled) {
            ImGui::PushItemWidth(120);
            if (ImGui::InputInt("Auto-Save Interval (minutes)", &m_editSettings.autoSaveIntervalMinutes)) {
                // clamp reasonable values
                m_editSettings.autoSaveIntervalMinutes = std::max(1, std::min(60, m_editSettings.autoSaveIntervalMinutes));
                m_isDirty = true;
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::TextDisabled("(1 - 60)");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How often the editor auto-saves when enabled.");
        }

        ImGui::Spacing();

        // maxUndoHistory
        ImGui::PushItemWidth(160);
        if (ImGui::InputInt("Max Undo History", &m_editSettings.maxUndoHistory)) {
            m_editSettings.maxUndoHistory = std::max(1, std::min(10000, m_editSettings.maxUndoHistory));
            m_isDirty = true;
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::TextDisabled("(1 - 10000)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Number of undo steps stored in memory.");

        ImGui::Spacing();

        if (ImGui::Checkbox("Show Debug Info", &m_editSettings.showDebugInfo)) {
            m_isDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle display of debug information in the node editor.");
    }
    else if (m_selCategory == "Appearance") {
        ImGui::TextWrapped("Theme / appearance settings.");
        ImGui::Spacing();

        // themeName combo
        const char* themes[] = { "Dark", "Light" };
        int current = 0;
        for (int i = 0; i < IM_ARRAYSIZE(themes); ++i) {
            if (m_editSettings.themeName == themes[i]) { current = i; break; }
        }
        if (ImGui::Combo("Theme", &current, themes, IM_ARRAYSIZE(themes))) {
            m_editSettings.themeName = themes[current];
            m_isDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select the editor theme.");
        ImGui::Spacing();

        if (ImGui::Checkbox("Show Grid in Node Editor", &m_editSettings.showGrid)) {
            m_isDirty = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle background grid visibility in the node editor.");

        if (ImGui::Checkbox("Show Resource Names in Node Editor", &m_editSettings.showResourceNames)) {
            m_isDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle display of resource names next to their icons in the node editor.");

        ImGui::Spacing();
        ImGuiIO& io = ImGui::GetIO();

        if (ImGui::BeginCombo("Fonts###Selector", m_editSettings.fontName.c_str()))
        {
            for (ImFont* font : io.Fonts->Fonts)
            {
                ImGui::PushID(font);
                if (ImGui::Selectable(font->GetDebugName(), font->GetDebugName() == m_editSettings.fontName)) {
                    m_editSettings.fontName = font->GetDebugName();
                    m_isDirty = true;
                }
                if (font->GetDebugName() == m_editSettings.fontName)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select the application font.");
        ImGui::Spacing();
        if (ImGui::DragFloat("FontSizeBase", &m_editSettings.fontSize, 0.20f, 5.0f, 100.0f, "%.0f")) {
            m_editSettings.fontSize = std::max(5.0f, std::min(100.0f, m_editSettings.fontSize));
            m_isDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Base font size for scaling UI elements.");
    }

    ImGui::EndChild();
}

void SettingsEditor::DrawBottomPanel() {
    ImGui::BeginChild("BottomPanel", ImVec2(0, 40), false);

    float panelWidth = ImGui::GetWindowWidth();
    float buttonWidth = 90.0f;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float totalWidth = buttonWidth * 2 + spacing;

    ImGui::SetCursorPosX(panelWidth - totalWidth - 10.0f); // small right margin

    // Apply button (only active when dirty)
    bool disabled = !m_isDirty;
    if (disabled) ImGui::BeginDisabled();
    if (ImGui::Button("Apply", ImVec2(buttonWidth, 0))) {
        ApplyChanges();
    }
    if (disabled) ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
        // revert edited values and close editor
        RevertChanges();
        m_isOpen = false;
    }

    ImGui::EndChild();
}


void SettingsEditor::StartEditing() {
    // pull from SettingsManager
    m_savedSettings = SettingsManager::instance().getSettings();
    m_editSettings = m_savedSettings;
    m_isDirty = false;

    // populate persistent buffers
    auto fillBuf = [](std::array<char, 1024>& buf, const std::filesystem::path& p) {
        std::string s = p.string();
        std::strncpy(buf.data(), s.c_str(), buf.size() - 1);
        buf[buf.size() - 1] = '\0';
    };

    fillBuf(m_bufDefaultProjectPath, m_editSettings.defaultProjectPath);
    fillBuf(m_bufGameDataPath, m_editSettings.gameDataPath);

    m_selCategory = "General";
}

void SettingsEditor::RevertChanges() {
    m_editSettings = m_savedSettings;
    m_isDirty = false;
    // restore buffers
    std::strncpy(m_bufDefaultProjectPath.data(),
                 m_savedSettings.defaultProjectPath.string().c_str(),
                 m_bufDefaultProjectPath.size() - 1);
    m_bufDefaultProjectPath[m_bufDefaultProjectPath.size() - 1] = '\0';

    std::strncpy(m_bufGameDataPath.data(),
                 m_savedSettings.gameDataPath.string().c_str(),
                 m_bufGameDataPath.size() - 1);
    m_bufGameDataPath[m_bufGameDataPath.size() - 1] = '\0';
}

void SettingsEditor::ApplyChanges() {
    // Update any fields that may not be in sync (buffers -> paths)
    m_editSettings.defaultProjectPath = std::string(m_bufDefaultProjectPath.data());
    m_editSettings.gameDataPath = std::string(m_bufGameDataPath.data());

    SettingsManager::instance().setSettings(m_editSettings);
    SettingsManager::instance().save();

    ImFont* font = nullptr;

    for (ImFont* f : ImGui::GetIO().Fonts->Fonts) {
        if (f->GetDebugName() == m_editSettings.fontName) {
            font = f;
            break;
        }
    }

    if (font) ImGui::GetIO().FontDefault = font;
    ImGui::GetStyle()._NextFrameFontSizeBase = m_editSettings.fontSize;
    // update saved copy + dirty flag
    m_savedSettings = m_editSettings;
    m_isDirty = false;
}