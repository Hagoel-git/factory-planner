#include "Application.h"
#include <algorithm>
#include <fstream>
#include <thread>
#include <atomic>
#include <iostream>
#include <set>

#include <GLFW/glfw3.h>
#include "imgui/imgui_internal.h"
#include "nativefiledialog-extended/src/include/nfd.h"

#include "editor/factory_planner/FactoryNodeEditor.h"
#include "core/data/GameDataScanner.h"
#include "services/NotificationManager.h"
#include "services/SettingsManager.h"
#include "services/RecentFiles.h"
#include "services/SessionManager.h"
#include "services/TextureManager.h"

Application::Application() : m_gameDataEditor(m_gameDataManager) {
    VLOG(2) << "Application starting up.";
    SettingsManager::instance().load();
    RecentFiles::instance().load();

    if (SettingsManager::instance().getSettings().restorePreviousSession) {
        restoreSession();
    }

    VLOG(2) << "Loading fonts.";
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
        VLOG(2) << "Found " << fontFiles.size() << " font files.";
    } catch (const std::exception &e) {
        LOG(ERROR) << "Error scanning fonts folder: " << e.what();
        return;
    }

    if (fontFiles.empty()) {
        LOG(WARNING) << "No font files found, using default ImGui font.";
        return;
    }

    std::sort(fontFiles.begin(), fontFiles.end());

    // Add each font
    for (auto &fp : fontFiles) {
        ImFont* font = io.Fonts->AddFontFromFileTTF(fp.c_str());
        if (!font) {
            LOG(ERROR) << "Failed to load font: " << fp;
        } else {
            VLOG(2) << "Loaded font: " << fp << " as " << font->GetDebugName();
        }
    }

    const std::string& targetFontName = SettingsManager::instance().getSettings().fontName;

    ImFont* font = nullptr;

    for (ImFont* f : ImGui::GetIO().Fonts->Fonts) {
        if (f->GetDebugName() == SettingsManager::instance().getSettings().fontName) {
            font = f;
            break;
        }
    }
    if (font) {
        ImGui::GetIO().FontDefault = font;
        VLOG(2) << "Set default font to " << targetFontName;
    } else {
        LOG(WARNING) << "Could not find font '" << targetFontName
                     << "' from settings. Using ImGui default font.";
    }

    auto im_gui_style = &ImGui::GetStyle();
    im_gui_style->_NextFrameFontSizeBase = SettingsManager::instance().getSettings().fontSize;
    VLOG(2) << "Set default font size to " << SettingsManager::instance().getSettings().fontSize;
}

Application::~Application() {
    SettingsManager::instance().save();
    RecentFiles::instance().save();
    saveSession();
    m_editors.clear();
    TextureManager::instance().cleanup();
}

