#include "Application.h"
#include <algorithm>
#include <fstream>
#include "imgui/imgui_internal.h"

#include <thread>
#include <atomic>
#include <iostream>
#include <set>
#include <GLFW/glfw3.h>

#include "nativefiledialog-extended/src/include/nfd.h"
#include "FilesystemUtils.h"
#include "SettingsManager.h"
#include "FactoryNodeEditor.h"
#include "RecentFiles.h"
#include "SessionManager.h"

Application::Application() {
    SettingsManager::instance().load();

    RecentFiles::instance().load();

    if (SettingsManager::instance().getSettings().restorePreviousSession) {
        RestoreSession();
    }

    auto io = ImGui::GetIO();
    std::filesystem::path folderPath = SettingsManager::instance().getSettings().executablePath / "assets" / "fonts";
    std::vector<std::string> fontFiles;
    try {
        for (auto const &entry : std::filesystem::recursive_directory_iterator(folderPath)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".ttf" || ext == ".otf") {
                fontFiles.push_back(entry.path().string());
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "Error scanning fonts folder: " << e.what() << std::endl;
        return;
    }

    if (fontFiles.empty()) {
        std::cout << "No .ttf/.otf files found in " << folderPath << std::endl;
        return;
    }

    std::sort(fontFiles.begin(), fontFiles.end());

    // Add each font
    for (auto &fp : fontFiles) {
        ImFont* font = io.Fonts->AddFontFromFileTTF(fp.c_str());
        if (!font) {
            std::cerr << "Failed to load font: " << fp << std::endl;
        }
    }

    ImFont* font = nullptr;

    for (ImFont* f : ImGui::GetIO().Fonts->Fonts) {
        if (f->GetDebugName() == SettingsManager::instance().getSettings().fontName) {
            font = f;
            break;
        }
    }

    if (font) ImGui::GetIO().FontDefault = font;
    auto im_gui_style = &ImGui::GetStyle();
    im_gui_style->_NextFrameFontSizeBase = SettingsManager::instance().getSettings().fontSize;
    //
}

Application::~Application() {
    SettingsManager::instance().save();
    RecentFiles::instance().save();
    SaveSession();
    editors.clear();
}

void Application::Draw() {
    if (SettingsManager::instance().getSettings().themeName != currentTheme || SettingsManager::instance().getSettings().showGrid != currentShowGrid) {
        currentTheme = SettingsManager::instance().getSettings().themeName;
        currentShowGrid = SettingsManager::instance().getSettings().showGrid;
        ChangeTheme();
    }

    HandleShortcuts();
    DrawDebugWindow();
    DrawMenuBar();
    DrawNewProjectDialog();
    DrawOpenProjectDialog();
    DrawSaveAsDialog();
    gameDataEditor.Draw();
    settingsEditor.Draw();
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                  ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    ImGui::Begin("Root", nullptr, host_flags);
    ImGui::PopStyleVar(3); // Pop the style vars
    ImGuiID dockspace_id = ImGui::GetID("Root");
    ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

    if (!dockInitialized) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, host_flags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

        ImGui::DockBuilderFinish(dockspace_id);

        dockInitialized = true;
    }

    // Workaround for ImGuiConfigFlags_NavEnableKeyboard
    // When the Alt key is released alone, some systems or ImGui's internal navigation state can cause focus to be lost.
    if (ImGui::IsKeyReleased(ImGuiKey_LeftAlt) || ImGui::IsKeyReleased(ImGuiKey_RightAlt)) {
        if (activeEditor != -1) {
            // If an editor window was previously active (indicated by activeEditor not being -1),
            // we'll explicitly restore keyboard focus to it.
            ImGui::SetWindowFocus(editors[activeEditor]->GetName().c_str());
        }
    }
    for (int i = 0; i < static_cast<int>(editors.size()); ++i) {
        auto &editor = editors[i];
        if (editor) {
            ImGui::SetNextWindowDockID(dockspace_id, ImGuiCond_FirstUseEver);

            // Give each window a unique ID if you have duplicate names
            ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(640, 480));
            bool is_open = true;
            ImGui::Begin(editor->GetName().c_str(), &is_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
            ImGui::PopStyleVar();
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                activeEditor = i;
            }
            editor->Draw();
            ImGui::End();
            if (!is_open) {
                CloseEditor(i);
            }
        }
    }

    if (focusRequested != -1) {
        if (focusRequested >= 0 && focusRequested < static_cast<int>(editors.size())) {
            ImGui::SetWindowFocus(editors[focusRequested]->GetName().c_str());
            activeEditor = focusRequested;
        }
        focusRequested = -1; // Reset after focusing
    }

    if (showFileAlreadyOpenPopup) {
        ImGui::OpenPopup("FileAlreadyOpen");
        showFileAlreadyOpenPopup = false; // Reset after drawing
    }
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("FileAlreadyOpen", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("An editor for this file is already open.");
        if (ImGui::Button("OK")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}

void Application::ChangeTheme() {
    if (SettingsManager::instance().getSettings().themeName == "Dark") {
        ImGui::StyleColorsDark();
    } else {
        ImGui::StyleColorsLight();
    }

    for (auto &editor : editors) {
        if (editor) {
            ed::SetCurrentEditor(editor->GetContext());
            auto& ed_style = ed::GetStyle();

            if (SettingsManager::instance().getSettings().themeName == "Dark") {
                ed_style.Colors[ed::StyleColor_Bg] = ImColor(24, 24, 27, 255);
                ed_style.Colors[ed::StyleColor_Grid] = ImColor(36, 37, 40, 255);
                ed_style.Colors[ed::StyleColor_NodeBg] = ImColor(48, 49, 54, 205);
                ed_style.Colors[ed::StyleColor_NodeBorder] = ImColor(96, 96, 96, 255);
                ed_style.Colors[ed::StyleColor_HovNodeBorder] = ImColor(50, 175, 255, 255);
                ed_style.Colors[ed::StyleColor_HovLinkBorder] = ImColor(50, 175, 255, 255);
                ed_style.Colors[ed::StyleColor_SelNodeBorder] = ImColor(255, 176,  50, 255);
                ed_style.Colors[ed::StyleColor_SelLinkBorder] = ImColor(255, 176,  50, 255);
            } else {
                ed_style.Colors[ed::StyleColor_Bg]            = ImColor(248, 248, 248, 255);
                ed_style.Colors[ed::StyleColor_Grid]          = ImColor(220, 220, 220, 255);
                ed_style.Colors[ed::StyleColor_NodeBg]        = ImColor(255, 255, 255, 205);
                ed_style.Colors[ed::StyleColor_NodeBorder]    = ImColor(200, 200, 200, 255);
                ed_style.Colors[ed::StyleColor_HovNodeBorder] = ImColor(0, 120, 215, 255);
                ed_style.Colors[ed::StyleColor_HovLinkBorder] = ImColor(0, 120, 215, 255);
                ed_style.Colors[ed::StyleColor_SelNodeBorder] = ImColor(255, 130, 255, 255);
                ed_style.Colors[ed::StyleColor_SelLinkBorder] = ImColor(255, 130, 255, 255);
            }

            if (!SettingsManager::instance().getSettings().showGrid) {
                ed_style.Colors[ed::StyleColor_Grid] = ed_style.Colors[ed::StyleColor_Bg];
            }
            ed::SetCurrentEditor(nullptr);
        }
    }
}

void Application::DrawDebugWindow() {
    if (!showDebugWindow) return;
    int flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(500, 600));
    ImGui::Begin("Debug Window", &showDebugWindow, flags);
    ImGui::PopStyleVar();
    ImGui::SeparatorText("General");
    auto &io = ImGui::GetIO();
    ImGui::Text("FPS: %.2f (%.2gms)", io.Framerate, io.Framerate ? 1000.0f / io.Framerate : 0.0f);
    ImGui::Text("Number of open editors: %zu", editors.size());
    ImGui::Text("Active editor index: %d", activeEditor);
    ImGui::Text("Copy buffer size: Nodes: %d, Ports: %d, Connections: % d", copyBuffer.nodes.size(), copyBuffer.ports.size(), copyBuffer.connections.size());

    ImGui::SeparatorText("Editor Specific");
    if (activeEditor != -1 && activeEditor < static_cast<int>(editors.size())) {
        const DebugInfo &debugInfo = editors[activeEditor]->GetDebugInfo();
        ImGui::TextWrapped("File path: %s", debugInfo.filePath.c_str());
        ImGui::TextWrapped("Game data path: %s", debugInfo.gameDataPath.c_str());
        ImGui::Text("Total nodes: %d", debugInfo.totalNodes);
        ImGui::Text("Total connections: %d", debugInfo.totalConnections);
        ImGui::Text("Total ports: %d", debugInfo.totalPorts);
        ImGui::Text("Visible nodes: %d / %d", debugInfo.visibleNodes, debugInfo.totalNodes);
        ImGui::Text("Selection size: %d", debugInfo.selectionSize);
        ImGui::Text("Solver time: %.2fms", debugInfo.lastTotalSolveDurationMs);
        ImGui::Text(" - Setup time: %.2fms", debugInfo.lastSetupSolveDurationMs);
        ImGui::Text(" - Solve time: %.2fms", debugInfo.lastSolverDurationMs);
        ImGui::Text(" - Update factory time: %.2fms", debugInfo.lastUpdateFactoryDurationMs);
        ImGui::Text("Last solver status: %s", debugInfo.lastSolverStatus.c_str());
        ImGui::Text("Quadtree bounds: (%.1f, %.1f) to (%.1f, %.1f)",
                    debugInfo.quadtreeBoundsMin[0], debugInfo.quadtreeBoundsMin[1],
                    debugInfo.quadtreeBoundsMax[0], debugInfo.quadtreeBoundsMax[1]);
        ImGui::Text("View bounds: (%.1f, %.1f) to (%.1f, %.1f)",
                    debugInfo.viewBoundsMin[0], debugInfo.viewBoundsMin[1],
                    debugInfo.viewBoundsMax[0], debugInfo.viewBoundsMax[1]);
        ImGui::Text("Undo stack size: %zu", debugInfo.undoStackSize);
        ImGui::Text("Redo stack size: %zu", debugInfo.redoStackSize);
    } else {
        ImGui::Text("No active editor.");
    }

    ImGui::End();
}

