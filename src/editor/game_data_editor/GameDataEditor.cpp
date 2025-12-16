#include "GameDataEditor.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "services/NotificationManager.h"
#include "services/SettingsManager.h"
#include <iostream>
#include <map>
#include <set>
#include <cstring>
#include <nfd.h>
#include <thread>

template<typename T>
std::vector<std::pair<std::string, T*>> GameDataEditor::FilterMap(std::map<std::string, T>& sourceMap, const std::string& filterStr) {
    std::vector<std::pair<std::string, T*>> result;
    result.reserve(sourceMap.size());

    std::string searchLower = filterStr;
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

template std::vector<std::pair<std::string, Resource*>> GameDataEditor::FilterMap(std::map<std::string, Resource>&, const std::string&);
template std::vector<std::pair<std::string, Machine*>> GameDataEditor::FilterMap(std::map<std::string, Machine>&, const std::string&);
template std::vector<std::pair<std::string, Recipe*>> GameDataEditor::FilterMap(std::map<std::string, Recipe>&, const std::string&);

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
    if (m_iconDialog.isReady) {
        if (!m_iconDialog.resultPath.empty()) {
            std::string err;
            if (!gameDataManager.setResourceIcon(m_iconDialog.targetKey, m_iconDialog.resultPath, err)) {
                NotificationManager::instance().addNotification("Icon Error", err, NotificationType::Error);
            }
        }
        m_iconDialog.resultPath.clear();
        m_iconDialog.targetKey.clear();
        m_iconDialog.isReady = false;
        m_iconDialog.isRunning = false;
    }
    auto filteredItems = FilterMap(gameDataManager.current().resources, m_searchBuffer);

    int flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("ResourceGrid", 4, flags)) {
        ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(filteredItems.size());

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

        while (clipper.Step()) {
            for (int row_n = clipper.DisplayStart; row_n < clipper.DisplayEnd; row_n++) {
                auto* item = filteredItems[row_n].second;
                std::string& key = filteredItems[row_n].first;

                ImGui::PushID(key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.1f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1,1,1,0.2f));

                if (ImGui::ImageButton("##icon", (ImTextureID)(uintptr_t)item->texture, ImVec2(24, 24))) {
                    if (!m_iconDialog.isRunning) {
                        m_iconDialog.targetKey = key;
                        m_iconDialog.isRunning = true;

                        std::thread([this]() {
                            nfdu8char_t *outPath = nullptr;
                            nfdu8filteritem_t filters[1] = { { "Images", "png,jpg,jpeg" } };
                            nfdopendialogu8args_t args = {0};
                            args.filterList = filters;
                            args.filterCount = 1;

                            nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);

                            if (result == NFD_OKAY) {
                                m_iconDialog.resultPath = outPath;
                                NFD_FreePathU8(outPath);
                            }
                            m_iconDialog.isReady = true;
                        }).detach();
                    }
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("Click to change icon");
                }

                ImGui::PopStyleColor(3);

                // Column 2: Name
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);

                char buf[256];
                strncpy(buf, item->name.c_str(), sizeof(buf));
                buf[sizeof(buf) - 1] = '\0';

                if (ImGui::InputText("##name", buf, sizeof(buf))) {
                    item->name = std::string(buf);
                }

                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    std::string err;
                    Resource temp = *item;
                    gameDataManager.editResource(key, temp, err);
                }

                // Column 3: Key (ID)
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", key.c_str());

                // Column 4: Delete button
                ImGui::TableNextColumn();
                float rowHeight = 24.0f + (ImGui::GetStyle().FramePadding.y * 2.0f);
                float buttonSize = ImGui::GetFrameHeight();

                float availX = ImGui::GetContentRegionAvail().x;
                if (availX > buttonSize) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availX - buttonSize) * 0.5f);
                }

                float offsetY = (rowHeight - buttonSize) * 0.5f;
                if (offsetY > 0.0f) {
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
                }

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));

                if (ImGui::Button("X", ImVec2(buttonSize, buttonSize))) {
                    std::string err;
                    if (!gameDataManager.deleteResource(key, err)) {
                        NotificationManager::instance().addNotification("Delete Error", err, NotificationType::Error);
                    }
                }
                ImGui::PopStyleColor(2);

                ImGui::PopID();
            }
        }

        ImGui::PopStyleColor(2);

        ImGui::EndTable();
    }
}