void Application::draw() {
    if (SettingsManager::instance().getSettings().themeName != m_currentTheme || SettingsManager::instance().getSettings().showGrid != m_currentShowGrid) {
        VLOG(1) << "Theme or grid setting changed, updating editors.";
        m_currentTheme = SettingsManager::instance().getSettings().themeName;
        m_currentShowGrid = SettingsManager::instance().getSettings().showGrid;
        applyThemeToAllEditors();
        VLOG(1) << "Applied theme: " << m_currentTheme;
    }

    handleShortcuts();
    drawDebugWindow();
    drawMenuBar();
    drawNewProjectDialog();
    drawOpenProjectDialog();
    drawSaveAsDialog();
    m_settingsEditor.draw();
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

    if (!m_dockInitialized) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, host_flags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

        ImGui::DockBuilderFinish(dockspace_id);

        m_dockInitialized = true;
    }

    // Workaround for ImGuiConfigFlags_NavEnableKeyboard
    // When the Alt key is released alone, some systems or ImGui's internal navigation state can cause focus to be lost.
    if (ImGui::IsKeyReleased(ImGuiKey_LeftAlt) || ImGui::IsKeyReleased(ImGuiKey_RightAlt)) {
        if (m_activeEditor != -1) {
            // If an editor window was previously active (indicated by activeEditor not being -1),
            // we'll explicitly restore keyboard focus to it.
            ImGui::SetWindowFocus(m_editors[m_activeEditor]->getName().c_str());
        }
    }
    m_gameDataEditor.draw();
    for (int i = 0; i < static_cast<int>(m_editors.size()); ++i) {
        auto &editor = m_editors[i];
        if (editor) {
            ImGui::SetNextWindowDockID(dockspace_id, ImGuiCond_FirstUseEver);

            // Give each window a unique ID if you have duplicate names
            ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(640, 480));
            bool is_open = true;
            ImGui::Begin(editor->getName().c_str(), &is_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
            ImGui::PopStyleVar();
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                m_activeEditor = i;
            }
            editor->draw();
            ImGui::End();
            if (!is_open) {
                closeEditor(i);
            }
        }
    }

    if (m_focusRequested != -1) {
        if (m_focusRequested >= 0 && m_focusRequested < static_cast<int>(m_editors.size())) {
            ImGui::SetWindowFocus(m_editors[m_focusRequested]->getName().c_str());
            m_activeEditor = m_focusRequested;
        }
        m_focusRequested = -1; // Reset after focusing
    }
    NotificationManager::instance().draw();
    if (m_showFileAlreadyOpenPopup) {
        ImGui::OpenPopup("FileAlreadyOpen");
        m_showFileAlreadyOpenPopup = false; // Reset after drawing
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

void Application::applyThemeToAllEditors() {
    if (SettingsManager::instance().getSettings().themeName == "Dark") {
        ImGui::StyleColorsDark();
    } else {
        ImGui::StyleColorsLight();
    }

    for (auto &editor : m_editors) {
        if (editor) {
            ed::SetCurrentEditor(editor->getContext());
            auto& ed_style = ed::GetStyle();

            if (SettingsManager::instance().getSettings().themeName == "Dark") {
                ed_style.Colors[ed::StyleColor_Bg] = ImColor(24, 24, 27, 255);
                ed_style.Colors[ed::StyleColor_Grid] = ImColor(36, 37, 40, 255);
                ed_style.Colors[ed::StyleColor_NodeBg] = ImColor(68, 70, 76, 205);
                ed_style.Colors[ed::StyleColor_NodeBorder] = ImColor(106, 106, 106, 255);
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

void Application::drawDebugWindow() {
    if (!m_showDebugWindow) return;
    int flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(500, 600));
    ImGui::Begin("Debug Window", &m_showDebugWindow, flags);
    ImGui::PopStyleVar();
    ImGui::SeparatorText("General");
    auto &io = ImGui::GetIO();
    ImGui::Text("FPS: %.2f (%.2gms)", io.Framerate, io.Framerate ? 1000.0f / io.Framerate : 0.0f);
    ImGui::Text("Number of open editors: %zu", m_editors.size());
    ImGui::Text("Active editor index: %d", m_activeEditor);
    ImGui::Text("Copy buffer size: Nodes: %d, Ports: %d, Connections: % d", m_copyBuffer.nodes.size(), m_copyBuffer.ports.size(), m_copyBuffer.connections.size());

    ImGui::SeparatorText("Editor Specific");
    if (m_activeEditor != -1 && m_activeEditor < static_cast<int>(m_editors.size())) {
        const DebugInfo &debugInfo = m_editors[m_activeEditor]->getDebugInfo();
        ImGui::Text(m_editors[m_activeEditor]->isFocused() ? "Focused" : "Not Focused");
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
    if (ImGui::Button("Simulate Crash")) {
        volatile int* ptr = nullptr;
        *ptr = 42; // Triggers SIGSEGV
    }
    if (ImGui::TreeNode("Notification Test")) {
        char title[64] = "System Update";
        char message[128] = "The operation completed successfully.";
        float duration = 5.0f;

        ImGui::Separator();

        if (ImGui::Button("Info")) {
            NotificationManager::instance().addNotification(title, message, NotificationType::Info, duration);
        }
        ImGui::SameLine();
        if (ImGui::Button("Success")) {
            NotificationManager::instance().addNotification(title, message, NotificationType::Success, duration);
        }
        ImGui::SameLine();
        if (ImGui::Button("Warning")) {
            NotificationManager::instance().addNotification(title, message, NotificationType::Warning, duration);
        }
        ImGui::SameLine();
        if (ImGui::Button("Error")) {
            NotificationManager::instance().addNotification(title, message, NotificationType::Error, duration);
        }
        ImGui::Separator();
        if (ImGui::Button("Test Long Text Wrapping")) {
            NotificationManager::instance().addNotification(
                "This Is A Very Long Title That Should Wrap Properly Without Hitting The Button",
                "This is a long paragraph designed to test if notification window height grows automatically. "
                "Since we fixed the width to 360px, this text should flow downward and push the next notification down.",
                NotificationType::Info,
                10.0f
            );
        }

        ImGui::TreePop();
    }

    ImGui::End();
}

void Application::drawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", "Ctrl+N")) {
                VLOG(1) << "Menu: New Project selected.";
                m_showNewProjectDialog = true; // Show dialog to create new project
            }
            if (ImGui::MenuItem("Open", "Ctrl+O")) {
                VLOG(1) << "Menu: Open Project selected.";
                m_showOpenProjectDialog = true; // Show dialog to open existing project
            }
            if (ImGui::BeginMenu("Open Recent")) {
                const auto &recentFiles = RecentFiles::instance().getFiles();
                if (recentFiles.empty()) {
                    ImGui::TextDisabled("No recent files");
                } else {
                    for (const auto &file : recentFiles) {
                        if (ImGui::MenuItem(file.stem().string().c_str())) {
                            if (!std::filesystem::exists(file)) {
                                LOG(WARNING) << "Recent file does not exist: " << file;
                                NotificationManager::instance().addNotification(
                                    "Error Opening Recent File",
                                    "The file does not exist: " + file.string(),
                                    NotificationType::Error);
                                continue;
                            }
                            VLOG(1)<< "Menu: Open Recent selected for file: " << file.string();
                            createNewEditor("", file, file.stem().string());
                        }
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Reopen Last Closed Editor", "Ctrl+Shift+T")) {
                VLOG(1) << "Menu: Reopen Last Closed Editor selected.";
                reopenLastClosedEditor();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                VLOG(1) << "Menu: Save selected.";
                saveActiveEditor();
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
                VLOG(1) << "Menu: Save As selected.";
                m_showSaveDialog = true;
            }
            if (ImGui::MenuItem("Save a Copy")) {
                VLOG(1) << "Menu: Save a Copy selected.";
                m_isSaveAsCopy = true;
                m_showSaveDialog = true;
            }
            if (ImGui::MenuItem("Save All")) {
                VLOG(1) << "Menu: Save All selected.";
                saveAll();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close Active", "Ctrl+W") && !m_editors.empty()) {
                VLOG(1) << "Menu: Close Active Editor selected.";
                closeActiveEditor();
            }
            if (ImGui::MenuItem("Close All")) {
                VLOG(1) << "Menu: Close All Selected.";
                m_editors.clear();
                m_activeEditor = -1; // Reset active editor
                saveSession();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Game Data Manager")) {
                VLOG(1) << "Menu: Open Game Data Manager selected.";
                m_gameDataEditor.setOpen(true);
            }
            if (ImGui::MenuItem("Settings")) {
                VLOG(1) << "Menu: Settings selected.";
                m_settingsEditor.setOpen(true);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
                VLOG(1) << "Menu: Quit selected.";
                quitRequested = true;
            }

            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
                VLOG(1) << "Menu: Undo selected.";
                undoActiveEditor();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
                VLOG(1) << "Menu: Redo selected.";
                redoActiveEditor();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "Ctrl+X")) {
                VLOG(1) << "Menu: Cut selected.";
                cutActiveEditor();
            }
            if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                VLOG(1) << "Menu: Copy selected.";
                copyActiveEditor();
            }
            if (ImGui::MenuItem("Paste", "Ctrl+V")) {
                VLOG(1) << "Menu: Paste selected.";
                pasteActiveEditor(false);
            }
            if (ImGui::MenuItem("Paste Special", "Ctrl+Shift+V")) {
                VLOG(1) << "Menu: Paste Special selected.";
                pasteActiveEditor(true);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "Ctrl+A")) {
                VLOG(1) << "Menu: Select All selected.";
                selectAllActiveEditor();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Show Flow", "Z")) {
                VLOG(1) << "Menu: Show Flow selected.";
                showFlowActiveEditor();
            }
            if (ImGui::MenuItem("Fit View", "F")) {
                VLOG(1) << "Menu: Fit View selected.";
                fitViewActiveEditor();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void Application::handleShortcuts() {
    // Handle keyboard shortcuts
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        VLOG(1) << "Escape key pressed, closing dialogs.";
        m_showNewProjectDialog = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F3)) {
        VLOG(1) << "F3 key pressed, toggling debug window.";
        m_showDebugWindow = !m_showDebugWindow;
    }
    bool editorFocused = !m_editors.empty() && m_editors[m_activeEditor]->isFocused();
    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_N)) {
            VLOG(1) << "Shortcut: Ctrl+N pressed, opening New Project dialog.";
            m_showNewProjectDialog = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_O)) {
            VLOG(1) << "Shortcut: Ctrl+O pressed, opening Open Project dialog.";
            m_showOpenProjectDialog = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
            VLOG(1) << "Shortcut: Ctrl+Q pressed, quitting application.";
            quitRequested = true;
        }
        if (io.KeyShift) {
            if (ImGui::IsKeyPressed(ImGuiKey_T)) {
                VLOG(1) << "Shortcut: Ctrl+Shift+T pressed, reopening last closed editor.";
                reopenLastClosedEditor();
            }
        }
        if (editorFocused) {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) {
                VLOG(1) << "Shortcut: Ctrl+W pressed, closing active editor.";
                closeActiveEditor();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_S)) {
                if (io.KeyShift) {
                    VLOG(1) << "Shortcut: Ctrl+Shift+S pressed, opening Save As dialog.";
                    m_showSaveDialog = true; // Show Save As dialog
                } else {
                    VLOG(1) << "Shortcut: Ctrl+S pressed, saving active editor.";
                    saveActiveEditor(); // Save current editor
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Y)) {
                VLOG(1) << "Shortcut: Ctrl+Y pressed, redoing in active editor.";
                redoActiveEditor();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Z)) {
                if (io.KeyShift) {
                    VLOG(1)<< "Shortcut: Ctrl+Shift+Z pressed, redoing in active editor.";
                    redoActiveEditor();
                } else {
                    VLOG(1) << "Shortcut: Ctrl+Z pressed, undoing in active editor.";
                    undoActiveEditor();
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_X)) {
                VLOG(1) << "Shortcut: Ctrl+X pressed, cutting in active editor.";
                cutActiveEditor();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_C)) {
                VLOG(1) << "Shortcut: Ctrl+C pressed, copying in active editor.";
                copyActiveEditor();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_V)) {
                VLOG(1) << "Shortcut: Ctrl+V " << (io.KeyShift ? "(Special)" : "") << " pressed, pasting in active editor.";
                pasteActiveEditor(io.KeyShift); // Shift key to map external connections
            }
            if (ImGui::IsKeyPressed(ImGuiKey_A)) {
                VLOG(1) << "Shortcut: Ctrl+A pressed, selecting all in active editor.";
                selectAllActiveEditor();
            }
        }
    } else {
        if (editorFocused) {
            if (io.WantTextInput) return;
            if (ImGui::IsKeyPressed(ImGuiKey_F)) {
                VLOG(1) << "F Key pressed, fitting view in active editor.";
                fitViewActiveEditor();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Z)) {
                VLOG(1) << "Z Key pressed. Showing flow in active editor.";
                showFlowActiveEditor();
            }
        }
    }
}

