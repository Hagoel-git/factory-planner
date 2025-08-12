#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS
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

    auto showLabel = [](const char* label, ImColor color)
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
        auto size = ImGui::CalcTextSize(label);

        auto padding = ImGui::GetStyle().FramePadding;
        auto spacing = ImGui::GetStyle().ItemSpacing;

        ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(spacing.x, -spacing.y));

        auto rectMin = ImGui::GetCursorScreenPos() - padding;
        auto rectMax = ImGui::GetCursorScreenPos() + size + padding;

        auto drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(rectMin, rectMax, color, size.y * 0.15f);
        ImGui::TextUnformatted(label);
    };

    // Begin the node editor canvas
    ed::Begin("FactoryEditor");

    auto cursorTopLeft = ImGui::GetCursorScreenPos();
    // --- Draw nodes ---
    if (first_frame) {
        for (const auto &node : graph.getNodes()) {
            ed::NodeId nodeId = ToNodeId(node.id);
            ed::SetNodePosition(nodeId, ImVec2(node.id * 50 % 100000, node.id * 50 % 100000)); // Simple layout
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
            int startId = FromPinId(start);
            int endId = FromPinId(end);

            selected_port_id = startId;

            if (graph.getPort(startId)->isInput) {
                std::swap(startId, endId); // Ensure start is always output
            }

            // ask the graph if the connection is valid (it knows which is input/output)
            if (!graph.isValidConnection(startId, endId)) {
                ed::RejectNewItem(ImColor(255, 128, 128), 1.0f); // Reject with red color
            } else {
                const bool connectionExists = graph.connectionExists(startId, endId);
                const ImColor color = connectionExists ? ImColor(255,128,128) : ImColor(128,255,128);

                if (ed::AcceptNewItem(color, 1.0f)) {
                    if (connectionExists) {
                        graph.removeConnection(startId, endId);
                    } else {
                        graph.addConnection(startId, endId);
                    }
                } else {
                    if (connectionExists) {
                        showLabel("- Remove Link", ImColor(255, 128, 128)); // Show label for removing link
                    } else {
                        showLabel("+ Create Link", ImColor(32, 45, 32, 180)); // Show label for creating link
                    }
                }
            }
        }
        if (ed::QueryNewNode(&start)) {
            selected_port_id = FromPinId(start);
            showLabel("Create Node", ImColor(32, 45, 32, 180)); // Show label for creating node
            if (ed::AcceptNewItem(ImColor(255, 255, 255), 0.7f)) {
                ed::Suspend();
                ImGui::OpenPopup("Create new node");
                ed::Resume();
            }
        }

    }
    ed::EndCreate();

    auto openPopupPosition = ImGui::GetMousePos();

    ed::Suspend();
    if (ed::ShowNodeContextMenu(&m_contextNodeId)) {
        ImGui::OpenPopup("Node Context Menu");
    }
    if (ed::ShowPinContextMenu(&m_contextPinId)) {
        ImGui::OpenPopup("Pin Context Menu");
    }
    if (ed::ShowLinkContextMenu(&m_contextLinkId)) {
        ImGui::OpenPopup("Link Context Menu");
    }
    if (ed::ShowBackgroundContextMenu()) {
        ImGui::OpenPopup("Create new node");
        selected_port_id = -1;
    }
    ed::Resume();

    ed::Suspend();
    if (ImGui::BeginPopup("Create new node")) {
        auto newNodePos = ImGui::GetMousePos();
        auto resourceFilter = graph.getGameData().resources.at(graph.getPort(selected_port_id)->resource_id);
        bool fromInput = graph.getPort(selected_port_id)->isInput;
        for (const auto& recipe : graph.getGameData().recipes) {
            if (fromInput) {
                for (const auto &output : recipe.output_ports) {
                    if (output.resource_id != resourceFilter.id) continue; // Filter by resource
                    if (ImGui::Selectable(recipe.name.c_str())) {
                        int new_node_id = graph.addNode(recipe.name, NodeType::PROCESSOR, recipe.id);
                        for (int port_id : graph.getNode(new_node_id)->output_ports) {
                            if (graph.getPort(port_id)->resource_id == output.resource_id) {
                                graph.addConnection(port_id, selected_port_id); // Connect to the selected input port
                                break; // Connect only to the first matching output port
                            }
                        }
                        ImGui::CloseCurrentPopup();
                    }
                }
            } else {
                for (const auto &input : recipe.input_ports) {
                    if (input.resource_id != resourceFilter.id) continue; // Filter by resource
                    if (ImGui::Selectable(recipe.name.c_str())) {
                        int new_node_id = graph.addNode(recipe.name, NodeType::PROCESSOR, recipe.id);
                        for (int port_id : graph.getNode(new_node_id)->input_ports) {
                            if (graph.getPort(port_id)->resource_id == input.resource_id) {
                                graph.addConnection(selected_port_id, port_id); // Connect to the selected output port
                                break; // Connect only to the first matching input port
                            }
                        }
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
        }
        ImGui::EndPopup();
    }
    ed::Resume();

    if (first_frame) {
        ed::NavigateToContent(0.0f); // Fit view to content on first frame
        first_frame = false; // Reset after first frame
    }

    ed::End(); // End node editor
    ImGui::End(); // End main window

    ed::SetCurrentEditor(nullptr);
}

// End of file