void Application::DrawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", "Ctrl+N")) {
                showNewProjectDialog = true; // Show dialog to create new project
            }
            if (ImGui::MenuItem("Open", "Ctrl+O")) {
                showOpenProjectDialog = true; // Show dialog to open existing project
            }
            if (ImGui::BeginMenu("Open Recent")) {
                const auto &recentFiles = RecentFiles::instance().getFiles();
                if (recentFiles.empty()) {
                    ImGui::TextDisabled("No recent files");
                } else {
                    for (const auto &file : recentFiles) {
                        if (ImGui::MenuItem(file.stem().string().c_str())) {
                            CreateNewEditor("", file, file.stem().string());
                        }
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                SaveActiveEditor();
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
                showSaveDialog = true;
            }
            if (ImGui::MenuItem("Save a Copy")) {
                isSaveAsCopy = true;
                showSaveDialog = true;
            }
            if (ImGui::MenuItem("Save All")) {
                SaveAll();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close Active", "Ctrl+W") && !editors.empty()) {
                CloseActiveEditor();
            }
            if (ImGui::MenuItem("Close All")) {
                editors.clear();
                activeEditor = -1; // Reset active editor
                SaveSession();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Game Data Manager")) {
                gameDataEditor.SetOpen(true);
            }
            if (ImGui::MenuItem("Settings")) {
                settingsEditor.SetOpen(true);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
                quitRequested = true;
            }

            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
                UndoActiveEditor();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
                RedoActiveEditor();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "Ctrl+X")) {
                CutActiveEditor();
            }
            if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                CopyActiveEditor();
            }
            if (ImGui::MenuItem("Paste", "Ctrl+V")) {
                PasteActiveEditor(false);
            }
            if (ImGui::MenuItem("Paste Special", "Ctrl+Shift+V")) {
                PasteActiveEditor(true);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "Ctrl+A")) {
                SelectAllActiveEditor();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void Application::HandleShortcuts() {
    // Handle keyboard shortcuts
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        showNewProjectDialog = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F3))
        showDebugWindow = !showDebugWindow;{
    }
    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_N)) {
            showNewProjectDialog = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_O)) {
            showOpenProjectDialog = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_W) && !editors.empty()) {
            CloseActiveEditor();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_S)) {
            if (io.KeyShift) {
                showSaveDialog = true; // Show Save As dialog
            } else {
                SaveActiveEditor(); // Save current editor
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
            quitRequested = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Y)) {
            RedoActiveEditor();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) {
            if (io.KeyShift) {
                RedoActiveEditor();
            } else {
                UndoActiveEditor();
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_X)) {
            CutActiveEditor();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_C)) {
            CopyActiveEditor();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_V)) {
            PasteActiveEditor(io.KeyShift); // Shift key to map external connections
        }
        if (ImGui::IsKeyPressed(ImGuiKey_A)) {
            SelectAllActiveEditor();
        }
    }
}