DialogState dirDialogState;

void Application::drawNewProjectDialog() {
    if (!m_showNewProjectDialog) return;

    constexpr const char* kProjectExtension = ".fpp";
    static char projectNameBuf[128] = "";
    static char locationBuf[512] = "";
    static int selectedGameDataFile = -1;

    ImGui::SetNextWindowSize(ImVec2(800, 650), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("New Project", &m_showNewProjectDialog,
                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }

    if (ImGui::IsWindowAppearing()) {
        std::filesystem::path gameDataPath = SettingsManager::instance().getSettings().gameDataPath;
        m_cachedGameDataPackages = scanForGameData(gameDataPath);

        selectedGameDataFile = m_cachedGameDataPackages.empty() ? -1 : 0;

        std::string defaultName = generateDefaultEditorName();
        std::strncpy(projectNameBuf, defaultName.c_str(), sizeof(projectNameBuf));
        projectNameBuf[sizeof(projectNameBuf) - 1] = '\0';

        std::filesystem::path defaultLocation = SettingsManager::instance().getSettings().defaultProjectPath;
        std::strncpy(locationBuf, defaultLocation.string().c_str(), sizeof(locationBuf));
        locationBuf[sizeof(locationBuf) - 1] = '\0';

        std::lock_guard<std::mutex> lock(dirDialogState.mutex);
        dirDialogState.resultPath.clear();
        dirDialogState.errorMessage.clear();
        dirDialogState.hasError = false;
        dirDialogState.isCancelled = false;

        VLOG(2) << "New Project dialog initialized.";
    }

    {
        std::unique_lock<std::mutex> lock(dirDialogState.mutex);

        if (!dirDialogState.isRunning) {
            // 1. Handle Path Result
            if (!dirDialogState.resultPath.empty()) {
                std::strncpy(locationBuf, dirDialogState.resultPath.c_str(), sizeof(locationBuf) - 1);
                locationBuf[sizeof(locationBuf) - 1] = '\0';
                dirDialogState.resultPath.clear();
                LOG(INFO) << "New Project: Selected location: " << locationBuf;
            }

            // 2. Handle Error
            if (dirDialogState.hasError) {
                std::string msg = dirDialogState.errorMessage;
                dirDialogState.hasError = false;
                dirDialogState.errorMessage.clear();

                lock.unlock();

                NotificationManager::instance().addNotification("Error", msg, NotificationType::Error, 5.0f);
            }

            // 3. Handle Cancellation (reset flag)
            if (dirDialogState.isCancelled) {
                dirDialogState.isCancelled = false;
            }
        }
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
        std::string s(projectNameBuf);
        size_t pos = s.find_last_of("/\\");
        if (pos != std::string::npos) s = s.substr(pos + 1);
        if (s.size() > std::strlen(kProjectExtension) &&
            s.compare(s.size() - std::strlen(kProjectExtension), std::strlen(kProjectExtension), kProjectExtension) == 0) {
            s.resize(s.size() - std::strlen(kProjectExtension));
        }
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
        std::lock_guard<std::mutex> lock(dirDialogState.mutex);
        if (!dirDialogState.isRunning) {
            VLOG(1) << "New Project: Browse for location button clicked.";
            dirDialogState.isRunning = true;
            dirDialogState.isCancelled = false;
            dirDialogState.hasError = false;

            std::thread([]() {
                nfdu8char_t *outPath = nullptr;
                nfdpickfolderu8args_t args = {0};
                nfdresult_t result = NFD_PickFolderU8_With(&outPath, &args);

                std::lock_guard<std::mutex> threadLock(dirDialogState.mutex);
                if (result == NFD_OKAY && outPath) {
                    dirDialogState.resultPath = outPath;
                    NFD_FreePathU8(outPath);
                } else if (result == NFD_CANCEL) {
                    dirDialogState.isCancelled = true;
                } else {
                    dirDialogState.hasError = true;
                    dirDialogState.errorMessage = "An error occurred while opening the directory selection dialog.";
                    LOG(ERROR) << "New Project: File dialog error.";
                }
                dirDialogState.isRunning = false;
            }).detach();
        }
    }
    ImGui::EndGroup();

    std::filesystem::path locationPath = std::filesystem::path(std::string(locationBuf));
    std::string projectNameStr = projectNameBuf;
    std::filesystem::path fullPath;
    if (!projectNameStr.empty()) {
        fullPath = locationPath / (projectNameStr + kProjectExtension);
    } else {
        fullPath = locationPath;
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

    if (m_cachedGameDataPackages.empty()) {
        ImGui::TextDisabled("No game configuration files found.");
        selectedGameDataFile = -1;
    } else {
        // Helper structs for sorting
        struct FoldableGroup {
            std::string gameName;
            std::vector<std::pair<std::string, int>> items; // {dataName, originalIndex}
        };

        struct SelectableItem {
            std::string gameName;
            std::string dataName;
            int originalIndex;
        };

        // Group packages
        std::map<std::string, std::vector<std::pair<std::string, int>>> groupedMap;
        for (int i = 0; i < static_cast<int>(m_cachedGameDataPackages.size()); ++i) {
            const auto& pkg = m_cachedGameDataPackages[i];
            groupedMap[pkg.gameName].push_back({pkg.dataName, i});
        }

        std::vector<FoldableGroup> foldableGroups;
        std::vector<SelectableItem> selectableItems;

        for (const auto& pair : groupedMap) {
            if (pair.second.size() == 1) {
                selectableItems.push_back({pair.first, pair.second[0].first, pair.second[0].second});
            } else {
                foldableGroups.push_back({pair.first, pair.second});
            }
        }

        // Render Groups
        for (const auto& group : foldableGroups) {
            if (ImGui::TreeNode(group.gameName.c_str())) {
                for (const auto& item : group.items) {
                    bool isSelected = (selectedGameDataFile == item.second);
                    if (ImGui::Selectable(item.first.c_str(), isSelected)) {
                        selectedGameDataFile = item.second;
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::TreePop();
            }
        }

        if (!foldableGroups.empty() && !selectableItems.empty()) ImGui::Separator();

        // Render Items
        for (const auto& item : selectableItems) {
            std::string displayName = item.gameName + " - " + item.dataName;
            bool isSelected = (selectedGameDataFile == item.originalIndex);
            if (ImGui::Selectable(displayName.c_str(), isSelected)) {
                selectedGameDataFile = item.originalIndex;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // Validation
    bool nameEmpty = projectNameStr.empty();
    bool nameExists = std::any_of(m_editors.begin(), m_editors.end(), [&](const auto &editor) {
        return editor->getName() == projectNameStr;
    });

    bool projectExists = false;
    if (!fullPath.empty()) {
        try { projectExists = std::filesystem::exists(fullPath); } catch (...) {}
    }

    // Buttons
    ImGui::BeginGroup();
    ImGui::BeginDisabled(nameEmpty || nameExists || projectExists || selectedGameDataFile == -1);
    if (ImGui::Button("Create", ImVec2(120, 0))) {
        LOG(INFO) << "New Project: Creating new project: " << fullPath.string();
        // Ensure project directory exists
        std::error_code ec;
        std::filesystem::create_directories(locationPath, ec);

        if (selectedGameDataFile >= 0 && selectedGameDataFile < static_cast<int>(m_cachedGameDataPackages.size())) {
            std::filesystem::path gameDataPath = SettingsManager::instance().getSettings().gameDataPath;
            std::filesystem::path selectedGameData = gameDataPath / m_cachedGameDataPackages.at(selectedGameDataFile).dataFilePath;

            createNewEditor(selectedGameData, fullPath, projectNameStr);
        }
        m_showNewProjectDialog = false;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        VLOG(1) << "New Project: Cancelled by user.";
        m_showNewProjectDialog = false;
    }
    ImGui::EndGroup();

    // Error Messages
    if (nameEmpty) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Project name cannot be empty.");
    } else if (nameExists) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1, 0.2f, 0.2f, 1), "An editor with this name already exists.");
    }
    if (projectExists) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1, 0.2f, 0.2f, 1), "A project file already exists at this location.");
    }
    if (selectedGameDataFile == -1) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1, 0.2f, 0.2f, 1), "Please select a game configuration file.");
    }

    ImGui::End();
}

