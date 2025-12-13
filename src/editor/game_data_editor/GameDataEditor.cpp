#include "GameDataEditor.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "services/NotificationManager.h"
#include "services/SettingsManager.h"
#include <iostream>
#include <map>
#include <set>
#include <cstring>

template<typename T>
std::vector<std::pair<std::string, T*>> GameDataEditor::FilterMap(std::map<std::string, T>& sourceMap) {
    std::vector<std::pair<std::string, T*>> result;
    result.reserve(sourceMap.size());

    std::string searchLower = m_searchBuffer;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

    for (auto& [key, item] : sourceMap) {
        // Skip internal items
        if (key == "nothing") continue;

        bool match = true;
        if (!searchLower.empty()) {
            std::string nameLower = item.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

            // Fuzzy search: name OR key
            if (nameLower.find(searchLower) == std::string::npos &&
                key.find(searchLower) == std::string::npos) {
                match = false;
                }
        }

        if (match) {
            result.push_back({key, &item});
        }
    }

    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.second->name < b.second->name;
    });

    return result;
}

template std::vector<std::pair<std::string, Resource*>> GameDataEditor::FilterMap(std::map<std::string, Resource>&);
template std::vector<std::pair<std::string, Machine*>> GameDataEditor::FilterMap(std::map<std::string, Machine>&);
template std::vector<std::pair<std::string, Recipe*>> GameDataEditor::FilterMap(std::map<std::string, Recipe>&);

GameDataEditor::GameDataEditor(GameDataManager& manager) : gameDataManager(manager) {
    RefreshPackageList();
}

void GameDataEditor::SetOpen(bool open) {
    m_isOpen = open;
    if (open) {
        RefreshPackageList();
    }
}

void GameDataEditor::RefreshPackageList() {
    std::filesystem::path path = SettingsManager::instance().getSettings().gameDataPath;
    m_packages = ScanForGameData(path);
}

void GameDataEditor::Draw() {
    if (!m_isOpen) return;

    ImGui::SetNextWindowSize(ImVec2(1280, 720), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Game Data Manager", &m_isOpen)) {

        m_isFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        static float browserWidth = 250.0f;

        ImGui::BeginChild("PackageBrowser", ImVec2(browserWidth, 0), true);
        DrawPackageBrowser();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginGroup();
        DrawEditorWorkspace();
        ImGui::EndGroup();

        if (m_requestNewFilePopup) {
            ImGui::OpenPopup("New File");
            m_requestNewFilePopup = false;
        }
        DrawNewFileDialog();
    }
    ImGui::End();
}