void Application::DrawNewProjectDialog() {
    if (!showNewProjectDialog) return;

    constexpr const char* kProjectExtension = ".fpp";
    static char projectNameBuf[128] = "";
    static char locationBuf[512] = "";
    static bool initialized = false;
    static int selectedGameDataFile = -1;

    ImGui::SetNextWindowSize(ImVec2(800, 650), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("New Project", &showNewProjectDialog,
                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }

    // Gather game data files (fresh each frame in case files change)
    std::filesystem::path gameDataPath = SettingsManager::instance().getSettings().gameDataPath;
    std::vector<std::filesystem::path> gameDataFiles = GetGameDataFiles(gameDataPath);

    // One-time initialization of buffers
    if (!initialized) {
        initialized = true;
        std::string defaultName = GenerateDefaultEditorName();
        std::strncpy(projectNameBuf, defaultName.c_str(), sizeof(projectNameBuf));
        projectNameBuf[sizeof(projectNameBuf) - 1] = '\0';

        std::filesystem::path defaultLocation = SettingsManager::instance().getSettings().defaultProjectPath;
        std::strncpy(locationBuf, defaultLocation.string().c_str(), sizeof(locationBuf));
        locationBuf[sizeof(locationBuf) - 1] = '\0';

        selectedGameDataFile = gameDataFiles.empty() ? -1 : 0;
    }

    // --- Header ---
    ImGui::TextWrapped("Create a new project. The project filename will be <name>%s", kProjectExtension);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Name & Location ---
    ImGui::PushItemWidth(-1); // full width inputs
    ImGui::Text("Project name (filename without extension):");
    if (ImGui::InputText("##projname", projectNameBuf, sizeof(projectNameBuf))) {
        // sanitize: strip any path separators and extension if user pasted it
        std::string s(projectNameBuf);
        // Remove any directory components
        size_t pos = s.find_last_of("/\\");
        if (pos != std::string::npos) s = s.substr(pos + 1);
        // Remove extension if user included it
        if (s.size() > std::strlen(kProjectExtension) &&
            s.compare(s.size() - std::strlen(kProjectExtension), std::strlen(kProjectExtension), kProjectExtension) == 0) {
            s.resize(s.size() - std::strlen(kProjectExtension));
        }
        // Remove any remaining slashes
        s.erase(std::remove_if(s.begin(), s.end(), [](char c){ return c == '/' || c == '\\' || c == ':'; }), s.end());
        std::strncpy(projectNameBuf, s.c_str(), sizeof(projectNameBuf));
        projectNameBuf[sizeof(projectNameBuf) - 1] = '\0';
    }
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::Text("Location (folder):");
    ImGui::SameLine();

    ImGui::BeginGroup();
    float totalWidth = ImGui::GetContentRegionAvail().x;
    float buttonWidth = 60.0f;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float inputWidth = totalWidth - buttonWidth - spacing;

    ImGui::PushItemWidth(inputWidth);
    ImGui::InputText("##location", locationBuf, sizeof(locationBuf));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(buttonWidth, 0))) {
        std::thread([]() {
        nfdu8char_t *outPath;
        nfdpickfolderu8args_t args = {0};
        args.defaultPath = locationBuf;
        nfdresult_t result = NFD_PickFolderU8_With(&outPath, &args);

        if (result == NFD_OKAY) {
            std::strncpy(locationBuf, outPath, sizeof(locationBuf) - 1);
            locationBuf[sizeof(locationBuf) - 1] = '\0';
            NFD_FreePathU8(outPath);
        }
    }).detach();
    }
    ImGui::EndGroup();

    // Compute full path shown to the user (location / (projectName + ext))
    std::filesystem::path locationPath = std::filesystem::path(std::string(locationBuf));
    std::string projectNameStr = projectNameBuf;
    std::filesystem::path fullPath;
    if (!projectNameStr.empty()) {
        fullPath = locationPath / (projectNameStr + kProjectExtension);
    } else {
        fullPath = locationPath; // fallback if name empty
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Full project path:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", fullPath.string().c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Game configuration list ---
    ImGui::Text("Select Game Configuration:");
    ImGui::BeginChild("GameSelection", ImVec2(0, 180), true);
    if (gameDataFiles.empty()) {
        ImGui::TextDisabled("No game configuration files found in %s", gameDataPath.string().c_str());
        selectedGameDataFile = -1;
    } else {
        // Ensure selected index is valid
        if (selectedGameDataFile < 0) selectedGameDataFile = 0;
        if (selectedGameDataFile >= static_cast<int>(gameDataFiles.size())) selectedGameDataFile = static_cast<int>(gameDataFiles.size()) - 1;

        for (int i = 0; i < static_cast<int>(gameDataFiles.size()); ++i) {
            const auto &p = gameDataFiles[i];
            const std::string display = p.filename().string();
            bool isSelected = (selectedGameDataFile == i);
            if (ImGui::Selectable(display.c_str(), isSelected)) {
                selectedGameDataFile = i;
            }
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // --- Validation checks ---
    bool nameEmpty = projectNameStr.empty();
    bool nameExists = std::any_of(editors.begin(), editors.end(), [&](const auto &editor) {
        return editor->GetName() == projectNameStr;
    });

    bool projectExists = false;
    if (!fullPath.empty()) {
        projectExists = std::filesystem::exists(fullPath);
    }

    // Buttons
    ImGui::BeginGroup();
    // Create button disabled if invalid
    ImGui::BeginDisabled(nameEmpty || nameExists || projectExists || selectedGameDataFile == -1);
    if (ImGui::Button("Create", ImVec2(120, 0))) {
        // Ensure project directory exists
        std::error_code ec;
        std::filesystem::create_directories(locationPath, ec);
        // Create the editor/project (call existing function)
        if (selectedGameDataFile >= 0 && selectedGameDataFile < static_cast<int>(gameDataFiles.size())) {
            std::filesystem::path selectedGameData = gameDataPath / gameDataFiles.at(selectedGameDataFile);
            CreateNewEditor(selectedGameData, fullPath, projectNameStr);
        }
        showNewProjectDialog = false;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        showNewProjectDialog = false;
    }
    ImGui::EndGroup();

    // --- Inline validation messages ---
    if (nameEmpty) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Project name cannot be empty.");
    } else if (nameExists) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1, 0.2f, 0.2f, 1), "An editor with this name already exists.");
    }

    if (projectExists) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1, 0.2f, 0.2f, 1), "A project with this name already exists at the specified location.");
    }

    if (selectedGameDataFile == -1) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1, 0.2f, 0.2f, 1), "Please select a game configuration file.");
    }

    ImGui::End();
}

