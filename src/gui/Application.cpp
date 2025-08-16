#include "Application.h"
#include <algorithm>
#include <imgui_internal.h>

Application::Application() {
    // Create the first tab
    CreateNewEditor("Factory 1");
}

Application::~Application() = default;

void Application::Draw() {
    DrawMenuBar();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

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
        auto& editor = editors[i];
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
            if (ImGui::MenuItem("New Factory", "Ctrl+T")) {
                CreateNewEditor();
            }

            if (ImGui::MenuItem("Close Active Factory", "Ctrl+W") && !editors.empty()) {
                CloseActiveEditor();
            }

            ImGui::Separator();

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    // Handle keyboard shortcuts
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_T)) {
            CreateNewEditor();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_W) && !editors.empty()) {
            CloseActiveEditor();
        }
    }
}

void Application::CreateNewEditor(const std::string& name) {
    std::string tabName = name.empty() ? GenerateDefaultEditorName() : name;

    // Create new editor with its own graph and solver
    auto editor = std::make_unique<FactoryNodeEditor>("../../data/satisfactory.json", tabName);
    editors.push_back(std::move(editor));

    // Switch to the new tab
    activeEditor = static_cast<int>(editors.size()) - 1;
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

void Application::RenameEditor(int index, const std::string& newName) {
    if (index >= 0 && index < editors.size()) {
        editors[index]->SetName(newName);
    }
}

std::string Application::GenerateDefaultEditorName() {
    std::set<int> usedNumbers;

    // Extract all numbers from existing factory names
    for (const auto& editor : editors) {
        const std::string& name = editor->GetName();

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

    return "Factory " + std::to_string(nextNumber);
}