static DialogState openDialogState;

void Application::drawOpenProjectDialog() {
    if (!m_showOpenProjectDialog) return;

    {
        std::unique_lock<std::mutex> lock(openDialogState.mutex);

        if (!openDialogState.isRunning) {
            // 1. Handle Success
            if (!openDialogState.resultPath.empty()) {
                std::string path = openDialogState.resultPath;
                openDialogState.resultPath.clear();

                lock.unlock();

                std::string projectName = std::filesystem::path(path).stem().string();
                createNewEditor(std::filesystem::path(), path, projectName);
                m_showOpenProjectDialog = false;
                return;
            }

            // 2. Handle Error
            if (openDialogState.hasError) {
                std::string msg = openDialogState.errorMessage;
                openDialogState.hasError = false;
                openDialogState.errorMessage.clear();

                lock.unlock();
                NotificationManager::instance().addNotification("Error", msg, NotificationType::Error);
            }

            // 3. Handle Cancel
            if (openDialogState.isCancelled) {
                m_showOpenProjectDialog = false;
                openDialogState.isCancelled = false;
            }
        }
    }

    bool shouldStart = false;
    {
        std::lock_guard<std::mutex> lock(openDialogState.mutex);
        if (!openDialogState.isRunning && openDialogState.resultPath.empty() && !openDialogState.isCancelled && !openDialogState.hasError) {
            shouldStart = true;
        }
    }

    if (shouldStart) {
        VLOG(2) << "Opening native file dialog for project selection.";

        {
            std::lock_guard<std::mutex> lock(openDialogState.mutex);
            openDialogState.isRunning = true;
            openDialogState.isCancelled = false;
            openDialogState.hasError = false;
        }

        std::thread([]() {
            nfdu8char_t *outPath;
            nfdopendialogu8args_t args = {0};
            nfdu8filteritem_t filters[1] = { { "Factory Planner Project", "fpp" } };
            args.filterList = filters;
            args.filterCount = 1;

            nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);

            std::lock_guard<std::mutex> lock(openDialogState.mutex);
            if (result == NFD_OKAY && outPath) {
                openDialogState.resultPath = outPath;
                NFD_FreePathU8(outPath);
            } else if (result == NFD_CANCEL) {
                openDialogState.isCancelled = true;
            } else {
                openDialogState.hasError = true;
                openDialogState.errorMessage = "An error occurred while trying to open the file dialog.";
                LOG(ERROR) << "Open dialog error.";
            }

            openDialogState.isRunning = false;
        }).detach();
    }
}