static std::atomic<bool> fileDialogRunning = false;
static std::string fileDialogResult;
static std::atomic<bool> fileDialogCancelled = false;

void Application::DrawOpenProjectDialog() {
    if (!showOpenProjectDialog) return;

    if (!fileDialogRunning && fileDialogResult.empty() && !fileDialogCancelled) {
        fileDialogRunning = true;
        fileDialogCancelled = false;

        std::thread([]() {
            nfdu8char_t *outPath;
            nfdopendialogu8args_t args = {0};
            nfdu8filteritem_t filters[1] = { { "Factory Planner Project", "fpp" } };
            args.filterList = filters;
            args.filterCount = 1;

            nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);

            if (result == NFD_OKAY) {
                fileDialogResult = outPath;
                NFD_FreePathU8(outPath);
            } else if (result == NFD_CANCEL) {
                fileDialogCancelled = true;
            } else {
                // Handle NFD_ERROR case
                fileDialogResult.clear();
                fileDialogCancelled = true;
            }

            fileDialogRunning = false;
        }).detach();
    }

    // Handle successful file selection
    if (!fileDialogResult.empty()) {
        std::string projectName = std::filesystem::path(fileDialogResult).stem().string();
        CreateNewEditor("", fileDialogResult, projectName);
        showOpenProjectDialog = false;
        fileDialogResult.clear();
    }

    // Handle cancellation
    if (fileDialogCancelled) {
        showOpenProjectDialog = false;
        fileDialogCancelled = false;
    }
}