void GameDataEditor::DrawMachineGrid() {
    if (m_iconDialog.isReady) {
        if (!m_iconDialog.resultPath.empty()) {
            std::string err;
            if (!gameDataManager.setMachineIcon(m_iconDialog.targetKey, m_iconDialog.resultPath, err)) {
                NotificationManager::instance().addNotification("Icon Error", err, NotificationType::Error);
            }
        }
        m_iconDialog.resultPath.clear();
        m_iconDialog.targetKey.clear();
        m_iconDialog.isReady = false;
        m_iconDialog.isRunning = false;
    }
    auto filteredItems = FilterMap(gameDataManager.current().machines, m_searchBuffer);

    int flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("MachineGrid", 4, flags)) {
        ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Speed", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(filteredItems.size());

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

        while (clipper.Step()) {
            for (int row_n = clipper.DisplayStart; row_n < clipper.DisplayEnd; row_n++) {
                auto* item = filteredItems[row_n].second;
                std::string& key = filteredItems[row_n].first;

                ImGui::PushID(key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.1f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1,1,1,0.2f));

                if (ImGui::ImageButton("##icon", (ImTextureID)(uintptr_t)item->texture, ImVec2(24, 24))) {
                    if (!m_iconDialog.isRunning) {
                        m_iconDialog.targetKey = key;
                        m_iconDialog.isRunning = true;

                        std::thread([this]() {
                            nfdu8char_t *outPath = nullptr;
                            nfdu8filteritem_t filters[1] = { { "Images", "png,jpg,jpeg" } };
                            nfdopendialogu8args_t args = {0};
                            args.filterList = filters;
                            args.filterCount = 1;

                            nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);

                            if (result == NFD_OKAY) {
                                m_iconDialog.resultPath = outPath;
                                NFD_FreePathU8(outPath);
                            }
                            m_iconDialog.isReady = true;
                        }).detach();
                    }
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("Click to change icon");
                }

                ImGui::PopStyleColor(3);

                // Column 2: Name
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);

                char buf[256];
                strncpy(buf, item->name.c_str(), sizeof(buf));
                buf[sizeof(buf) - 1] = '\0';

                if (ImGui::InputText("##name", buf, sizeof(buf))) {
                    item->name = std::string(buf);
                }

                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    std::string err;
                    Machine temp = *item;
                    gameDataManager.editMachine(key, temp, err);
                }

                // Column 3: Speed
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);

                char speedBuf[64];
                snprintf(speedBuf, sizeof(speedBuf), "%.2f", item->base_crafting_speed);

                if (ImGui::InputText("##speed", speedBuf, sizeof(speedBuf))) {
                    item->base_crafting_speed = std::stof(speedBuf);
                }

                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    std::string err;
                    Machine temp = *item;
                    gameDataManager.editMachine(key, temp, err);
                }

                // Column 4: Key (ID)
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", key.c_str());

                ImGui::PopID();
            }
        }
        ImGui::PopStyleColor(2);
        ImGui::EndTable();
    }
}

