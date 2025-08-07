//
// Created by hagoel on 8/6/25.
//

#include "FactoryNodeEditor.h"

bool FactoryNodeEditor::Initialize() {
    ed::Config config;
    config.SettingsFile = "FactoryPlanner.json"; // Save editor state
    m_ctx = CreateEditor(&config);
    if (!m_ctx) {
        std::cerr << "Failed to create node editor." << std::endl;
        return false; // Editor context creation failed
    }
    return true;
}

void FactoryNodeEditor::Shutdown() {
    if (m_ctx) {
        ed::DestroyEditor(m_ctx);
        m_ctx = nullptr;
    }
}

void FactoryNodeEditor::SetGraph(FactoryGraph *graph) {
    m_factoryGraph = graph;
}

void FactoryNodeEditor::SetSolver(FactorySolver *solver) {
    m_factorySolver = solver;
}

void FactoryNodeEditor::Show() {
    ShowEditor();
}

FactoryNodeEditor::FactoryNodeEditor() {
}

FactoryNodeEditor::~FactoryNodeEditor() {
    Shutdown();
}

void FactoryNodeEditor::ShowEditor() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags window_flags = 0;
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
    window_flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    window_flags |= ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    bool open = true;
    ImGui::Begin("Node Editor", &open, window_flags);
    ImGui::PopStyleVar(3);

    ed::SetCurrentEditor(m_ctx);
    ed::Begin("Factory Node Editor", ImVec2(0.0f, 0.0f));

    RenderNodes();
    RenderConnections();
    HandleInteractions();

    ed::End();
    ed::SetCurrentEditor(m_ctx);

    ImGui::End();
}

void FactoryNodeEditor::RenderNodes() {
}

void FactoryNodeEditor::RenderConnections() {
}

void FactoryNodeEditor::HandleInteractions() {
}

void FactoryNodeEditor::HandleDoubleClick() {
}

void FactoryNodeEditor::HandleConnectionDrag() {
}

void FactoryNodeEditor::HandleNodeCreation(ImVec2 pos) {
}

void FactoryNodeEditor::HandleNodeDeletion() {
}
