#include "GameDataEditor.h"
#include <imgui.h>

#include "FilesystemUtils.h"
#include "SettingsManager.h"
#include "StringUtils.h"

GameDataEditor::GameDataEditor() {
    gameDataManager.clear();
};

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

    // Button to open the "New File" popup
    if (ImGui::Button("+ New Game Data File", ImVec2(-1, 0))) {
        ImGui::OpenPopup("New File");
    }

    ImGui::Separator();

    // List all game data files
    const auto &gameDataPath = SettingsManager::instance().getSettings().gameDataPath;
    std::vector<std::filesystem::path> files = GetGameDataFiles(gameDataPath);

    for (const auto &file: files) {
        const bool isSelected = (m_currentlyEditingFile.filename() == file.filename());
        if (ImGui::Selectable(file.filename().string().c_str(), isSelected)) {
            m_currentlyEditingFile = gameDataPath / file.filename();
            if (!gameDataManager.loadFromFile(m_currentlyEditingFile, m_fileLoadError)) {
                // Loading failed, error message is in m_fileLoadError
            } else {
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
    if (ImGui::BeginPopupModal("SaveError", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
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
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 1), ImVec2(FLT_MAX, ImGui::GetTextLineHeightWithSpacing() * 10));
        if (ImGui::BeginChild("ConstrainedChild", ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
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
        if (ImGui::BeginPopupModal("ConfirmDelete", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Are you sure you want to delete this item? This cannot be undone.");
            if (ImGui::Button("Yes")) {
                std::string err;
                if (deleteTargetType == 1) gameDataManager.deleteResource(deleteTargetId, err);
                else if (deleteTargetType == 2) gameDataManager.deleteMachine(deleteTargetId, err);
                else if (deleteTargetType == 3) gameDataManager.deleteRecipe(deleteTargetId, err);
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

    if (ImGui::BeginPopupModal("Rename File", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char renameBuffer[128] = "";
        static std::string renameErrorMessage = "";

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
                        ImGui::CloseCurrentPopup();
                    } catch (const std::filesystem::filesystem_error& e) {
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

    if (ImGui::BeginPopupModal("Delete File", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
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
                ImGui::CloseCurrentPopup();
            } catch (const std::filesystem::filesystem_error& e) {
                // Could store error message to show in a separate error popup
                // For now, we'll just close the popup and the file will remain
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
        r.key_name = "";
        r.name = "New Resource";
        int newId = gameDataManager.addResource(r);
        selResourceId = newId;
        // load into buffer
        strncpy(resNameBuf, "New Resource", sizeof(resNameBuf));
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete##res") && selResourceId > 0) {
        deleteTargetType = 1;
        deleteTargetId = selResourceId;
        showDeleteConfirm = true;
    }
    ImGui::PushItemWidth(-1);
    ImGui::InputTextWithHint("##resfilter", "Filter...", resourceFilter, sizeof(resourceFilter));
    ImGui::PopItemWidth();
    ImGui::Separator();

    // list resources
    for (const auto &r: gd.resources) {
        if (r.id == 0) continue; // skip reserved nothing
        // filter by name/key
        std::string combined = r.name + " " + r.key_name;
        if (resourceFilter[0] != '\0' && combined.find(resourceFilter) == std::string::npos) continue;
        char buf[128];
        snprintf(buf, sizeof(buf), "%s", r.name.c_str());
        ImGui::PushID(r.id);
        if (ImGui::Selectable(buf, selResourceId == r.id)) {
            selResourceId = r.id;
            // load into buffers
            strncpy(resNameBuf, r.name.c_str(), sizeof(resNameBuf));
            resourceDirty = false;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    // Right: details
    ImGui::NextColumn();
    ImGui::BeginChild("ResDetail", ImVec2(0, 0), false);
    if (selResourceId <= 0) {
        ImGui::TextDisabled("Select resource to edit or press Add.");
    } else {
        ImGui::InputText("Name", resNameBuf, sizeof(resNameBuf));
        ImGui::Spacing();
        if (ImGui::Button("Save")) {
            Resource r;
            r.id = selResourceId;
            r.name = std::string(resNameBuf);
            r.key_name = slugify(resNameBuf);
            std::string err;
            if (!gameDataManager.editResource(selResourceId, r, err)) {
                std::cerr << "Error saving resource: " << err << std::endl;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            // reload from manager
            auto it = std::find_if(gd.resources.begin(), gd.resources.end(), [&](const Resource &res) {
                return res.id == selResourceId;
            });
            if (it != gd.resources.end()) {
                strncpy(resNameBuf, it->name.c_str(), sizeof(resNameBuf));
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
        m.key_name = "";
        m.name = "New Machine";
        m.base_crafting_speed = 1.0;
        int id = gameDataManager.addMachine(m);
        selMachineId = id;
        strncpy(machNameBuf, "New Machine", sizeof(machNameBuf));
        machBaseSpeed = 1.0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete##mach") && selMachineId > 0) {
        deleteTargetType = 2;
        deleteTargetId = selMachineId;
        showDeleteConfirm = true;
    }
    ImGui::InputTextWithHint("##machfilter", "Filter...", machineFilter, sizeof(machineFilter));
    ImGui::Separator();
    for (const auto &m: gd.machines) {
        std::string combined = m.name + " " + m.key_name;
        if (machineFilter[0] != '\0' && combined.find(machineFilter) == std::string::npos) continue;
        char buf[256];
        snprintf(buf, sizeof(buf), "%s", m.name.c_str());
        ImGui::PushID(m.id);
        if (ImGui::Selectable(buf, selMachineId == m.id)) {
            selMachineId = m.id;
            strncpy(machNameBuf, m.name.c_str(), sizeof(machNameBuf));
            machBaseSpeed = m.base_crafting_speed;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    // Right: machine detail
    ImGui::NextColumn();
    ImGui::BeginChild("MachDetail", ImVec2(0, 0), false);
    if (selMachineId < 0) {
        ImGui::TextDisabled("Select machine or press Add.");
    } else {
        ImGui::InputText("Name", machNameBuf, sizeof(machNameBuf));
        ImGui::InputDouble("Base speed", &machBaseSpeed, 0.0, 0.0, "%.4g");
        ImGui::Spacing();
        if (ImGui::Button("Save")) {
            Machine m;
            m.id = selMachineId;
            m.name = machNameBuf;
            m.key_name = slugify(machNameBuf);
            m.base_crafting_speed = machBaseSpeed;
            std::string err;
            if (!gameDataManager.editMachine(selMachineId, m, err)) {
                std::cerr << "Error saving machine: " << err << std::endl;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            // reload
            auto it = std::find_if(gd.machines.begin(), gd.machines.end(),
                                   [&](const Machine &mm) { return mm.id == selMachineId; });
            if (it != gd.machines.end()) {
                strncpy(machNameBuf, it->name.c_str(), sizeof(machNameBuf));
                machBaseSpeed = it->base_crafting_speed;
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
        r.key_name = "";
        r.name = "New Recipe";
        r.time_seconds = 1.0;
        int id = gameDataManager.addRecipe(r);
        selRecipeId = id;
        strncpy(recNameBuf, "New Recipe", sizeof(recNameBuf));
        recTimeSeconds = r.time_seconds;
        recInputs.clear();
        recOutputs.clear();
        recMachines.clear();
    }

    ImGui::SameLine();
    if (ImGui::Button("Delete##rec") && selRecipeId >= 0) {
        deleteTargetType = 3;
        deleteTargetId = selRecipeId;
        showDeleteConfirm = true;
    }

    ImGui::InputTextWithHint("##recfilter", "Filter...", recipeFilter, sizeof(recipeFilter));
    ImGui::Separator();

    for (const auto &r: gd.recipes) {
        std::string combined = r.name + " " + r.key_name;
        if (recipeFilter[0] != '\0' && combined.find(recipeFilter) == std::string::npos) continue;
        char buf[256];
        snprintf(buf, sizeof(buf), "%s", r.name.c_str());
        ImGui::PushID(r.id);
        if (ImGui::Selectable(buf, selRecipeId == r.id)) {
            selRecipeId = r.id;
            // load recipe into buffers
            strncpy(recNameBuf, r.name.c_str(), sizeof(recNameBuf));
            recTimeSeconds = r.time_seconds;
            // copy ports
            recInputs.clear();
            recOutputs.clear();
            recMachines.clear();
            for (auto &p: r.input_ports) recInputs.emplace_back(p.resource_id, p.amount);
            for (auto &p: r.output_ports) recOutputs.emplace_back(p.resource_id, p.amount);
            for (int mid: r.produced_in_machines_ids) recMachines.insert(mid);
            recipeDirty = false;
        }
        ImGui::PopID();
    }

    ImGui::EndChild();

    // Right: recipe detail editor
    ImGui::NextColumn();
    ImGui::BeginChild("RecDetail", ImVec2(0, 0), false);

    if (selRecipeId < 0) {
        ImGui::TextDisabled("Select or create a recipe.");
    } else {
        ImGui::InputText("Name", recNameBuf, sizeof(recNameBuf));
        ImGui::InputDouble("Time (seconds)", &recTimeSeconds, 0.0, 0.0, "%.2f");

        ImGui::Separator();
        ImGui::Text("Inputs");

        // --- Prepare per-row name/filter buffers and keep them in sync with recInputs ---
        if ((int)recInputNameBufs.size() != (int)recInputs.size()) {
            // resize and initialize from current recInputs
            recInputNameBufs.resize(recInputs.size());
            recInputFilterBufs.resize(recInputs.size());
            for (size_t i = 0; i < recInputs.size(); ++i) {
                const Resource *rres = findResourceById(gd, recInputs[i].first);
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

            for (int i = 0; i < (int)recInputs.size(); ) {
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
                    ImGui::InputTextWithHint("##filter", "Type to filter or enter new name", recInputFilterBufs[i].data(), recInputFilterBufs[i].size());
                    ImGui::PopItemWidth();
                    ImGui::PopID();
                    ImGui::Separator();

                    std::string filter(recInputFilterBufs[i].data());
                    // list matching resources
                    bool anyShown = false;
                    for (const auto &res: gd.resources) {
                        if (res.id == 0) continue;
                        if (!filter.empty()) {
                            if (res.name.find(filter) == std::string::npos) continue;
                        }
                        anyShown = true;
                        bool selected = (res.id == recInputs[i].first);
                        if (ImGui::Selectable(res.name.c_str(), selected)) {
                            recInputs[i].first = res.id;
                            strncpy(recInputNameBufs[i].data(), res.name.c_str(), recInputNameBufs[i].size());
                            // clear filter so next open shows all
                            recInputFilterBufs[i][0] = '\0';
                        }
                    }

                    if (!anyShown) {
                        ImGui::TextDisabled("No resources match.");
                    }
                    ImGui::Separator();

                    // allow creating new resource from typed filter or typed name
                    std::string typed = recInputFilterBufs[i][0] ? std::string(recInputFilterBufs[i].data()) : std::string(recInputNameBufs[i].data());
                    if (!typed.empty()) {
                        std::string createLabel = "Create new resource: '" + typed + "'";
                        if (ImGui::Selectable(createLabel.c_str(), false)) {
                            // create resource via manager
                            Resource newRes;
                            newRes.name = typed;
                            newRes.key_name = slugify(typed);
                            int newId = gameDataManager.addResource(newRes);
                            if (newId > 0) {
                                recInputs[i].first = newId;
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
            recInputs.emplace_back(0, 1.0);
            // ensure buffers in next frame will be resized
        }

        ImGui::Separator();
        ImGui::Text("Outputs");

        // --- outputs buffers ---
        if ((int)recOutputNameBufs.size() != (int)recOutputs.size()) {
            recOutputNameBufs.resize(recOutputs.size());
            recOutputFilterBufs.resize(recOutputs.size());
            for (size_t i = 0; i < recOutputs.size(); ++i) {
                const Resource *rres = findResourceById(gd, recOutputs[i].first);
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

            for (int i = 0; i < (int)recOutputs.size(); ) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                std::string comboLabel = std::string("##outres") + std::to_string(i);
                const char *preview = recOutputNameBufs[i].data();
                if (preview[0] == '\0') preview = "<none>";
                ImGui::PushItemWidth(-1);
                if (ImGui::BeginCombo(comboLabel.c_str(), preview, ImGuiComboFlags_HeightLarge)) {
                    ImGui::PushID(("outfilter" + std::to_string(i)).c_str());
                    ImGui::PushItemWidth(-1);
                    ImGui::InputTextWithHint("##filter", "Type to filter or enter new name", recOutputFilterBufs[i].data(), recOutputFilterBufs[i].size());
                    ImGui::PopItemWidth();
                    ImGui::PopID();
                    ImGui::Separator();

                    std::string filter(recOutputFilterBufs[i].data());
                    bool anyShown = false;
                    for (const auto &res: gd.resources) {
                        if (res.id == 0) continue;
                        if (!filter.empty()) {
                            if (res.name.find(filter) == std::string::npos) continue;
                        }
                        anyShown = true;
                        bool selected = (res.id == recOutputs[i].first);
                        if (ImGui::Selectable(res.name.c_str(), selected)) {
                            recOutputs[i].first = res.id;
                            strncpy(recOutputNameBufs[i].data(), res.name.c_str(), recOutputNameBufs[i].size());
                            recOutputFilterBufs[i][0] = '\0';
                        }
                    }

                    if (!anyShown) {
                        ImGui::TextDisabled("No resources match.");
                    }
                    ImGui::Separator();

                    std::string typed = recOutputFilterBufs[i][0] ? std::string(recOutputFilterBufs[i].data()) : std::string(recOutputNameBufs[i].data());
                    if (!typed.empty()) {
                        std::string createLabel = "Create new resource: '" + typed + "'";
                        if (ImGui::Selectable(createLabel.c_str(), false)) {
                            Resource newRes;
                            newRes.name = typed;
                            newRes.key_name = slugify(typed);
                            int newId = gameDataManager.addResource(newRes);
                            if (newId > 0) {
                                recOutputs[i].first = newId;
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
            recOutputs.emplace_back(0, 1.0);
        }

        ImGui::Separator();
        ImGui::Text("Compatible Machines");

        // show checkboxes for machines (compress if many)
        ImGui::BeginChild("machCheckboxes", ImVec2(0, 240), true);
        for (const auto &m: gd.machines) {
            bool checked = recMachines.count(m.id);
            std::string cbLabel = m.name + " (" + m.key_name + ")";
            if (ImGui::Checkbox(cbLabel.c_str(), &checked)) {
                if (checked) recMachines.insert(m.id);
                else recMachines.erase(m.id);
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();

        // Save / Cancel buttons
        if (ImGui::Button("Save Recipe")) {
            // Build recipe object
            Recipe r;
            r.id = selRecipeId;
            r.name = recNameBuf;
            r.key_name = slugify(recNameBuf);
            r.time_seconds = recTimeSeconds;

            // Before copying ports, ensure any typed-but-uncreated resources are created.
            // (Search by name among existing resources; if not found, create.)
            auto findResourceByName = [&](const std::string &name) -> const Resource* {
                for (const auto &res : gd.resources) {
                    if (res.name == name) return &res;
                }
                return nullptr;
            };

            // For inputs: create missing resources if any (buffer arrays hold names)
            // We must retrieve the mutable name buffers we prepared earlier.
            // If a recInputs entry has id==0 but a name in the matching name buffer, create it.
            // NOTE: we attempt to reuse the name buffers prepared above if sizes match.
            {
                // ensure we have consistent buffer sizes; if not, fallback to trying to look up by id only
                if (recInputNameBufs.size() == recInputs.size()) {
                    for (size_t i = 0; i < recInputs.size(); ++i) {
                        if (recInputs[i].first == 0) {
                            std::string maybeName = recInputNameBufs[i].data();
                            if (!maybeName.empty()) {
                                const Resource *found = findResourceByName(maybeName);
                                if (found) {
                                    recInputs[i].first = found->id;
                                } else {
                                    Resource newRes;
                                    newRes.name = maybeName;
                                    newRes.key_name = slugify(maybeName);
                                    int newId = gameDataManager.addResource(newRes);
                                    if (newId > 0) {
                                        recInputs[i].first = newId;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Same for outputs
            {
                if (recOutputNameBufs.size() == recOutputs.size()) {
                    for (size_t i = 0; i < recOutputs.size(); ++i) {
                        if (recOutputs[i].first == 0) {
                            std::string maybeName = recOutputNameBufs[i].data();
                            if (!maybeName.empty()) {
                                const Resource *found = findResourceByName(maybeName);
                                if (found) {
                                    recOutputs[i].first = found->id;
                                } else {
                                    Resource newRes;
                                    newRes.name = maybeName;
                                    newRes.key_name = slugify(maybeName);
                                    int newId = gameDataManager.addResource(newRes);
                                    if (newId > 0) {
                                        recOutputs[i].first = newId;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // copy ports into r
            r.input_ports.clear();
            for (auto &p: recInputs) r.input_ports.push_back(RecipePort{p.second, p.first});
            r.output_ports.clear();
            for (auto &p: recOutputs) r.output_ports.push_back(RecipePort{p.second, p.first});

            r.produced_in_machines_ids.clear();
            for (int mid: recMachines) r.produced_in_machines_ids.push_back(mid);

            std::string err;
            if (!gameDataManager.editRecipe(selRecipeId, r, err)) {
                // show error (you can use a modal or store err into a temp char buf to show)
            } else {
                // success - optionally mark dirty false
                recipeDirty = false;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            // reload from gd
            auto it = std::find_if(gd.recipes.begin(), gd.recipes.end(),
                                   [&](const Recipe &rr) { return rr.id == selRecipeId; });
            if (it != gd.recipes.end()) {
                strncpy(recNameBuf, it->name.c_str(), sizeof(recNameBuf));
                recTimeSeconds = it->time_seconds;
                recInputs.clear();
                recOutputs.clear();
                recMachines.clear();
                for (auto &p: it->input_ports) recInputs.emplace_back(p.resource_id, p.amount);
                for (auto &p: it->output_ports) recOutputs.emplace_back(p.resource_id, p.amount);
                for (int mid: it->produced_in_machines_ids) recMachines.insert(mid);
            }
        }
    }

    ImGui::EndChild();
    ImGui::Columns(1);
}