void GameDataEditor::DrawNewFileDialog() {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("New File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char gameNameBuffer[128] = "";
        static char fileNameBuffer[128] = "";
        static std::string errorMessage;

        // Autofocus the input field when the popup opens
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
            // Clear state from previous openings
            gameNameBuffer[0] = '\0';
            fileNameBuffer[0] = '\0';
            errorMessage.clear();
        }

        ImGui::Text("Game Name:");
        // Collect unique existing game names for the dropdown
        std::set<std::string> existingGames;
        for (const auto& pkg : m_packages) {
            existingGames.insert(pkg.gameName);
        }

        ImGui::PushItemWidth(-1);
        if (ImGui::BeginCombo("##gamename_combo", gameNameBuffer, ImGuiComboFlags_PopupAlignLeft)) {
            for (const auto& game : existingGames) {
                bool isSelected = (game == gameNameBuffer);
                if (ImGui::Selectable(game.c_str(), isSelected)) {
                    strncpy(gameNameBuffer, game.c_str(), sizeof(gameNameBuffer) - 1);
                    gameNameBuffer[sizeof(gameNameBuffer) - 1] = '\0';
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::InputTextWithHint("##gamename_input", "Enter new or select existing...", gameNameBuffer, sizeof(gameNameBuffer));
        ImGui::PopItemWidth();

        ImGui::Spacing();

        ImGui::Text("File name:");
        ImGui::PushItemWidth(-1);

        // Request file creation on pressing Enter or clicking the "Create" button
        bool createRequested = ImGui::InputText("##filename", fileNameBuffer, sizeof(fileNameBuffer),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();

        ImGui::Spacing();

        if (ImGui::Button("Create", ImVec2(120, 0))) {
            createRequested = true;
        }

        if (createRequested) {
            std::string gameNameStr(gameNameBuffer);
            std::string filenameStr(fileNameBuffer);
            if (gameNameStr.empty()) {
                errorMessage = "Game name cannot be empty.";
            } else if (filenameStr.empty()) {
                errorMessage = "File name cannot be empty.";
            } else {
                const auto &gameDataPath = SettingsManager::instance().getSettings().gameDataPath;
                std::filesystem::path gameDir = gameDataPath / gameNameStr;
                std::filesystem::path dataDir = gameDir / "game_datas";

                std::error_code ec;
                std::filesystem::create_directories(dataDir, ec);

                if (ec) {
                    errorMessage = "Failed to create directories: " + ec.message();
                } else {
                    std::filesystem::path newFilePath = dataDir / filenameStr;
                    if (newFilePath.extension() != ".gd") {
                        newFilePath += ".gd";
                    }
                    if (std::filesystem::exists(newFilePath)) {
                        errorMessage = "File already exists.";
                    } else {
                        std::string saveError;
                        gameDataManager.createNew(gameNameStr, "seconds");

                        if (gameDataManager.saveToFile(newFilePath.string(), saveError)) {
                            std::string loadError;
                            if (gameDataManager.loadFromFile(newFilePath.string(), loadError)) {
                                RefreshPackageList();
                                ImGui::CloseCurrentPopup();
                                NotificationManager::instance().addNotification("File Created", "New game data file created successfully", NotificationType::Success);
                            } else {
                                errorMessage = "File saved but failed to load: " + loadError;
                            }
                        } else {
                            errorMessage = "Failed to save file: " + saveError;
                        }
                    }
                }
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        if (!errorMessage.empty()) {
            ImGui::Spacing();
            NotificationManager::instance().addNotification("Error", errorMessage, NotificationType::Error);
        }

        ImGui::EndPopup();
    }
}

void GameDataEditor::DrawPackageBrowser() {
    ImGui::TextDisabled("Available Packages");
    ImGui::Separator();

    if (ImGui::Button("Refresh")) {
        RefreshPackageList();
    }
    ImGui::SameLine();

    if (ImGui::Button("New File")) {
        m_requestNewFilePopup = true;
    }

    ImGui::Spacing();

    if (m_packages.empty()) {
        ImGui::TextWrapped("No game data found in settings path.");
        return;
    }

    std::map<std::string, std::vector<int>> groupedMap;
    for (int i = 0; i < static_cast<int>(m_packages.size()); ++i) {
        groupedMap[m_packages[i].gameName].push_back(i);
    }

    for (const auto& pair : groupedMap) {
        bool nodeOpen = ImGui::TreeNodeEx(pair.first.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (nodeOpen) {
            for (int idx : pair.second) {
                const auto& pkg = m_packages[idx];
                bool isSelected = (m_selectedPackageIndex == idx);

                if (ImGui::Selectable(pkg.dataName.c_str(), isSelected)) {
                    m_selectedPackageIndex = idx;
                    std::string error;
                    if (!gameDataManager.loadFromFile(pkg.dataFilePath.string(), error)) {
                        NotificationManager::instance().addNotification("Error", error, NotificationType::Error);
                    }
                }
            }
            ImGui::TreePop();
        }
    }
}

void GameDataEditor::DrawEditorWorkspace() {
    DrawTopMenuBar();
    float availHeight = ImGui::GetContentRegionAvail().y;

    if (m_showContextPane) {
        float availWidth = ImGui::GetContentRegionAvail().x;
        float contextWidth = 300.0f;
        float gridWidth = availWidth - contextWidth - ImGui::GetStyle().ItemSpacing.x;

        ImGui::BeginChild("CentralArea", ImVec2(gridWidth, availHeight), false);
        DrawCentralWorkspace();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("ContextPane", ImVec2(contextWidth, availHeight), true);
        DrawContextPane();
        ImGui::EndChild();
    } else {
        ImGui::BeginChild("CentralArea", ImVec2(0, availHeight), false);
        DrawCentralWorkspace();
        ImGui::EndChild();
    }
}

void GameDataEditor::Save() {
    std::string err;
    std::string path = gameDataManager.current().gameDataFilePath.string();

    if (path.empty()) {
        NotificationManager::instance().addNotification("Save Failed", "No file path associated with current game data.", NotificationType::Warning);
        return;
    }

    if (!gameDataManager.saveToFile(path, err)) {
        NotificationManager::instance().addNotification("Save Failed", err, NotificationType::Error);
    } else {
        NotificationManager::instance().addNotification("Saved", "Game Data saved successfully", NotificationType::Success);
    }
}

void GameDataEditor::DrawTopMenuBar() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));

    if (ImGui::Button(m_activeTab == GameDataTab::General ? "[ General ]" : "  General  ")) {
        m_activeTab = GameDataTab::General;
    }
    ImGui::SameLine();

    if (ImGui::Button(m_activeTab == GameDataTab::Resources ? "[ Resources ]" : "  Resources  ")) {
        m_activeTab = GameDataTab::Resources;
    }
    ImGui::SameLine();
    if (ImGui::Button(m_activeTab == GameDataTab::Machines ? "[ Machines ]" : "  Machines  ")) {
        m_activeTab = GameDataTab::Machines;
    }
    ImGui::SameLine();
    if (ImGui::Button(m_activeTab == GameDataTab::Recipes ? "[ Recipes ]" : "  Recipes  ")) {
        m_activeTab = GameDataTab::Recipes;
    }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    ImGui::BeginDisabled(m_activeTab == GameDataTab::General);
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##search", "Search...", m_searchBuffer, sizeof(m_searchBuffer));
    ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button(m_showContextPane ? "Hide Context >>" : "Show Context <<")) {
        m_showContextPane = !m_showContextPane;
    }

    ImGui::PopStyleVar();
    ImGui::Separator();
}

void GameDataEditor::DrawCentralWorkspace() {
    switch (m_activeTab) {
        case GameDataTab::General:
            DrawGeneralTab();
            break;
        case GameDataTab::Resources:
            DrawResourceGrid();
            break;
        case GameDataTab::Machines:
            DrawMachineGrid();
            break;
        case GameDataTab::Recipes:
            DrawRecipeGrid();
            break;
    }
}

void GameDataEditor::DrawGeneralTab() {
    GameData& data = gameDataManager.current();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
    ImGui::Spacing();

    ImGui::Text("File Metadata");
    ImGui::Separator();

    ImGui::TextDisabled("File Path:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", data.gameDataFilePath.string().c_str());

    ImGui::TextDisabled("UUID:");
    ImGui::SameLine();
    ImGui::Text("%s", data.uuid.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Game Settings");

    static char gameNameBuf[128];
    if (ImGui::IsWindowAppearing()) {
        strncpy(gameNameBuf, data.gameName.c_str(), sizeof(gameNameBuf));
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Game Name:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(300);
    if (ImGui::InputText("##gamename", gameNameBuf, sizeof(gameNameBuf))) {
        data.gameName = gameNameBuf;
    }

    ImGui::Spacing();

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Time Unit:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo("##timeunit", data.time_unit.c_str())) {
        if (ImGui::Selectable("seconds", data.time_unit == "seconds")) data.time_unit = "seconds";
        if (ImGui::Selectable("minutes", data.time_unit == "minutes")) data.time_unit = "minutes";
        if (ImGui::Selectable("hours", data.time_unit == "hours")) data.time_unit = "hours";
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Base unit for recipe times (visual only)");

    ImGui::Separator();

    auto errors = gameDataManager.validate(gameDataManager.current());
    if (errors.empty()) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Validation successful");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Validation Errors (%zu):", errors.size());
        for(size_t i = 0; i < errors.size(); i++) {
            ImGui::Indent(10.0f);
            ImGui::TextUnformatted(errors[i].c_str());
        }
    }

    ImGui::PopStyleVar();
}

void GameDataEditor::DrawResourceGrid() {
    // Get Data (Filtered & Sorted)
    auto filteredItems = FilterMap(gameDataManager.current().resources);

    int flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("ResourceGrid", 3, flags)) {
        ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 32.0f); // Fixed width for icon
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);      // Stretches to fill space
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 150.0f);  // Fixed width for ID
        ImGui::TableHeadersRow();

        // Render Rows with Clipper
        ImGuiListClipper clipper;
        clipper.Begin(filteredItems.size());

        while (clipper.Step()) {
            for (int row_n = clipper.DisplayStart; row_n < clipper.DisplayEnd; row_n++) {
                auto* item = filteredItems[row_n].second;
                std::string& key = filteredItems[row_n].first;

                ImGui::PushID(key.c_str()); // Ensure stable ID for UI interactions
                ImGui::TableNextRow();

                // Column 1: Icon
                ImGui::TableNextColumn();
                if (item->texture) {
                    ImGui::Image((ImTextureID)(uintptr_t)item->texture, ImVec2(24, 24));
                }

                // Column 2: Name
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(item->name.c_str());

                // Column 3: Key (ID)
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", key.c_str());

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }
}