void Application::DrawSaveAsDialog() {
    if (!showSaveDialog) return;
    if (editors.empty()) {
        showSaveDialog = false; // No editors to save
        return;
    }
    // Start native save dialog on background thread if not already running
    if (!fileDialogRunning && fileDialogResult.empty() && !fileDialogCancelled) {
        fileDialogRunning = true;
        fileDialogCancelled = false;

        std::thread([]() {
            nfdu8char_t *outPath = nullptr;
            nfdsavedialogu8args_t args = {0};
            nfdu8filteritem_t filters[1] = { { "Factory Planner Project", "fpp" } };
            args.filterList = filters;
            args.filterCount = 1;

            nfdresult_t result = NFD_SaveDialogU8_With(&outPath, &args);

            if (result == NFD_OKAY) {
                fileDialogResult = outPath ? outPath : "";
                if (outPath) NFD_FreePathU8(outPath);
            } else if (result == NFD_CANCEL) {
                fileDialogCancelled = true;
            } else {
                // NFD_ERROR
                fileDialogResult.clear();
                fileDialogCancelled = true;
            }

            fileDialogRunning = false;
        }).detach();
    }

    // If a path was picked, perform SaveAs and switch to the new file
    if (!fileDialogResult.empty()) {
        if (isSaveAsCopy) {
            SaveActiveEditorAs(fileDialogResult, SaveAsMode::KeepCurrentFile);
            isSaveAsCopy = false;
        } else {
            // check if the file is already open
            std::string newFileName = std::filesystem::path(fileDialogResult).stem().string();
            if (std::any_of(editors.begin(), editors.end(), [&](const auto &editor) {
                return editor->GetName() == newFileName;
            })) {
                // If an editor with the same name is already open, close it
                CloseEditorByName(newFileName);
            }
            SaveActiveEditorAs(fileDialogResult, SaveAsMode::SwitchToNewFile);
        }
        showSaveDialog = false;
        fileDialogResult.clear();
    }

    // Handle cancellation
    if (fileDialogCancelled) {
        showSaveDialog = false;
        fileDialogCancelled = false;
    }
}

