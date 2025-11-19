#include "GameDataEditor.h"
#include <imgui.h>
#include <iostream>

#include "common/FilesystemUtils.h"
#include "core/data/GameDataScanner.h"
#include "services/SettingsManager.h"
#include "common/StringUtils.h"

GameDataEditor::GameDataEditor(GameDataManager& manager) : gameDataManager(manager) {
    gameDataManager.clear();
    RefreshPackageList();
};

void GameDataEditor::RefreshPackageList() {
    const auto &gameDataPath = SettingsManager::instance().getSettings().gameDataPath;
    m_cachedPackages = ScanForGameData(gameDataPath);
}

void GameDataEditor::Draw() {
    if (!m_isOpen) return;

    int flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(900, 700));
    ImGui::Begin("Game Data Editor", &m_isOpen, flags);
    ImGui::PopStyleVar();

    DrawLeftSide();

    ImGui::SameLine();

    DrawRightSide();

    ImGui::End();
}

void GameDataEditor::DrawLeftSide() {
    ImGui::BeginChild("LeftPanel", ImVec2(200, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

    const float refresh_button_width = 60.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float total_avail_width = ImGui::GetContentRegionAvail().x;
    const float new_file_button_width = total_avail_width - refresh_button_width - spacing;
    // Button to open the "New File" popup
    if (ImGui::Button("+ New Game Data File", ImVec2(new_file_button_width, 0))) {
        ImGui::OpenPopup("New File");
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(refresh_button_width, 0))) {
        RefreshPackageList();
    }

    ImGui::Separator();

    // List all game data files
    const auto &gameDataPath = SettingsManager::instance().getSettings().gameDataPath;
    const std::vector<GameDataPackage>& packages = m_cachedPackages;

    // Helper structs for sorting
    struct FoldableGroup {
        std::string gameName;
        // {dataName, fullPath}
        std::vector<std::pair<std::string, std::filesystem::path>> items;
    };

    struct SelectableItem {
        std::string gameName;
        std::string dataName;
        std::filesystem::path fullPath;
    };

    // Temporary map to group packages by gameName
    std::map<std::string, std::vector<std::pair<std::string, std::filesystem::path>>> groupedMap;
    for (const auto& pkg : packages) {
        // Use dataName for the display and dataFilePath for the action
        groupedMap[pkg.gameName].emplace_back(pkg.dataName, pkg.dataFilePath);
    }

    // Two lists to store the two types of UI elements
    std::vector<FoldableGroup> foldableGroups;
    std::vector<SelectableItem> selectableItems;

    // Sort the map into the two lists
    for (const auto& pair : groupedMap) {
        const std::string& gameName = pair.first;
        const auto& items = pair.second;

        if (items.size() == 1) {
            // Rule A: Only one item, add to selectableItems
            const auto& item = items[0];
            selectableItems.push_back({gameName, item.first, item.second});
        } else {
            // Rule B: More than one item, add to foldableGroups
            foldableGroups.push_back({gameName, items});
        }
    }

    // Render Foldable Groups first
    for (const auto& group : foldableGroups) {
        if (ImGui::TreeNode(group.gameName.c_str())) {
            for (const auto& item : group.items) {
                // item.first is dataName, item.second is fullPath
                const bool isSelected = (m_currentlyEditingFile == item.second);
                if (ImGui::Selectable(item.first.c_str(), isSelected)) {
                    m_currentlyEditingFile = item.second; // Use the full path
                    // clear all buffers
                    selResourceKey.clear();
                    selMachineKey.clear();
                    selRecipeKey.clear();
                    resourceDirty = false;
                    machineDirty = false;
                    recipeDirty = false;

                    if (!gameDataManager.loadFromFile(m_currentlyEditingFile.string(), m_fileLoadError)) {
                        // Loading failed, error message is in m_fileLoadError
                    } else {
                        m_fileLoadError.clear();
                    }
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::TreePop();
        }
    }

    if (!foldableGroups.empty() && !selectableItems.empty()) {
        ImGui::Separator();
    }

    // Render Single Selectable Items last
    for (const auto& item : selectableItems) {
        // Display as "GameName - DataName" for context
        std::string displayName = item.gameName + " - " + item.dataName;
        const bool isSelected = (m_currentlyEditingFile == item.fullPath);

        if (ImGui::Selectable(displayName.c_str(), isSelected)) {
            m_currentlyEditingFile = item.fullPath; // Use the full path
            // clear all buffers
            selResourceKey.clear();
            selMachineKey.clear();
            selRecipeKey.clear();
            resourceDirty = false;
            machineDirty = false;
            recipeDirty = false;

            if (!gameDataManager.loadFromFile(m_currentlyEditingFile.string(), m_fileLoadError)) {
                // Loading failed, error message is in m_fileLoadError
            } else {
                m_fileLoadError.clear();
            }
        }
        if (isSelected) {
            ImGui::SetItemDefaultFocus();
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

    if (ImGui::BeginPopupModal("New File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char fileNameBuffer[128] = "";
        static std::string errorMessage;

        // Autofocus the input field when the popup opens
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
            // Clear state from previous openings
            fileNameBuffer[0] = '\0';
            errorMessage.clear();
        }

        ImGui::Text("Enter new file name:");
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
            std::string filenameStr(fileNameBuffer);
            if (filenameStr.empty()) {
                errorMessage = "File name cannot be empty.";
            } else {
                std::filesystem::path newFilePath =
                        SettingsManager::instance().getSettings().gameDataPath / filenameStr;
                newFilePath.replace_extension(".json");

                std::string saveError;
                gameDataManager.createNew("New Game", "seconds");
                if (gameDataManager.saveToFile(newFilePath, saveError)) {
                    m_currentlyEditingFile = newFilePath;
                    gameDataManager.loadFromFile(m_currentlyEditingFile, m_fileLoadError); // Load the new file
                    m_fileLoadError.clear(); // Clear any old loading errors
                    RefreshPackageList();
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
    ImGui::BeginChild("RightPanel", ImVec2(0, 0), false);
    if (m_currentlyEditingFile.empty()) {
        ImGui::TextDisabled("Select a game data file from the left panel to begin editing.");
        ImGui::EndChild();
        return;
    }
    // header: Game name + time unit + save + rename + delete
    GameData &gd = gameDataManager.current();

    // --- Header row ---
    ImGui::PushID("Header");
    ImGui::TextUnformatted("Project:");
    ImGui::SameLine();
    if (gd.gameName.empty()) {
        strncpy(gameNameBuf, "Unnamed Game", sizeof(gameNameBuf));
    } else {
        strncpy(gameNameBuf, gd.gameName.c_str(), sizeof(gameNameBuf));
    }
    if (ImGui::InputText("##GameName", gameNameBuf, sizeof(gameNameBuf))) {
        gd.gameName = std::string(gameNameBuf);
    }
    ImGui::SameLine();
    const char *units[] = {"seconds", "minutes", "hours"};
    int curUnit = 0;
    for (int i = 0; i < 3; ++i) if (gd.time_unit == units[i]) curUnit = i;
    if (ImGui::Combo("Time Unit", &curUnit, units, IM_ARRAYSIZE(units))) {
        gd.time_unit = units[curUnit];
    }

    // File operation buttons
    if (ImGui::Button("Save")) {
        std::string err;
        if (!gameDataManager.saveToFile(m_currentlyEditingFile, err)) {
            ImGui::OpenPopup("SaveError");
        } else {
            // optional success toast/notification
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Rename")) {
        ImGui::OpenPopup("Rename File");
    }

    ImGui::SameLine();
    if (ImGui::Button("Delete File")) {
        ImGui::OpenPopup("Delete File");
    }

    // Save error popup
    if (ImGui::BeginPopupModal("SaveError", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Failed to save:\n%s", "Unable to write file");
        if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Draw the rename and delete dialogs
    DrawRenameFileDialog();
    DrawDeleteFileDialog();

    ImGui::PopID();

    ImGui::Separator();
    ImGui::Spacing();

    // show validation warnings (non-blocking)
    auto messages = gameDataManager.validate(gd);
    if (!messages.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 180, 50, 255));
        ImGui::TextWrapped("Validation warnings:");
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 1),
                                            ImVec2(FLT_MAX, ImGui::GetTextLineHeightWithSpacing() * 10));
        if (ImGui::BeginChild("ConstrainedChild", ImVec2(-FLT_MIN, 0.0f),
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
            for (const auto &m: messages) {
                ImGui::BulletText("%s", m.c_str());
            }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    // main tab bar
    if (ImGui::BeginTabBar("DataSections")) {
        if (ImGui::BeginTabItem("Resources")) {
            DrawResourcesTab(gd);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Machines")) {
            DrawMachinesTab(gd);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Recipes")) {
            DrawRecipesTab(gd);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // Delete confirmation modal:
    if (showDeleteConfirm) {
        ImGui::OpenPopup("ConfirmDelete");
        if (ImGui::BeginPopupModal("ConfirmDelete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Are you sure you want to delete this item? This cannot be undone.");
            if (ImGui::Button("Yes")) {
                std::string err;
                if (deleteTargetType == 1) gameDataManager.deleteResource(deleteTargetKey, err);
                else if (deleteTargetType == 2) gameDataManager.deleteMachine(deleteTargetKey, err);
                else if (deleteTargetType == 3) gameDataManager.deleteRecipe(deleteTargetKey, err);
                showDeleteConfirm = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("No")) {
                showDeleteConfirm = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::EndChild();
}

void GameDataEditor::DrawRenameFileDialog() {
    // Always center the popup when it appears
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Rename File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char renameBuffer[128] = "";
        static std::string renameErrorMessage;

        // Auto-focus the input field when the popup opens and initialize with current filename
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
            // Initialize with current filename (without extension)
            std::string currentName = m_currentlyEditingFile.stem().string();
            strncpy(renameBuffer, currentName.c_str(), sizeof(renameBuffer));
            renameBuffer[sizeof(renameBuffer) - 1] = '\0';
            renameErrorMessage.clear();
        }

        ImGui::Text("Enter new file name:");
        ImGui::PushItemWidth(-1);

        // Request rename on pressing Enter or clicking the "Rename" button
        bool renameRequested = ImGui::InputText("##renamefield", renameBuffer, sizeof(renameBuffer),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();

        ImGui::Spacing();

        if (ImGui::Button("Rename", ImVec2(120, 0))) {
            renameRequested = true;
        }

        if (renameRequested) {
            std::string newFilenameStr(renameBuffer);
            if (newFilenameStr.empty()) {
                renameErrorMessage = "File name cannot be empty.";
            } else {
                const auto &gameDataPath = SettingsManager::instance().getSettings().gameDataPath;
                std::filesystem::path newFilePath = gameDataPath / newFilenameStr;
                newFilePath.replace_extension(".json");

                // Check if target file already exists
                if (std::filesystem::exists(newFilePath) && newFilePath != m_currentlyEditingFile) {
                    renameErrorMessage = "A file with this name already exists.";
                } else {
                    try {
                        // Rename the file
                        std::filesystem::rename(m_currentlyEditingFile, newFilePath);
                        m_currentlyEditingFile = newFilePath;
                        RefreshPackageList();
                        ImGui::CloseCurrentPopup();
                    } catch (const std::filesystem::filesystem_error &e) {
                        renameErrorMessage = "Failed to rename file: " + std::string(e.what());
                    }
                }
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        // Display error message if something went wrong during rename
        if (!renameErrorMessage.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", renameErrorMessage.c_str());
        }

        ImGui::EndPopup();
    }
}

void GameDataEditor::DrawDeleteFileDialog() {
    // Always center the popup when it appears
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Delete File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Are you sure you want to delete this file?");
        ImGui::Text("File: %s", m_currentlyEditingFile.filename().string().c_str());
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "This action cannot be undone!");

        ImGui::Spacing();

        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            try {
                // Delete the file
                std::filesystem::remove(m_currentlyEditingFile);
                // Clear current file and reset state
                m_currentlyEditingFile.clear();
                gameDataManager.clear();
                m_fileLoadError.clear();
                RefreshPackageList();
                ImGui::CloseCurrentPopup();
            } catch (const std::filesystem::filesystem_error &e) {
                LOG(ERROR) << "Failed to delete file: " << e.what();
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void GameDataEditor::DrawResourcesTab(const GameData &gd) {
    ImGui::Columns(2, "rescols", true);

    // Left: list + toolbar
    ImGui::BeginChild("ResList", ImVec2(0, 0), false);
    // toolbar
    if (ImGui::Button("Add##res")) {
        Resource r;
        r.name = "New Resource";
        bool success = gameDataManager.addResource(r);
        selResourceKey = slugify(r.name);
        // load into buffer
        strncpy(resNameBuf, "New Resource", sizeof(resNameBuf));
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete##res") && !selResourceKey.empty()) {
        deleteTargetType = 1;
        deleteTargetKey = selResourceKey;
        showDeleteConfirm = true;
    }
    ImGui::PushItemWidth(-1);
    ImGui::InputTextWithHint("##resfilter", "Filter...", resourceFilter, sizeof(resourceFilter));
    ImGui::PopItemWidth();
    ImGui::Separator();

    // list resources
    for (const auto &r: gd.resources) {
        if (r.first == "nothing") continue; // skip reserved nothing
        // filter by name/key
        std::string combined = r.second.name + " " + r.first;
        if (resourceFilter[0] != '\0' && combined.find(resourceFilter) == std::string::npos) continue;
        char buf[128];
        snprintf(buf, sizeof(buf), "%s", r.second.name.c_str());
        ImGui::PushID(r.first.c_str());
        if (ImGui::Selectable(buf, selResourceKey == r.first)) {
            selResourceKey = r.first;
            // load into buffers
            strncpy(resNameBuf, r.second.name.c_str(), sizeof(resNameBuf));
            resourceDirty = false;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    // Right: details
    ImGui::NextColumn();
    ImGui::BeginChild("ResDetail", ImVec2(0, 0), false);
    if (selResourceKey.empty()) {
        ImGui::TextDisabled("Select resource to edit or press Add.");
    } else {
        ImGui::InputText("Name", resNameBuf, sizeof(resNameBuf));
        ImGui::Spacing();
        if (ImGui::Button("Save")) {
            Resource r;
            r.name = std::string(resNameBuf);
            std::string err;
            if (!gameDataManager.editResource(selResourceKey, r, err)) {
                LOG(WARNING) << "Error saving resource: " << err;
            }
            selResourceKey = slugify(r.name); // update selected key in case name changed
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            // reload from manager
            auto it = gd.resources.find(selResourceKey);
            if (it != gd.resources.end()) {
                strncpy(resNameBuf, it->second.name.c_str(), sizeof(resNameBuf));
            }
        }
    }
    ImGui::EndChild();
    ImGui::Columns(1);
}

void GameDataEditor::DrawMachinesTab(const GameData &gd) {
    ImGui::Columns(2, "machcols", true);
    // Left: machine list + toolbar
    ImGui::BeginChild("MachList", ImVec2(0, 0), false);
    if (ImGui::Button("Add##mach")) {
        Machine m;
        m.name = "New Machine";
        m.base_crafting_speed = 1.0;
        bool success = gameDataManager.addMachine(m);
        selMachineKey = slugify(m.name);
        strncpy(machNameBuf, "New Machine", sizeof(machNameBuf));
        machBaseSpeed = 1.0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete##mach") && !selMachineKey.empty()) {
        deleteTargetType = 2;
        deleteTargetKey = selMachineKey;
        showDeleteConfirm = true;
    }
    ImGui::InputTextWithHint("##machfilter", "Filter...", machineFilter, sizeof(machineFilter));
    ImGui::Separator();
    for (const auto &m: gd.machines) {
        std::string combined = m.second.name + " " + m.first;
        if (machineFilter[0] != '\0' && combined.find(machineFilter) == std::string::npos) continue;
        char buf[256];
        snprintf(buf, sizeof(buf), "%s", m.second.name.c_str());
        ImGui::PushID(m.first.c_str());
        if (ImGui::Selectable(buf, selMachineKey == m.first)) {
            selMachineKey = m.first;
            strncpy(machNameBuf, m.second.name.c_str(), sizeof(machNameBuf));
            machBaseSpeed = m.second.base_crafting_speed;
            machineDirty = false;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    // Right: machine detail
    ImGui::NextColumn();
    ImGui::BeginChild("MachDetail", ImVec2(0, 0), false);
    if (selMachineKey.empty()) {
        ImGui::TextDisabled("Select machine or press Add.");
    } else {
        ImGui::InputText("Name", machNameBuf, sizeof(machNameBuf));
        ImGui::InputDouble("Base speed", &machBaseSpeed, 0.0, 0.0, "%.4g");
        ImGui::Spacing();
        if (ImGui::Button("Save")) {
            Machine m;
            m.name = machNameBuf;
            m.base_crafting_speed = machBaseSpeed;
            std::string err;
            if (!gameDataManager.editMachine(selMachineKey, m, err)) {
                LOG(WARNING) << "Error saving machine: " << err;
            }
            selMachineKey = slugify(m.name); // update selected key in case name changed
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            auto it = gd.machines.find(selMachineKey);
            if (it != gd.machines.end()) {
                strncpy(machNameBuf, it->second.name.c_str(), sizeof(machNameBuf));
                machBaseSpeed = it->second.base_crafting_speed;
            }
        }
    }
    ImGui::EndChild();
    ImGui::Columns(1);
}

void GameDataEditor::DrawRecipesTab(const GameData &gd) {
    ImGui::Columns(2, "reccols", true);

    ImGui::BeginChild("RecList", ImVec2(0, 0), false);

    if (ImGui::Button("Add##rec")) {
        Recipe r;
        r.name = "New Recipe";
        r.time_seconds = 1.0;
        bool success = gameDataManager.addRecipe(r);
        selRecipeKey = slugify(r.name);
        strncpy(recNameBuf, "New Recipe", sizeof(recNameBuf));
        recTimeSeconds = r.time_seconds;
        recInputs.clear();
        recOutputs.clear();
        recMachines.clear();
    }

    ImGui::SameLine();
    if (ImGui::Button("Delete##rec") && !selRecipeKey.empty()) {
        deleteTargetType = 3;
        deleteTargetKey = selRecipeKey;
        showDeleteConfirm = true;
    }

    ImGui::InputTextWithHint("##recfilter", "Filter...", recipeFilter, sizeof(recipeFilter));
    ImGui::Separator();

    for (const auto &r: gd.recipes) {
        std::string combined = r.second.name + " " + r.first;
        if (recipeFilter[0] != '\0' && combined.find(recipeFilter) == std::string::npos) continue;
        char buf[256];
        snprintf(buf, sizeof(buf), "%s", r.second.name.c_str());
        ImGui::PushID(r.first.c_str());
        if (ImGui::Selectable(buf, selRecipeKey == r.first)) {
            selRecipeKey = r.first;
            // load recipe into buffers
            strncpy(recNameBuf, r.second.name.c_str(), sizeof(recNameBuf));
            recTimeSeconds = r.second.time_seconds;
            // copy ports
            recInputs.clear();
            recOutputs.clear();
            recMachines.clear();
            for (auto &p: r.second.input_ports) {
                recInputs.emplace_back(p.resource_key, p.amount);
                strcpy(recInputNameBufs.emplace_back().data(), p.resource_key.c_str());
                recInputFilterBufs.emplace_back()[0] = '\0';
            }
            for (auto &p: r.second.output_ports) {
                recOutputs.emplace_back(p.resource_key, p.amount);
                strcpy(recOutputNameBufs.emplace_back().data(), p.resource_key.c_str());
                recOutputFilterBufs.emplace_back()[0] = '\0';
            }
            for (const auto& m: r.second.produced_in_machines_keys) recMachines.insert(m);

            recipeDirty = false;
        }
        ImGui::PopID();
    }

    ImGui::EndChild();

    // Right: recipe detail editor
    ImGui::NextColumn();
    ImGui::BeginChild("RecDetail", ImVec2(0, 0), false);

    if (selRecipeKey.empty()) {
        ImGui::TextDisabled("Select or create a recipe.");
    } else {
        ImGui::InputText("Name", recNameBuf, sizeof(recNameBuf));
        ImGui::InputDouble("Time (seconds)", &recTimeSeconds, 0.0, 0.0, "%.2g");

        ImGui::Separator();
        ImGui::Text("Inputs");

        // --- Prepare per-row name/filter buffers and keep them in sync with recInputs ---
        if ((int) recInputNameBufs.size() != (int) recInputs.size()) {
            // resize and initialize from current recInputs
            recInputNameBufs.resize(recInputs.size());
            recInputFilterBufs.resize(recInputs.size());
            for (size_t i = 0; i < recInputs.size(); ++i) {
                const Resource *rres = gd.resources.find(recInputs[i].first) != gd.resources.end()
                                         ? &gd.resources.at(recInputs[i].first)
                                         : nullptr;
                if (rres) {
                    strncpy(recInputNameBufs[i].data(), rres->name.c_str(), recInputNameBufs[i].size());
                } else {
                    recInputNameBufs[i][0] = '\0';
                }
                recInputFilterBufs[i][0] = '\0';
            }
        }

        // inputs table
        if (ImGui::BeginTable("inputs", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Resource");
            ImGui::TableSetupColumn("Amount");
            ImGui::TableSetupColumn("Remove");
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(recInputs.size());) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                // resource combo-with-filter
                ImGui::PushItemWidth(-1);
                std::string comboLabel = std::string("##inres") + std::to_string(i);
                const char *preview = recInputNameBufs[i].data();
                if (preview[0] == '\0') preview = "<none>";
                if (ImGui::BeginCombo(comboLabel.c_str(), preview, ImGuiComboFlags_HeightLarge)) {
                    // filter text input inside combo
                    ImGui::PushID(("infilter" + std::to_string(i)).c_str());
                    ImGui::PushItemWidth(-1);
                    ImGui::InputTextWithHint("##filter", "Type to filter or enter new name",
                                             recInputFilterBufs[i].data(), recInputFilterBufs[i].size());
                    ImGui::PopItemWidth();
                    ImGui::PopID();
                    ImGui::Separator();

                    std::string filter(recInputFilterBufs[i].data());
                    // list matching resources
                    bool anyShown = false;
                    for (const auto &res: gd.resources) {
                        if (res.first == "nothing") continue;
                        if (!filter.empty()) {
                            if (res.second.name.find(filter) == std::string::npos) continue;
                        }
                        anyShown = true;
                        bool selected = (res.first == recInputs[i].first);
                        if (ImGui::Selectable(res.second.name.c_str(), selected)) {
                            recInputs[i].first = res.first;
                            strncpy(recInputNameBufs[i].data(), res.second.name.c_str(), recInputNameBufs[i].size());
                            // clear filter so next open shows all
                            recInputFilterBufs[i][0] = '\0';
                        }
                    }

                    if (!anyShown) {
                        ImGui::TextDisabled("No resources match.");
                    }
                    ImGui::Separator();

                    // allow creating new resource from typed filter or typed name
                    std::string typed = recInputFilterBufs[i][0]
                                            ? std::string(recInputFilterBufs[i].data())
                                            : std::string(recInputNameBufs[i].data());
                    if (!typed.empty()) {
                        std::string createLabel = "Create new resource: '" + typed + "'";
                        if (ImGui::Selectable(createLabel.c_str(), false)) {
                            // create resource via manager
                            Resource newRes;
                            newRes.name = typed;
                            bool success = gameDataManager.addResource(newRes);
                            if (success) {
                                recInputs[i].first = slugify(typed);
                                // update name buffer
                                strncpy(recInputNameBufs[i].data(), typed.c_str(), recInputNameBufs[i].size());
                                recInputFilterBufs[i][0] = '\0';
                            } else {
                                // optionally store/trigger an error; here we just leave it
                            }
                        }
                    }

                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                ImGui::TableNextColumn();
                ImGui::InputDouble(("##inamt" + std::to_string(i)).c_str(), &recInputs[i].second, 0.0, 0.0, "%.4g");

                ImGui::TableNextColumn();
                if (ImGui::Button(("Remove##in" + std::to_string(i)).c_str())) {
                    recInputs.erase(recInputs.begin() + i);
                    recInputNameBufs.erase(recInputNameBufs.begin() + i);
                    recInputFilterBufs.erase(recInputFilterBufs.begin() + i);
                    // do NOT increment i; continue with new element at this index
                    continue;
                }

                ++i;
            }

            ImGui::EndTable();
        }

        if (ImGui::Button("Add Input")) {
            recInputs.emplace_back("", 1.0);
        }

        ImGui::Separator();
        ImGui::Text("Outputs");

        // --- outputs buffers ---
        if ((int) recOutputNameBufs.size() != (int) recOutputs.size()) {
            recOutputNameBufs.resize(recOutputs.size());
            recOutputFilterBufs.resize(recOutputs.size());
            for (size_t i = 0; i < recOutputs.size(); ++i) {
                const Resource *rres = gd.resources.find(recOutputs[i].first) != gd.resources.end()
                                         ? &gd.resources.at(recOutputs[i].first)
                                         : nullptr;
                if (rres) {
                    strncpy(recOutputNameBufs[i].data(), rres->name.c_str(), recOutputNameBufs[i].size());
                } else {
                    recOutputNameBufs[i][0] = '\0';
                }
                recOutputFilterBufs[i][0] = '\0';
            }
        }

        if (ImGui::BeginTable("outputs", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Resource");
            ImGui::TableSetupColumn("Amount");
            ImGui::TableSetupColumn("Remove");
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int) recOutputs.size();) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                std::string comboLabel = std::string("##outres") + std::to_string(i);
                const char *preview = recOutputNameBufs[i].data();
                if (preview[0] == '\0') preview = "<none>";
                ImGui::PushItemWidth(-1);
                if (ImGui::BeginCombo(comboLabel.c_str(), preview, ImGuiComboFlags_HeightLarge)) {
                    ImGui::PushID(("outfilter" + std::to_string(i)).c_str());
                    ImGui::PushItemWidth(-1);
                    ImGui::InputTextWithHint("##filter", "Type to filter or enter new name",
                                             recOutputFilterBufs[i].data(), recOutputFilterBufs[i].size());
                    ImGui::PopItemWidth();
                    ImGui::PopID();
                    ImGui::Separator();

                    std::string filter(recOutputFilterBufs[i].data());
                    bool anyShown = false;
                    for (const auto &res: gd.resources) {
                        if (res.first == "nothing") continue;
                        if (!filter.empty()) {
                            if (res.second.name.find(filter) == std::string::npos) continue;
                        }
                        anyShown = true;
                        bool selected = (res.first == recOutputs[i].first);
                        if (ImGui::Selectable(res.second.name.c_str(), selected)) {
                            recOutputs[i].first = res.first;
                            strncpy(recOutputNameBufs[i].data(), res.second.name.c_str(), recOutputNameBufs[i].size());
                            recOutputFilterBufs[i][0] = '\0';
                        }
                    }

                    if (!anyShown) {
                        ImGui::TextDisabled("No resources match.");
                    }
                    ImGui::Separator();

                    std::string typed = recOutputFilterBufs[i][0]
                                            ? std::string(recOutputFilterBufs[i].data())
                                            : std::string(recOutputNameBufs[i].data());
                    if (!typed.empty()) {
                        std::string createLabel = "Create new resource: '" + typed + "'";
                        if (ImGui::Selectable(createLabel.c_str(), false)) {
                            Resource newRes;
                            newRes.name = typed;
                            bool success = gameDataManager.addResource(newRes);
                            if (success) {
                                recOutputs[i].first = slugify(typed);
                                strncpy(recOutputNameBufs[i].data(), typed.c_str(), recOutputNameBufs[i].size());
                                recOutputFilterBufs[i][0] = '\0';
                            }
                        }
                    }

                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                ImGui::TableNextColumn();
                ImGui::InputDouble(("##outamt" + std::to_string(i)).c_str(), &recOutputs[i].second, 0.0, 0.0, "%.4g");

                ImGui::TableNextColumn();
                if (ImGui::Button(("Remove##out" + std::to_string(i)).c_str())) {
                    recOutputs.erase(recOutputs.begin() + i);
                    recOutputNameBufs.erase(recOutputNameBufs.begin() + i);
                    recOutputFilterBufs.erase(recOutputFilterBufs.begin() + i);
                    continue;
                }

                ++i;
            }

            ImGui::EndTable();
        }

        if (ImGui::Button("Add Output")) {
            recOutputs.emplace_back("", 1.0);
        }

        ImGui::Separator();
        ImGui::Text("Compatible Machines");

        // show checkboxes for machines (compress if many)
        ImGui::BeginChild("machCheckboxes", ImVec2(0, 240), true);
        for (const auto &m: gd.machines) {
            bool checked = recMachines.count(m.first);
            std::string cbLabel = m.second.name;
            if (ImGui::Checkbox(cbLabel.c_str(), &checked)) {
                if (checked) recMachines.insert(m.first);
                else recMachines.erase(m.first);
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();

        // Save / Cancel buttons
        if (ImGui::Button("Save Recipe")) {
            // Build recipe object
            Recipe r;
            r.name = recNameBuf;
            r.time_seconds = recTimeSeconds;

            // Before copying ports, ensure any typed-but-uncreated resources are created.
            for (auto &p: recInputs) {
                if (p.first.empty()) continue;
                if (gd.resources.find(p.first) == gd.resources.end()) {
                    Resource newRes;
                    newRes.name = p.first; // use key as name if not found
                    // search in name buffers for a better name
                    for (const auto &nbuf: recInputNameBufs) {
                        if (slugify(nbuf.data()) == p.first) {
                            newRes.name = nbuf.data();
                            break;
                        }
                    }
                    gameDataManager.addResource(newRes);
                }
            }
            for (auto &p: recOutputs) {
                if (p.first.empty()) continue;
                if (gd.resources.find(p.first) == gd.resources.end()) {
                    Resource newRes;
                    newRes.name = p.first;
                    for (const auto &nbuf: recOutputNameBufs) {
                        if (slugify(nbuf.data()) == p.first) {
                            newRes.name = nbuf.data();
                            break;
                        }
                    }
                    gameDataManager.addResource(newRes);
                }
            }

            // copy ports into r
            r.input_ports.clear();
            for (auto &p: recInputs) r.input_ports.push_back(RecipePort{p.second, p.first});
            r.output_ports.clear();
            for (auto &p: recOutputs) r.output_ports.push_back(RecipePort{p.second, p.first});

            r.produced_in_machines_keys.clear();
            for (const auto& key: recMachines) r.produced_in_machines_keys.push_back(key);

            std::string err;
            if (!gameDataManager.editRecipe(selRecipeKey, r, err)) {
                // show error
            } else {
                recipeDirty = false;
            }
            selRecipeKey = slugify(r.name); // update selected key in case name changed
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            // reload from gd
            auto r = gd.recipes.at(selRecipeKey);
            // load recipe into buffers
            strncpy(recNameBuf, r.name.c_str(), sizeof(recNameBuf));
            recTimeSeconds = r.time_seconds;
            // copy ports
            recInputs.clear();
            recOutputs.clear();
            recMachines.clear();
            for (auto &p: r.input_ports) {
                recInputs.emplace_back(p.resource_key, p.amount);
                strcpy(recInputNameBufs.emplace_back().data(), p.resource_key.c_str());
                recInputFilterBufs.emplace_back()[0] = '\0';
            }
            for (auto &p: r.output_ports) {
                recOutputs.emplace_back(p.resource_key, p.amount);
                strcpy(recOutputNameBufs.emplace_back().data(), p.resource_key.c_str());
                recOutputFilterBufs.emplace_back()[0] = '\0';
            }
            for (const auto& m: r.produced_in_machines_keys) recMachines.insert(m);
            recipeDirty = false;
        }
    }

    ImGui::EndChild();
    ImGui::Columns(1);
}