static DialogState saveDialogState;

void Application::drawSaveAsDialog() {
    if (!m_showSaveDialog) return;
    if (m_editors.empty()) {
        m_showSaveDialog = false; // No editors to save
        return;
    }
    {
        std::unique_lock<std::mutex> lock(saveDialogState.mutex);

        if (!saveDialogState.isRunning) {
            // 1. Handle Success
            if (!saveDialogState.resultPath.empty()) {
                std::string path = saveDialogState.resultPath;
                saveDialogState.resultPath.clear();

                lock.unlock();

                if (m_isSaveAsCopy) {
                    saveActiveEditorAs(path, SaveAsMode::KeepCurrentFile);
                    m_isSaveAsCopy = false;
                } else {
                    std::string newFileName = std::filesystem::path(path).stem().string();

                    // Check for duplicate names
                    bool nameExists = std::any_of(m_editors.begin(), m_editors.end(), [&](const auto &editor) {
                        return editor->getName() == newFileName;
                    });

                    if (nameExists) {
                        closeEditorByName(newFileName);
                    }
                    saveActiveEditorAs(path, SaveAsMode::SwitchToNewFile);
                }
                m_showSaveDialog = false;
                return;
            }

            // 2. Handle Error
            if (saveDialogState.hasError) {
                std::string msg = saveDialogState.errorMessage;
                saveDialogState.hasError = false;
                saveDialogState.errorMessage.clear();

                lock.unlock();
                NotificationManager::instance().addNotification("Error", msg, NotificationType::Error);

                m_showSaveDialog = false;
            }

            // 3. Handle Cancel
            if (saveDialogState.isCancelled) {
                m_showSaveDialog = false;
                saveDialogState.isCancelled = false;
            }
        }
    }

    bool shouldStart = false;
    {
        std::lock_guard<std::mutex> lock(saveDialogState.mutex);
        if (!saveDialogState.isRunning && saveDialogState.resultPath.empty() && !saveDialogState.isCancelled && !saveDialogState.hasError) {
            shouldStart = true;
        }
    }

    if (shouldStart) {
        VLOG(2) << "Opening native file dialog for Save As.";
        {
            std::lock_guard<std::mutex> lock(saveDialogState.mutex);
            saveDialogState.isRunning = true;
            saveDialogState.isCancelled = false;
            saveDialogState.hasError = false;
        }

        std::thread([]() {
            nfdu8char_t *outPath = nullptr;
            nfdsavedialogu8args_t args = {0};
            nfdu8filteritem_t filters[1] = { { "Factory Planner Project", "fpp" } };
            args.filterList = filters;
            args.filterCount = 1;

            nfdresult_t result = NFD_SaveDialogU8_With(&outPath, &args);

            std::lock_guard<std::mutex> lock(saveDialogState.mutex);
            if (result == NFD_OKAY && outPath) {
                saveDialogState.resultPath = outPath;
                NFD_FreePathU8(outPath);
            } else if (result == NFD_CANCEL) {
                saveDialogState.isCancelled = true;
            } else {
                saveDialogState.hasError = true;
                saveDialogState.errorMessage = "An error occurred while opening the save dialog.";
                LOG(ERROR) << "Save dialog error.";
            }

            saveDialogState.isRunning = false;
        }).detach();
    }
}

