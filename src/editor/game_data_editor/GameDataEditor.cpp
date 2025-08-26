#include "GameDataEditor.h"
#include <imgui.h>

#include "FilesystemUtils.h"
#include "SettingsManager.h"

GameDataEditor::GameDataEditor() {
    gameDataManager.clear();
};

void GameDataEditor::Draw() {
    if (!m_isOpen) return;

    int flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(800, 650));
    ImGui::Begin("Game Data Editor", &m_isOpen, flags);
    ImGui::PopStyleVar();

    DrawLeftSide();

    ImGui::SameLine();

    DrawRightSide();

    ImGui::End();
}

void GameDataEditor::DrawLeftSide() {
    ImGui::BeginChild("LeftPanel", ImVec2(200, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

    // Button to open the "New File" popup
    if (ImGui::Button("+ New Game Data File", ImVec2(-1, 0))) {
        ImGui::OpenPopup("New File");
    }

    ImGui::Separator();

    // List all game data files
    const auto& gameDataPath = SettingsManager::instance().getSettings().gameDataPath;
    std::vector<std::filesystem::path> files = GetGameDataFiles(gameDataPath);

    for (const auto& file : files) {
        const bool isSelected = (m_currentlyEditingFile.filename() == file.filename());
        if (ImGui::Selectable(file.filename().string().c_str(), isSelected)) {
            m_currentlyEditingFile = gameDataPath / file.filename();
            if (!gameDataManager.loadFromFile(m_currentlyEditingFile, m_fileLoadError)) {
                m_currentlyEditingFile.clear();
            } else {
                // Clear any previous error message on successful load
                m_fileLoadError.clear();
            }
        }
    }

    // Display the file loading error, if any, at the bottom of the list
    if (!m_fileLoadError.empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", m_fileLoadError.c_str());
    }

    DrawNewFileDialog();

    ImGui::EndChild();
}

void GameDataEditor::DrawNewFileDialog() {
    // Always center the popup when it appears
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("New File", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char fileNameBuffer[128] = "";
        static std::string errorMessage = "";

        // Auto-focus the input field when the popup opens
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
            // Clear state from previous openings
            fileNameBuffer[0] = '\0';
            errorMessage.clear();
        }

        ImGui::Text("Enter new file name:");
        ImGui::PushItemWidth(-1);

        // Request file creation on pressing Enter or clicking the "Create" button
        bool createRequested = ImGui::InputText("##filename", fileNameBuffer, sizeof(fileNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();

        ImGui::Spacing();

        if (ImGui::Button("Create", ImVec2(120, 0))) {
            createRequested = true;
        }

        if (createRequested) {
            std::string filenameStr(fileNameBuffer);
            if (filenameStr.empty()) {
                errorMessage = "File name cannot be empty.";
            } else {
                std::filesystem::path newFilePath = SettingsManager::instance().getSettings().gameDataPath / filenameStr;
                newFilePath.replace_extension(".json");

                std::string saveError;
                gameDataManager.createNew("New Game", "seconds");
                if (gameDataManager.saveToFile(newFilePath, saveError)) {
                    m_currentlyEditingFile = newFilePath;
                    gameDataManager.loadFromFile(m_currentlyEditingFile, m_fileLoadError); // Load the new file
                    m_fileLoadError.clear(); // Clear any old loading errors
                    ImGui::CloseCurrentPopup();
                } else {
                    errorMessage = "Failed to save file: " + saveError;
                }
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        // Display error message if something went wrong during creation
        if (!errorMessage.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", errorMessage.c_str());
        }

        ImGui::EndPopup();
    }
}
void GameDataEditor::DrawRightSide() {
    ImGui::BeginChild("RightPanel", ImVec2(0, 0));

    // Check if a file is loaded
    if (m_currentlyEditingFile.empty()) {
        ImGui::TextDisabled("Select a game data file from the left panel to begin editing.");
        ImGui::EndChild();
        return;
    }
    ImGui::EndChild();
}