void GameDataEditor::DrawRecipeGrid() {
    auto filteredItems = FilterMap(gameDataManager.current().recipes, m_searchBuffer);

    int flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

    if (ImGui::BeginTable("RecipeGrid", 6, flags)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 150.0f);
        ImGui::TableSetupColumn("Time (sec)", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Inputs", ImGuiTableColumnFlags_WidthStretch, 375.0f);
        ImGui::TableSetupColumn("Outputs", ImGuiTableColumnFlags_WidthStretch, 225.0f);
        ImGui::TableSetupColumn("Produced in", ImGuiTableColumnFlags_WidthStretch, 150.0f);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 150.0f);  // Fixed width for ID
        ImGui::TableHeadersRow();


        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

        for (int row_n = 0; row_n < filteredItems.size(); row_n++) {
            auto* item = filteredItems[row_n].second;
            std::string& key = filteredItems[row_n].first;

            ImGui::PushID(key.c_str());
            ImGui::TableNextRow();

            // Column 1: Name
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);

            char buf[256];
            strncpy(buf, item->name.c_str(), sizeof(buf));
            buf[sizeof(buf) - 1] = '\0';

            if (ImGui::InputText("##name", buf, sizeof(buf))) {
                item->name = std::string(buf);
            }

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                std::string err;
                Recipe temp = *item;
                gameDataManager.editRecipe(key, temp, err);
            }

            // Column 2: Time
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);

            if (ImGui::InputDouble("##time", &item->time_seconds, 0.0f, 0.0f, "%.2f")) {
                // Value is updated automatically
            }

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                std::string err;
                Recipe temp = *item;
                gameDataManager.editRecipe(key, temp, err);
            }

            // Column 3: Inputs
            ImGui::TableNextColumn();
            if (DrawTokenList("##in", item->input_ports, true)) {
                std::string err;
                Recipe temp = *item;
                gameDataManager.editRecipe(key, temp, err);
            }

            // Column 4: Outputs
            ImGui::TableNextColumn();
            if (DrawTokenList("##out", item->output_ports, false)) {
                std::string err;
                Recipe temp = *item;
                gameDataManager.editRecipe(key, temp, err);
            }

            // Column 5: Produced in
            ImGui::TableNextColumn();
            if (DrawMachineList("##mach", item->produced_in_machines_keys)) {
                std::string err;
                Recipe temp = *item;
                gameDataManager.editRecipe(key, temp, err);
            }

            // Column 6: Key (ID)
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", key.c_str());
            ImGui::PopID();
        }
    }
    ImGui::PopStyleColor(2);
    ImGui::EndTable();
}