void Application::restoreSession() {
    m_restoringSession = true;
    SessionManager::instance().load();
    const auto& session = SessionManager::instance().getSessionState();
    for (const auto& path : session.openProjectPaths) {
        if (std::filesystem::exists(path)) {
            createNewEditor("", path, path.stem().string(), false);
        } else {
            LOG(WARNING) << "Project file from previous session does not exist: " << path;
        }
    }
    if (session.activeProjectIndex >= 0 && session.activeProjectIndex < static_cast<int>(m_editors.size())) {
        m_focusRequested = session.activeProjectIndex;
    }
    m_restoringSession = false;
}

void Application::reopenLastClosedEditor() {
    if (m_closedEditorHistory.empty()) return;

    std::filesystem::path lastClosedPath = m_closedEditorHistory.back();
    m_closedEditorHistory.pop_back();

    if (std::filesystem::exists(lastClosedPath)) {
        std::string projectName = lastClosedPath.stem().string();
        createNewEditor("", lastClosedPath, projectName);
        VLOG(1) << "Reopened last closed editor: " << lastClosedPath;
    } else {
        LOG(WARNING) << "Cannot reopen last closed editor, file does not exist: " << lastClosedPath;
        NotificationManager::instance().addNotification("Reopen Failed",
            "The last closed project file does not exist: " + lastClosedPath.string(),
            NotificationType::Warning);
    }
}

