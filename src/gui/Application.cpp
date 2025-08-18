#include "Application.h"
#include <algorithm>
#include <fstream>
#include <imgui_internal.h>

Application::Application() {
}

Application::~Application() = default;

void Application::Draw() {
    DrawMenuBar();
    DrawNewProjectDialog();
    DrawOpenProjectDialog();
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
        dockInitialized = true;
    }

    activeEditor = -1; // reset each frame; will set to index of focused one
    for (int i = 0; i < static_cast<int>(editors.size()); ++i) {
        auto &editor = editors[i];
        if (editor) {
            // Give each window a unique ID if you have duplicate names
            ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(640, 480));
            ImGui::Begin(editor->GetName().c_str());
            ImGui::PopStyleVar();
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                activeEditor = i;
            }
            editor->Draw();
            ImGui::End();
        }
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
            if (ImGui::MenuItem("Open Recent (WIP)")) {
                // todo: Implement recent files functionality
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                SaveActiveEditor();
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S (WIP)")) {

            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close Active", "Ctrl+W") && !editors.empty()) {
                CloseActiveEditor();
            }
            if (ImGui::MenuItem("Close All")) {
                editors.clear();
                activeEditor = -1; // Reset active editor
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Ctrl+Q (WIP)")) {
                editors.clear();
                // todo: Implement application quit functionality
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    // Handle keyboard shortcuts
    ImGuiIO &io = ImGui::GetIO();
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
            SaveActiveEditor();
        }
    }
}

void Application::DrawNewProjectDialog() {
    if (!showNewProjectDialog) return;

    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("New Project", &showNewProjectDialog, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
        std::string gameDataPath = "game_data"; // Default path for game data files
        std::vector<std::string> gameDataFiles = GetGameDataFiles(gameDataPath);
        static char projectName[128];
        static char location[256] = "projects/"; // Default path
        ImGui::Text("Create a new project:");
        if (projectName[0] == '\0') {
            // Initialize with a default name if empty
            std::string defaultName = GenerateDefaultEditorName();
            std::strncpy(projectName, defaultName.c_str(), sizeof(projectName));
            projectName[sizeof(projectName) - 1] = '\0'; // Ensure null-termination
        }
        ImGui::InputText("Project Name", projectName, sizeof(projectName));
        ImGui::InputText("Location (Relative)", location, sizeof(projectName));
        std::string fullPath = std::string(location) + projectName + ".json";
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Full project path: %s", fullPath.c_str());
        ImGui::Text("Select Game Configuration:");

        static int selectedGameDataFile = gameDataFiles.empty() ? -1 : 0; // Default to first file if available

        if (ImGui::BeginChild("GameSelection", ImVec2(0, 150), true)) {
            for (int i = 0; i < static_cast<int>(gameDataFiles.size()); ++i) {
                bool isSelected = (selectedGameDataFile == i);
                if (ImGui::Selectable(gameDataFiles[i].c_str(), isSelected)) {
                    selectedGameDataFile = i;
                }
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();

        bool nameExists = std::any_of(editors.begin(), editors.end(), [&](const auto &editor) {
            return editor->GetName() == projectName;
        });
        bool projectExists = std::filesystem::exists(
            std::filesystem::path(location) / (std::string(projectName) + ".json"));

        ImGui::BeginDisabled(nameExists || projectExists || selectedGameDataFile == -1);
        // Disable button if name exists or project already exists
        if (ImGui::Button("Create")) {
            if (!nameExists) {
                CreateNewEditor(gameDataPath + "/" + gameDataFiles.at(selectedGameDataFile), fullPath, projectName);
                showNewProjectDialog = false;
            }
            if (!projectExists) {
                // create the project directory and file if it doesn't exist
                std::filesystem::create_directories(std::filesystem::path(location));
                std::ofstream projectFile(fullPath);
                if (projectFile) {
                    projectFile << "{}"; // Create an empty JSON file
                    projectFile.close();
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            showNewProjectDialog = false; // Close dialog without action
        }
        if (nameExists) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "An editor with this name already exists.");
        }
        if (projectExists) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1, 0, 0, 1),
                               "A project with this name already exists at the specified location.");
        }
        if (selectedGameDataFile == -1) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Please select a game configuration file.");
        }
        ImGui::End();
    }
}

void Application::DrawOpenProjectDialog() {
    if (!showOpenProjectDialog) return;
    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Open Project", &showOpenProjectDialog, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
        std::string gameDataPath = "game_data"; // Default path for game data files
        std::vector<std::string> gameDataFiles = GetGameDataFiles(gameDataPath);
        static char location[256] = "projects/"; // Default path
        ImGui::Text("Open an existing project:");
        ImGui::InputText("Location (Relative)", location, sizeof(location));
        if (ImGui::Button("Open")) {
            // Check if the project file exists
            std::string projectFilePath = std::string(location);
            if (std::filesystem::exists(projectFilePath)) {
                // name from location
                std::string projectName = std::filesystem::path(projectFilePath).stem().string();
                // Load the project
                CreateNewEditor(gameDataPath + "/" + gameDataFiles.at(0), location, projectName);
                showOpenProjectDialog = false; // Close dialog after opening
            } else {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Project file does not exist at the specified location.");
            }
        }
        ImGui::End();
    }
}

void Application::CreateNewEditor(const std::string &gameDataFilePath, const std::string &location,
                                  const std::string &name) {
    std::string projectName = name.empty() ? GenerateDefaultEditorName() : name;

    // Create new editor with its own graph and solver
    auto editor = std::make_unique<FactoryNodeEditor>(gameDataFilePath, location, projectName);
    editors.push_back(std::move(editor));

    // Switch to the new tab
    activeEditor = static_cast<int>(editors.size()) - 1;
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

std::vector<std::string> Application::GetGameDataFiles(const std::string &directory) {
    if (!std::filesystem::exists(directory)) {
        std::filesystem::create_directories(directory);
    }
    std::vector<std::string> jsonFiles;
    for (const auto &entry: std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            jsonFiles.push_back(entry.path().filename().string());
        }
    }
    return jsonFiles;
}