void GameDataEditor::DrawMachineGrid() {
    auto filteredItems = FilterMap(gameDataManager.current().machines);

    int flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("MachineGrid", 4, flags)) {
        ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 32.0f); // Fixed width for icon
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);      // Stretches to fill space
        ImGui::TableSetupColumn("Speed", ImGuiTableColumnFlags_WidthFixed, 100.0f); // Fixed width for Speed
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 150.0f);  // Fixed width for ID
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(filteredItems.size());

        while (clipper.Step()) {
            for (int row_n = clipper.DisplayStart; row_n < clipper.DisplayEnd; row_n++) {
                auto* item = filteredItems[row_n].second;
                std::string& key = filteredItems[row_n].first;

                ImGui::PushID(key.c_str());
                ImGui::TableNextRow();

                // Column 1: Icon
                ImGui::TableNextColumn();
                if (item->texture) {
                    ImGui::Image((ImTextureID)(uintptr_t)item->texture, ImVec2(24, 24));
                }

                // Column 2: Name
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(item->name.c_str());

                // Column 3: Speed
                ImGui::TableNextColumn();
                ImGui::Text("%.2fx", item->base_crafting_speed);

                // Column 4: Key (ID)
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", key.c_str());

                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
}

void GameDataEditor::DrawRecipeGrid() {
    auto filteredItems = FilterMap(gameDataManager.current().recipes);

    int flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("RecipeGrid", 6, flags)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 250.0f);
        ImGui::TableSetupColumn("Time (sec)", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Inputs", ImGuiTableColumnFlags_WidthStretch, 250.0f);
        ImGui::TableSetupColumn("Outputs", ImGuiTableColumnFlags_WidthStretch, 250.0f);
        ImGui::TableSetupColumn("Produced in", ImGuiTableColumnFlags_WidthStretch, 150.0f);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 150.0f);  // Fixed width for ID
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(filteredItems.size());

        while (clipper.Step()) {
            for (int row_n = clipper.DisplayStart; row_n < clipper.DisplayEnd; row_n++) {
                auto* item = filteredItems[row_n].second;
                std::string& key = filteredItems[row_n].first;

                ImGui::PushID(key.c_str());
                ImGui::TableNextRow();

                // Column 1: Name
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(item->name.c_str());

                // Column 2: Time
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", item->time_seconds);

                // Column 3: Inputs
                ImGui::TableNextColumn();
                for (const auto& input : item->input_ports) {
                    ImGui::Text("%s", input.resource_key.c_str());
                }

                // Column 4: Outputs
                ImGui::TableNextColumn();
                for (const auto& output : item->output_ports) {
                    ImGui::Text("%s", output.resource_key.c_str());
                }

                // Column 5: Produced in
                ImGui::TableNextColumn();
                for (const auto& machineKey : item->produced_in_machines_keys) {
                    ImGui::Text("%s", machineKey.c_str());
                }

                // Column 6: Key (ID)
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", key.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
}

void GameDataEditor::DrawContextPane() {
    ImGui::TextDisabled("Context Pane");
    ImGui::Separator();
}