void Application::saveSession() {
    if (m_restoringSession) return; // Don't save while restoring

    SessionState state;
    for (auto &editor : m_editors) {
        if (editor) {
            state.openProjectPaths.push_back(editor->getProjectFilePath());
        }
    }
    state.activeProjectIndex = m_activeEditor;
    SessionManager::instance().setSessionState(state);
    SessionManager::instance().save();
}

void Application::createNewEditor(const std::filesystem::path &gameDataFilePath, const std::filesystem::path &location,
                                  const std::string &name, bool addToRecent) {
    // Check if an editor with the same name already exists
    if (std::any_of(m_editors.begin(), m_editors.end(), [&](const auto &editor) {
        return editor->getName() == name;
    })) {
        VLOG(1) << "An editor with the name '" << name << "' already exists. Not creating a new one.";
        m_showFileAlreadyOpenPopup = true;
        return; // Do not create a new editor if the name already exists
    }
    if (location.empty() || !std::filesystem::exists(std::filesystem::path(location).parent_path())) {
        LOG(ERROR) << "Invalid location for new editor: " << location;
        NotificationManager::instance().addNotification("Invalid Project Location",
            "The specified project location is invalid or does not exist.",
            NotificationType::Error);
        return;
    }
    // Create new editor with its own graph and solver
    if (gameDataFilePath.empty()) {
        LOG(INFO) << "Creating new editor '" << name << "' with empty game data.";
        auto editor = std::make_unique<FactoryNodeEditor>(GameData(), location, name);
        m_editors.push_back(std::move(editor));
    } else {
        LOG(INFO) << "Creating new editor '" << name << "' with game data from file: " << gameDataFilePath;
        GameDataManager tempDataManager;
        std::string tempDataManagerError;
        tempDataManager.loadFromFile(gameDataFilePath, tempDataManagerError);
        if (!tempDataManagerError.empty()) {
            LOG(ERROR) << "Failed to load game data from file '" << gameDataFilePath
                       << "': " << tempDataManagerError;
            NotificationManager::instance().addNotification("Error Loading Game Data",
                "Failed to load game data from file: " + tempDataManagerError,
                NotificationType::Error);
            return;
        }

        auto editor = std::make_unique<FactoryNodeEditor>(tempDataManager.current(), location, name);
        m_editors.push_back(std::move(editor));
        LOG(INFO) << "Game data loaded successfully for editor '" << name << "'.";
        m_gameDataManager.clear();

    }

    applyThemeToAllEditors(); // Apply current theme

    // Switch to the new tab
    m_activeEditor = static_cast<int>(m_editors.size()) - 1;

    saveSession();
    if (addToRecent) {
        RecentFiles::instance().addFile(location);
    }
}

bool Application::saveActiveEditor() {
    if (m_activeEditor < 0 || static_cast<size_t>(m_activeEditor) >= m_editors.size()) {
        return false; // No active editor to save
    }

    auto &editor = m_editors[m_activeEditor];
    if (editor) {
        editor->save();
        return true;
    }
    return false;
}

bool Application::saveActiveEditorAs(const std::string &newFilePath, SaveAsMode mode) {
    if (m_activeEditor < 0 || static_cast<size_t>(m_activeEditor) >= m_editors.size()) {
        return false; // No active editor to save
    }

    auto &editor = m_editors[m_activeEditor];
    if (editor) {
        return editor->saveAs(newFilePath, mode);
    }
    return false;
}

void Application::saveAll() {
    for (auto& editor : m_editors) {
        if (editor) {
            editor->save();
        }
    }
}

void Application::copyActiveEditor() {
    if (m_activeEditor < 0 || static_cast<size_t>(m_activeEditor) >= m_editors.size()) {
        return; // No active editor to copy
    }
    auto &editor = m_editors[m_activeEditor];
    if (editor) {
        ed::SetCurrentEditor(editor->getContext());
        editor->copy(m_copyBuffer);
        ed::SetCurrentEditor(nullptr);
    }
}

void Application::cutActiveEditor() {
    if (m_activeEditor < 0 || static_cast<size_t>(m_activeEditor) >= m_editors.size()) {
        return; // No active editor to cut
    }
    auto &editor = m_editors[m_activeEditor];
    if (editor) {
        ed::SetCurrentEditor(editor->getContext());
        editor->cut(m_copyBuffer);
        ed::SetCurrentEditor(nullptr);
    }
}