bool GameDataEditor::DrawTokenList(const char* str_id, std::vector<RecipePort>& ports, bool isInput) {
    bool changed = false;
    ImGui::PushID(str_id);

    ImGuiStyle& style = ImGui::GetStyle();
    auto* drawList = ImGui::GetWindowDrawList();
    auto fontSize = ImGui::GetFontSize();

    // Calculate Layout Limits (Screen Space)
    // We capture the "Right Edge" of the current column/cell.
    float startScreenX = ImGui::GetCursorScreenPos().x;
    float availWidth = ImGui::GetContentRegionAvail().x;
    float limitScreenX = startScreenX + availWidth;

    const float chipRounding = 4.0f;
    const ImVec2 chipPadding(4.0f, 2.0f);
    const float iconSize = 16.0f;
    ImVec4 chipBgColor = ImVec4(0.2f, 0.25f, 0.3f, 1.0f);
    ImVec4 chipHoverColor = ImVec4(0.3f, 0.35f, 0.45f, 1.0f);
    if (SettingsManager::instance().getSettings().themeName == "Light") {
        chipBgColor = ImVec4(0.8f, 0.85f, 0.9f, 1.0f);
        chipHoverColor = ImVec4(0.7f, 0.75f, 0.85f, 1.0f);
    }

    for (int i = 0; i < ports.size(); ++i) {
        ImGui::PushID(i);

        std::string resName = ports[i].resource_key;

        if (resName == "nothing") {
            ImGui::PopID();
            continue;
        }

        ImTextureID icon = 0;
        if (gameDataManager.current().resources.count(resName)) {
            const auto& res = gameDataManager.current().resources.at(resName);
            resName = res.name;
            icon = (ImTextureID)(uintptr_t)res.texture;
        }

        char label[128];
        snprintf(label, sizeof(label), "%s x%.2f", resName.c_str(), ports[i].amount);
        ImVec2 textSize = ImGui::CalcTextSize(label);

        float bodyWidth = chipPadding.x + (icon ? (iconSize + style.ItemInnerSpacing.x) : 0) + textSize.x + chipPadding.x;
        float xBtnWidth = ImGui::GetFrameHeight();
        float totalChipWidth = bodyWidth + xBtnWidth;

        if (i > 0) {
            float lastItemEndScreenX = ImGui::GetItemRectMax().x;
            float nextItemEndScreenX = lastItemEndScreenX + style.ItemSpacing.x + totalChipWidth;

            if (nextItemEndScreenX < limitScreenX) {
                ImGui::SameLine();
            }
        }

        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        float frameHeight = ImGui::GetFrameHeight();

        ImGui::SetCursorScreenPos(cursorPos);
        bool clickedBody = ImGui::InvisibleButton("##body", ImVec2(bodyWidth, frameHeight));
        bool hoveredBody = ImGui::IsItemHovered();

        drawList->AddRectFilled(
            cursorPos,
            ImVec2(cursorPos.x + bodyWidth, cursorPos.y + frameHeight),
            ImGui::GetColorU32(hoveredBody ? chipHoverColor : chipBgColor),
            chipRounding,
            ImDrawFlags_RoundCornersLeft
        );

        float contentX = cursorPos.x + chipPadding.x;
        float contentY = cursorPos.y + (frameHeight - iconSize) * 0.5f;

        if (icon) {
            drawList->AddImage(icon, ImVec2(contentX, contentY), ImVec2(contentX + iconSize, contentY + iconSize));
            contentX += iconSize + style.ItemInnerSpacing.x;
        }

        float textY = cursorPos.y + (frameHeight - textSize.y) * 0.5f;
        drawList->AddText(ImVec2(contentX, textY), ImGui::GetColorU32(ImGuiCol_Text), label);

        if (clickedBody) ImGui::OpenPopup("EditTokenPopup");

        ImGui::SameLine(0, 0);

        ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + bodyWidth, cursorPos.y));
        bool clickedX = ImGui::InvisibleButton("##x_btn", ImVec2(xBtnWidth, frameHeight));
        bool hoveredX = ImGui::IsItemHovered();
        bool activeX = ImGui::IsItemActive();

        ImVec4 xColor = ImVec4(chipBgColor.x * 0.8f, chipBgColor.y * 0.8f, chipBgColor.z * 0.8f, 1.0f); // Default Darker
        if (activeX) {
            xColor = ImVec4(xColor.x * 0.7f, xColor.y * 0.7f, xColor.z * 0.7f, 1.0f); // Clicked = Darkest
        } else if (hoveredX) {
            xColor = ImVec4(xColor.x * 1.3f, xColor.y * 1.3f, xColor.z * 1.3f, 1.0f); // Hovered = Lighter
        }

        if (SettingsManager::instance().getSettings().themeName == "Light") {
            xColor = ImVec4(chipBgColor.x * 1.1f, chipBgColor.y * 1.1f, chipBgColor.z * 1.1f, 1.0f); // Default Lighter
            if (activeX) {
                xColor = ImVec4(xColor.x * 0.7f, xColor.y * 0.7f, xColor.z * 0.7f, 1.0f); // Clicked = Darkest
            } else if (hoveredX) {
                xColor = ImVec4(xColor.x * 0.8f, xColor.y * 0.8f, xColor.z * 0.8f, 1.0f); // Hovered = Darker
            }
        }

        drawList->AddRectFilled(
            ImVec2(cursorPos.x + bodyWidth, cursorPos.y),
            ImVec2(cursorPos.x + bodyWidth + xBtnWidth, cursorPos.y + frameHeight),
            ImGui::GetColorU32(xColor),
            chipRounding,
            ImDrawFlags_RoundCornersRight
        );

        ImVec2 xTextSize = ImGui::CalcTextSize("x");
        float xTextX = cursorPos.x + bodyWidth + (xBtnWidth - xTextSize.x) * 0.5f;
        float xTextY = cursorPos.y + (frameHeight - xTextSize.y) * 0.5f;
        drawList->AddText(ImVec2(xTextX, xTextY), ImGui::GetColorU32(ImGuiCol_Text), "x");

        if (clickedX) {
            ports.erase(ports.begin() + i);
            changed = true;
            ImGui::PopID();
            continue;
        }

        if (ImGui::BeginPopup("EditTokenPopup")) {
            ImGui::TextDisabled("Edit Port");
            ImGui::Separator();

            ImGui::SetNextItemWidth(120);
            ImGui::InputDouble("Amount", &ports[i].amount, 1.0, 10.0, "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                changed = true;
            }

            ImGui::Separator();
            ImGui::TextDisabled("Change Resource");

            static char sbuf[64] = "";
            if (ImGui::IsWindowAppearing()) { sbuf[0] = 0; ImGui::SetKeyboardFocusHere(); }
            ImGui::InputTextWithHint("##s", "Search...", sbuf, 64);

            if (ImGui::BeginChild("L", ImVec2(-1, 200))) {
                auto resources = FilterMap(gameDataManager.current().resources, sbuf);

                for (const auto& pair : resources) {
                    const std::string& key = pair.first;
                    Resource* res = pair.second;
                    bool is_selected = (ports[i].resource_key == key);

                    ImVec2 start_pos = ImGui::GetCursorPos();

                    if (ImGui::Selectable(std::string("##" + key).c_str(), is_selected, ImGuiSelectableFlags_None)) {
                        ports[i].resource_key = key;
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::SetItemAllowOverlap();

                    ImGui::SetCursorPos(start_pos);

                    ImGui::Image(res->texture, ImVec2(fontSize, fontSize));
                    ImGui::SameLine();
                    ImGui::TextUnformatted(res->name.c_str());

                    ImGui::Dummy(ImVec2(0.0f, 0.0f));
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    float lastItemEndScreenX = ImGui::GetItemRectMax().x;
    float addBtnWidth = 24.0f;
    float frameHeight = ImGui::GetFrameHeight();

    if (!ports.empty() && (lastItemEndScreenX + style.ItemSpacing.x + addBtnWidth < limitScreenX)) {
        ImGui::SameLine();
    }

    ImGui::PushStyleColor(ImGuiCol_Button, chipBgColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, chipHoverColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(chipHoverColor.x * 0.9f, chipHoverColor.y * 0.9f, chipHoverColor.z * 0.9f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    if (ImGui::Button("+", ImVec2(addBtnWidth, frameHeight))) {
        m_tokenPopupState.targetVector = &ports;
        m_tokenPopupState.searchBuf[0] = '\0';
        ImGui::OpenPopup("AddTokenPopup");
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    if (ImGui::BeginPopup("AddTokenPopup")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##s", "Search...", m_tokenPopupState.searchBuf, 128);
        ImGui::Separator();

        if (ImGui::BeginChild("AddL", ImVec2(-1, 200))) {
            auto resources = FilterMap(gameDataManager.current().resources, m_tokenPopupState.searchBuf);

            for (const auto& pair : resources) {
                const std::string& key = pair.first;
                Resource* res = pair.second;
                bool is_selected = false;

                ImVec2 start_pos = ImGui::GetCursorPos();

                if (ImGui::Selectable(std::string("##" + key).c_str(), is_selected, ImGuiSelectableFlags_None)) {
                    if (m_tokenPopupState.targetVector) {
                        m_tokenPopupState.targetVector->push_back({1.0, key});
                        m_tokenPopupState.isInput = isInput;
                        changed = true;
                    }
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SetItemAllowOverlap();

                ImGui::SetCursorPos(start_pos);

                ImGui::Image(res->texture, ImVec2(fontSize, fontSize));
                ImGui::SameLine();
                ImGui::TextUnformatted(res->name.c_str());

                ImGui::Dummy(ImVec2(0.0f, 0.0f));
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return changed;
}

bool GameDataEditor::DrawMachineList(const char* str_id, std::vector<std::string>& machineKeys) {
    bool changed = false;
    ImGui::PushID(str_id);

    ImGuiStyle& style = ImGui::GetStyle();
    auto* drawList = ImGui::GetWindowDrawList();
    auto fontSize = ImGui::GetFontSize();

    // Calculate Layout Limits (Screen Space)
    // We capture the "Right Edge" of the current column/cell.
    float startScreenX = ImGui::GetCursorScreenPos().x;
    float availWidth = ImGui::GetContentRegionAvail().x;
    float limitScreenX = startScreenX + availWidth;

    const float chipRounding = 4.0f;
    const ImVec2 chipPadding(4.0f, 2.0f);
    const float iconSize = 16.0f;
    ImVec4 chipBgColor = ImVec4(0.2f, 0.25f, 0.3f, 1.0f);
    ImVec4 chipHoverColor = ImVec4(0.3f, 0.35f, 0.45f, 1.0f);
    if (SettingsManager::instance().getSettings().themeName == "Light") {
        chipBgColor = ImVec4(0.8f, 0.85f, 0.9f, 1.0f);
        chipHoverColor = ImVec4(0.7f, 0.75f, 0.85f, 1.0f);
    }

    for (int i = 0; i < machineKeys.size(); ++i) {
        ImGui::PushID(i);

        std::string machName = machineKeys[i];

        if (machName == "nothing") {
            ImGui::PopID();
            continue;
        }

        ImTextureID icon = 0;
        if (gameDataManager.current().machines.count(machName)) {
            const auto& mach = gameDataManager.current().machines.at(machName);
            machName = mach.name;
            icon = (ImTextureID)(uintptr_t)mach.texture;
        }

        ImVec2 textSize = ImGui::CalcTextSize(machName.c_str());
        float bodyWidth = chipPadding.x + (icon ? (iconSize + style.ItemInnerSpacing.x) : 0) + textSize.x + chipPadding.x;
        float xBtnWidth = ImGui::GetFrameHeight();
        float totalChipWidth = bodyWidth + xBtnWidth;

        if (i > 0) {
            float lastItemEndScreenX = ImGui::GetItemRectMax().x;
            float nextItemEndScreenX = lastItemEndScreenX + style.ItemSpacing.x + totalChipWidth;
            if (nextItemEndScreenX < limitScreenX) {
                ImGui::SameLine();
            }
        }

        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        float frameHeight = ImGui::GetFrameHeight();

        ImGui::SetCursorScreenPos(cursorPos);
        ImGui::Dummy(ImVec2(bodyWidth, frameHeight));

        drawList->AddRectFilled(
            cursorPos,
            ImVec2(cursorPos.x + bodyWidth, cursorPos.y + frameHeight),
            ImGui::GetColorU32(chipBgColor),
            chipRounding,
            ImDrawFlags_RoundCornersLeft
        );

        float contentX = cursorPos.x + chipPadding.x;
        float contentY = cursorPos.y + (frameHeight - iconSize) * 0.5f;

        if (icon) {
            drawList->AddImage(icon, ImVec2(contentX, contentY), ImVec2(contentX + iconSize, contentY + iconSize));
            contentX += iconSize + style.ItemInnerSpacing.x;
        }

        float textY = cursorPos.y + (frameHeight - textSize.y) * 0.5f;
        drawList->AddText(ImVec2(contentX, textY), ImGui::GetColorU32(ImGuiCol_Text), machName.c_str());

        ImGui::SameLine(0, 0);

        ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + bodyWidth, cursorPos.y));
        bool clickedX = ImGui::InvisibleButton("##x_btn", ImVec2(xBtnWidth, frameHeight));
        bool hoveredX = ImGui::IsItemHovered();
        bool activeX = ImGui::IsItemActive();

        ImVec4 xColor = ImVec4(chipBgColor.x * 0.8f, chipBgColor.y * 0.8f, chipBgColor.z * 0.8f, 1.0f); // Default Darker
        if (activeX) {
            xColor = ImVec4(xColor.x * 0.7f, xColor.y * 0.7f, xColor.z * 0.7f, 1.0f); // Clicked = Darkest
        } else if (hoveredX) {
            xColor = ImVec4(xColor.x * 1.3f, xColor.y * 1.3f, xColor.z * 1.3f, 1.0f); // Hovered = Lighter
        }

        if (SettingsManager::instance().getSettings().themeName == "Light") {
            xColor = ImVec4(chipBgColor.x * 1.1f, chipBgColor.y * 1.1f, chipBgColor.z * 1.1f, 1.0f); // Default Lighter
            if (activeX) {
                xColor = ImVec4(xColor.x * 0.7f, xColor.y * 0.7f, xColor.z * 0.7f, 1.0f); // Clicked = Darkest
            } else if (hoveredX) {
                xColor = ImVec4(xColor.x * 0.8f, xColor.y * 0.8f, xColor.z * 0.8f, 1.0f); // Hovered = Darker
            }
        }

        drawList->AddRectFilled(
            ImVec2(cursorPos.x + bodyWidth, cursorPos.y),
            ImVec2(cursorPos.x + bodyWidth + xBtnWidth, cursorPos.y + frameHeight),
            ImGui::GetColorU32(xColor),
            chipRounding,
            ImDrawFlags_RoundCornersRight
        );

        ImVec2 xTextSize = ImGui::CalcTextSize("x");
        float xTextX = cursorPos.x + bodyWidth + (xBtnWidth - xTextSize.x) * 0.5f;
        float xTextY = cursorPos.y + (frameHeight - xTextSize.y) * 0.5f;
        drawList->AddText(ImVec2(xTextX, xTextY), ImGui::GetColorU32(ImGuiCol_Text), "x");

        if (clickedX) {
            machineKeys.erase(machineKeys.begin() + i);
            changed = true;
            ImGui::PopID();
            continue;
        }
        ImGui::PopID();
    }

    float lastItemEndScreenX = ImGui::GetItemRectMax().x;
    float addBtnWidth = 24.0f;
    float frameHeight = ImGui::GetFrameHeight();

    if (!machineKeys.empty() && (lastItemEndScreenX + style.ItemSpacing.x + addBtnWidth < limitScreenX)) {
        ImGui::SameLine();
    }

    ImGui::PushStyleColor(ImGuiCol_Button, chipBgColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, chipHoverColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(chipHoverColor.x * 0.9f, chipHoverColor.y * 0.9f, chipHoverColor.z * 0.9f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    if (ImGui::Button("+", ImVec2(addBtnWidth, frameHeight))) {
        ImGui::OpenPopup("AddMachinePopup");
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    if (ImGui::BeginPopup("AddMachinePopup")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##s", "Search...", m_tokenPopupState.searchBuf, 128);
        ImGui::Separator();

        if (ImGui::BeginChild("AddL", ImVec2(-1, 200))) {
            auto machines = FilterMap(gameDataManager.current().machines, m_tokenPopupState.searchBuf);

            for (const auto& pair : machines) {
                const std::string& key = pair.first;
                Machine* mach = pair.second;

                bool alreadyHas = false;
                for (const auto& existing : machineKeys) {
                    if (existing == key) {
                        alreadyHas = true;
                        break;
                    }
                }
                if (alreadyHas) continue;

                bool is_selected = false;

                ImVec2 start_pos = ImGui::GetCursorPos();

                if (ImGui::Selectable(std::string("##" + key).c_str(), is_selected, ImGuiSelectableFlags_None)) {
                    machineKeys.push_back(key);
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SetItemAllowOverlap();

                ImGui::SetCursorPos(start_pos);

                ImGui::Image(mach->texture, ImVec2(fontSize, fontSize));
                ImGui::SameLine();
                ImGui::TextUnformatted(mach->name.c_str());

                ImGui::Dummy(ImVec2(0.0f, 0.0f));
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return changed;
}

void GameDataEditor::DrawContextPane() {
    ImGui::TextDisabled("Context Pane");
    ImGui::Separator();
}