void Application::RestoreSession() {
    restoringSession = true;
    SessionManager::instance().load();
    const auto& session = SessionManager::instance().getSessionState();
    for (const auto& path : session.openProjectPaths) {
        if (std::filesystem::exists(path)) {
            CreateNewEditor("", path, path.stem().string());
        } else {
            std::cerr << "Warning: Project file from previous session does not exist: " << path << std::endl;
        }
    }
    if (session.activeProjectIndex >= 0 && session.activeProjectIndex < static_cast<int>(editors.size())) {
        focusRequested = session.activeProjectIndex;
    }
    restoringSession = false;
}

void Application::SaveSession() {
    if (restoringSession) return; // Don't save while restoring

    SessionState state;
    for (auto &editor : editors) {
        if (editor) {
            state.openProjectPaths.push_back(editor->GetProjectFilePath());
        }
    }
    state.activeProjectIndex = activeEditor;
    SessionManager::instance().setSessionState(state);
    SessionManager::instance().save();
}

void Application::CreateNewEditor(const std::string &gameDataFilePath, const std::string &location,
                                  const std::string &name) {
    // Check if an editor with the same name already exists
    if (std::any_of(editors.begin(), editors.end(), [&](const auto &editor) {
        return editor->GetName() == name;
    })) {
        showFileAlreadyOpenPopup = true;
        return; // Do not create a new editor if the name already exists
    }
    // Create new editor with its own graph and solver
    if (gameDataFilePath.empty()) {
        auto editor = std::make_unique<FactoryNodeEditor>(GameData(), location, name);
        editors.push_back(std::move(editor));
    } else {
        gameDataManager.loadFromFile(gameDataFilePath,gameDataManagerError);
        if (!gameDataManagerError.empty()) {
            // todo: show error to user
            std::cerr << gameDataManagerError << std::endl;
        }

        auto editor = std::make_unique<FactoryNodeEditor>(gameDataManager.current(), location, name);
        editors.push_back(std::move(editor));
        gameDataManager.clear();
    }

    // Switch to the new tab
    activeEditor = static_cast<int>(editors.size()) - 1;

    SaveSession();
    RecentFiles::instance().addFile(location);
}