void Application::pasteActiveEditor(bool mapExternalConnections) {
    if (m_activeEditor < 0 || static_cast<size_t>(m_activeEditor) >= m_editors.size()) {
        return; // No active editor to paste into
    }
    auto &editor = m_editors[m_activeEditor];
    if (editor) {
        ed::SetCurrentEditor(editor->getContext());
        editor->paste(m_copyBuffer, mapExternalConnections);
        ed::SetCurrentEditor(nullptr);
    }
}

void Application::undoActiveEditor() {
    if (m_activeEditor < 0 || static_cast<size_t>(m_activeEditor) >= m_editors.size()) {
        return; // No active editor to undo
    }
    auto &editor = m_editors[m_activeEditor];
    if (editor) {
        ed::SetCurrentEditor(editor->getContext());
        editor->undo();
        ed::SetCurrentEditor(nullptr);
    }
}

void Application::redoActiveEditor() {
    if (m_activeEditor < 0 || static_cast<size_t>(m_activeEditor) >= m_editors.size()) {
        return; // No active editor to redo
    }
    auto &editor = m_editors[m_activeEditor];
    if (editor) {
        ed::SetCurrentEditor(editor->getContext());
        editor->redo();
        ed::SetCurrentEditor(nullptr);
    }
}

void Application::selectAllActiveEditor() {
    if (m_activeEditor < 0 || static_cast<size_t>(m_activeEditor) >= m_editors.size()) {
        return; // No active editor to select all
    }
    auto &editor = m_editors[m_activeEditor];
    if (editor) {
        ed::SetCurrentEditor(editor->getContext());
        editor->selectAll();
        ed::SetCurrentEditor(nullptr);
    }
}

void Application::showFlowActiveEditor() {
    if (m_activeEditor < 0 || static_cast<size_t>(m_activeEditor) >= m_editors.size()) {
        return; // No active editor to show flow
    }
    auto &editor = m_editors[m_activeEditor];
    if (editor) {
        ed::SetCurrentEditor(editor->getContext());
        editor->showFlow();
        ed::SetCurrentEditor(nullptr);
    }
}

void Application::fitViewActiveEditor() {
    if (m_activeEditor < 0 || static_cast<size_t>(m_activeEditor) >= m_editors.size()) {
        return;
    }
    auto &editor = m_editors[m_activeEditor];
    if (editor) {
        ed::SetCurrentEditor(editor->getContext());
        editor->fitView();
        ed::SetCurrentEditor(nullptr);
    }
}


void Application::closeEditor(int index) {
    // index is signed; compare safely with size()
    if (index < 0) return;
    if (static_cast<size_t>(index) >= m_editors.size()) return;

    auto &editor = m_editors[index];
    if (editor) {
        std::filesystem::path projectPath = editor->getProjectFilePath();
        if (!projectPath.empty() && std::filesystem::exists(projectPath)) {
            m_closedEditorHistory.push_back(projectPath);
        }
    }

    m_editors.erase(m_editors.begin() + index);

    // Adjust activeEditor:
    if (m_editors.empty()) {
        m_activeEditor = -1;
    } else if (m_activeEditor >= static_cast<int>(m_editors.size())) {
        m_activeEditor = static_cast<int>(m_editors.size()) - 1;
    }

    saveSession();
}



void Application::closeEditorByName(const std::string& name) {
    auto it = std::find_if(m_editors.begin(), m_editors.end(), [&](const auto& editor) {
        return editor->getName() == name;
    });
    if (it != m_editors.end()) {
        int index = static_cast<int>(std::distance(m_editors.begin(), it));
        closeEditor(index);
    }
}

void Application::closeActiveEditor() {
    if (m_editors.empty()) return;

    if (m_activeEditor >= 0 && static_cast<size_t>(m_activeEditor) < m_editors.size()) {
        closeEditor(m_activeEditor);
    } else {
        // No focused editor, close the last tab as a sensible default
        closeEditor(static_cast<int>(m_editors.size()) - 1);
    }
}


std::string Application::generateDefaultEditorName() {
    std::set<int> usedNumbers;
    const std::string prefix = "Factory_";

    auto extractNumber = [&](const std::string& name) {
        if (name.length() > prefix.length() && name.substr(0, prefix.length()) == prefix) {
            std::string numberPart = name.substr(prefix.length());

            // Check if the rest is a valid number
            if (!numberPart.empty() && std::all_of(numberPart.begin(), numberPart.end(), ::isdigit)) {
                try {
                    int number = std::stoi(numberPart);
                    usedNumbers.insert(number);
                } catch (...) {
                    // Ignore parsing errors (e.g. out of range)
                }
            }
        }
    };

    for (const auto &editor: m_editors) {
        extractNumber(editor->getName());
    }

    try {
        const auto& defaultPath = SettingsManager::instance().getSettings().defaultProjectPath;
        if (std::filesystem::exists(defaultPath) && std::filesystem::is_directory(defaultPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(defaultPath)) {
                if (entry.is_regular_file() && entry.path().extension() == ".fpp") {
                    extractNumber(entry.path().stem().string());
                }
            }
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "Failed to scan default project directory for naming: " << e.what();
    }

    int nextNumber = 1;
    while (usedNumbers.count(nextNumber) > 0) {
        nextNumber++;
    }

    return prefix + std::to_string(nextNumber);
}