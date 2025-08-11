#pragma once
#include "FactoryNodeEditor.h"
#include "imgui.h"
#include "imgui_node_editor.h"
namespace ed = ax::NodeEditor;

#include <unordered_map>
#include <string>

static constexpr uintptr_t NODE_ID_OFFSET = 0x10000000u;
static constexpr uintptr_t PIN_ID_OFFSET = 0x20000000u;
static constexpr uintptr_t LINK_ID_OFFSET = 0x30000000u;

bool first_frame = true; // Track if this is the first frame to set initial node positions

static inline ed::NodeId ToNodeId(int id) { return ed::NodeId((uintptr_t) id + NODE_ID_OFFSET); }
static inline ed::PinId ToPinId(int id) { return ed::PinId((uintptr_t) id + PIN_ID_OFFSET); }
static inline ed::LinkId ToLinkId(int id) { return ed::LinkId((uintptr_t) id + LINK_ID_OFFSET); }
static inline int FromPinId(ed::PinId id) { return (int) (id.Get() - PIN_ID_OFFSET); }
static inline int FromNodeId(ed::NodeId id) { return (int) (id.Get() - NODE_ID_OFFSET); }
static inline int FromLinkId(ed::LinkId id) { return (int) (id.Get() - LINK_ID_OFFSET); }

FactoryNodeEditor::FactoryNodeEditor(FactoryGraph &g)
    : graph(g) {
    context = ed::CreateEditor();
    ed::SetCurrentEditor(context);


    ed::SetCurrentEditor(nullptr);
}

FactoryNodeEditor::~FactoryNodeEditor() {
    if (context) {
        ed::SetCurrentEditor(context);
        ed::DestroyEditor(context);
        context = nullptr;
        ed::SetCurrentEditor(nullptr);
    }
}

void FactoryNodeEditor::Draw() {
    if (!context) return;

    ed::SetCurrentEditor(context);

    ImGui::Begin("Factory Editor", nullptr, ImGuiWindowFlags_NoScrollbar);
    auto &io = ImGui::GetIO();
    ImGui::Text("FPS: %.2f (%.2gms)", io.Framerate, io.Framerate ? 1000.0f / io.Framerate : 0.0f);

    ImGui::Separator();

    // Top toolbar
    if (ImGui::Button("Add Node")) {
        // Simple convenience: add a default node (you may want a picker popup)
        graph.addNode("New", NodeType::PROCESSOR, 55);
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit View")) {
        ed::NavigateToContent();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        graph.clear();
    }

    // Begin the node editor canvas
    ed::Begin("FactoryEditor");

    auto cursorTopLeft = ImGui::GetCursorScreenPos();
    // --- Draw nodes ---
    if (first_frame) {
        for (const auto &node : graph.getNodes()) {
            ed::NodeId nodeId = ToNodeId(node.id);
            ed::SetNodePosition(nodeId, ImVec2(node.id * 100, node.id * 100)); // Simple layout
        }
    }
    for (const auto &node: graph.getNodes()) {
        ed::NodeId nodeId = ToNodeId(node.id);
        ed::BeginNode(nodeId);
        ImGui::Text("%s", node.name.c_str());
        ImGui::BeginGroup(); // Group inputs/outputs
        // Draw input pins
        for (int pid: node.input_ports) {
            Port *p = graph.getPort(pid);
            if (!p) continue; // Skip invalid ports
            ed::PinId pinId = ToPinId(p->id);
            ed::BeginPin(pinId, ed::PinKind::Input);
            ImGui::Text("<in> %s", graph.getGameData().resources.at(p->resource_id).name.c_str()); // Display resource name
            ed::EndPin();
        }
        ImGui::SameLine();
        // Draw output pins
        for (int pid: node.output_ports) {
            Port *p = graph.getPort(pid);
            if (!p) continue; // Skip invalid ports
            ed::PinId pinId = ToPinId(p->id);
            ed::BeginPin(pinId, ed::PinKind::Output);
            ImGui::Text("<out> %s", graph.getGameData().resources.at(p->resource_id).name.c_str()); // Display resource name
            ed::EndPin();
        }
        ImGui::EndGroup(); // End inputs/outputs group
        ed::EndNode();
    }

    // --- Draw connections ---
    for (const auto &c: graph.getConnections()) {
        // We assume Connection has fields: id, from_port, to_port
        ed::Link(ToLinkId(c.id), ToPinId(c.from_port), ToPinId(c.to_port));
    }

    // --- Handle new links being created interactively ---
    if (ed::BeginCreate()) {
        ed::PinId start, end;
        if (ed::QueryNewLink(&start, &end)) {
            const int startId = FromPinId(start);
            const int endId = FromPinId(end);

            // ask the graph if the connection is valid (it knows which is input/output)
            if (graph.isValidConnection(startId, endId)) {
                ed::AcceptNewItem();
                graph.addConnection(startId, endId);
            } else {
                ed::RejectNewItem();
            }
        }
    }
    ed::EndCreate();

    if (first_frame) {
        ed::NavigateToContent(0.0f); // Fit view to content on first frame
        first_frame = false; // Reset after first frame
    }


    ed::End(); // End node editor
    ImGui::End(); // End main window

    ed::SetCurrentEditor(nullptr);
}

// End of file