bool Application::SaveActiveEditor() {
    if (activeEditor < 0 || static_cast<size_t>(activeEditor) >= editors.size()) {
        return false; // No active editor to save
    }

    auto &editor = editors[activeEditor];
    if (editor) {
        editor->Save();
        return true;
    }
    return false;
}

bool Application::SaveActiveEditorAs(const std::string &newFilePath, SaveAsMode mode) {
    if (activeEditor < 0 || static_cast<size_t>(activeEditor) >= editors.size()) {
        return false; // No active editor to save
    }

    auto &editor = editors[activeEditor];
    if (editor) {
        return editor->SaveAs(newFilePath, mode);
    }
    return false;
}

void Application::SaveAll() {
    for (auto& editor : editors) {
        if (editor) {
            editor->Save();
        }
    }
}

void Application::CopyActiveEditor() {
    if (activeEditor < 0 || static_cast<size_t>(activeEditor) >= editors.size()) {
        return; // No active editor to copy
    }
    auto &editor = editors[activeEditor];
    if (editor) {
        ed::SetCurrentEditor(editor->GetContext());
        editor->copy(copyBuffer);
        ed::SetCurrentEditor(nullptr);
    }
}

void Application::CutActiveEditor() {
    if (activeEditor < 0 || static_cast<size_t>(activeEditor) >= editors.size()) {
        return; // No active editor to cut
    }
    auto &editor = editors[activeEditor];
    if (editor) {
        ed::SetCurrentEditor(editor->GetContext());
        editor->cut(copyBuffer);
        ed::SetCurrentEditor(nullptr);
    }
}

void Application::PasteActiveEditor(bool mapExternalConnections) {
    if (activeEditor < 0 || static_cast<size_t>(activeEditor) >= editors.size()) {
        return; // No active editor to paste into
    }
    auto &editor = editors[activeEditor];
    if (editor) {
        if (editor.get()->GetGameDataFilePath() != copyBuffer.gameDataFilePath) {
            return;
        }
        ed::SetCurrentEditor(editor->GetContext());
        editor->paste(copyBuffer, mapExternalConnections);
        ed::SetCurrentEditor(nullptr);
    }
}

void Application::UndoActiveEditor() {
    if (activeEditor < 0 || static_cast<size_t>(activeEditor) >= editors.size()) {
        return; // No active editor to undo
    }
    auto &editor = editors[activeEditor];
    if (editor) {
        ed::SetCurrentEditor(editor->GetContext());
        editor->undo();
        ed::SetCurrentEditor(nullptr);
    }
}

void Application::RedoActiveEditor() {
    if (activeEditor < 0 || static_cast<size_t>(activeEditor) >= editors.size()) {
        return; // No active editor to redo
    }
    auto &editor = editors[activeEditor];
    if (editor) {
        ed::SetCurrentEditor(editor->GetContext());
        editor->redo();
        ed::SetCurrentEditor(nullptr);
    }
}

void Application::SelectAllActiveEditor() {
    if (activeEditor < 0 || static_cast<size_t>(activeEditor) >= editors.size()) {
        return; // No active editor to select all
    }
    auto &editor = editors[activeEditor];
    if (editor) {
        ed::SetCurrentEditor(editor->GetContext());
        editor->selectAll();
        ed::SetCurrentEditor(nullptr);
    }
}

void Application::CloseEditor(int index) {
    // index is signed; compare safely with size()
    if (index < 0) return;
    if (static_cast<size_t>(index) >= editors.size()) return;

    editors.erase(editors.begin() + index);

    // Adjust activeEditor:
    if (editors.empty()) {
        activeEditor = -1;
    } else if (activeEditor >= static_cast<int>(editors.size())) {
        activeEditor = static_cast<int>(editors.size()) - 1;
    }

    SaveSession();
}

void Application::CloseEditorByName(const std::string& name) {
    auto it = std::find_if(editors.begin(), editors.end(), [&](const auto& editor) {
        return editor->GetName() == name;
    });
    if (it != editors.end()) {
        int index = static_cast<int>(std::distance(editors.begin(), it));
        CloseEditor(index);
    }
}

void Application::CloseActiveEditor() {
    if (editors.empty()) return;

    if (activeEditor >= 0 && static_cast<size_t>(activeEditor) < editors.size()) {
        CloseEditor(activeEditor);
    } else {
        // No focused editor, close the last tab as a sensible default
        CloseEditor(static_cast<int>(editors.size()) - 1);
    }
}


std::string Application::GenerateDefaultEditorName() {
    std::set<int> usedNumbers;

    // Extract all numbers from existing factory names
    for (const auto &editor: editors) {
        const std::string &name = editor->GetName();

        // Check if name starts with "Factory "
        if (name.substr(0, 8) == "Factory ") {
            std::string numberPart = name.substr(8);

            // Check if the rest is a valid number
            if (!numberPart.empty() && std::all_of(numberPart.begin(), numberPart.end(), ::isdigit)) {
                int number = std::stoi(numberPart);
                usedNumbers.insert(number);
            }
        }
    }

    // Find the first missing positive number
    int nextNumber = 1;
    while (usedNumbers.count(nextNumber) > 0) {
        nextNumber++;
    }

    return "Factory_" + std::to_string(nextNumber);
}
