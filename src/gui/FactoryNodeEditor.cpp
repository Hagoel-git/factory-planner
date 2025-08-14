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

FactoryNodeEditor::FactoryNodeEditor(FactoryGraph &g, FactorySolver &s)
    : graph(g), solver(s) {
    ed::Config config;
    config.SettingsFile = "FactoryEditor.json"; // Save/load settings
    config.EnableSmoothZoom = true;
    config.SmoothZoomPower = 1.2f;
    context = ed::CreateEditor(&config);
    ed::SetCurrentEditor(context);
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

    if (!ImGui::Begin("Factory Editor", nullptr, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        ed::SetCurrentEditor(nullptr);
        return;
    }

    DrawHeader();
    DrawToolbar();

    // Begin the node editor canvas
    ed::Begin("FactoryEditor");

    HandleFirstFrame();
    DrawNodes();
    DrawConnections();
    HandleUserInteractions();
    HandleKeyboardShortcuts();
    HandleContextMenus();
    HandlePopups();

    ed::End(); // End node editor
    ImGui::End(); // End main window

    ed::SetCurrentEditor(nullptr);
}

void FactoryNodeEditor::DrawHeader() {
    auto &io = ImGui::GetIO();
    ImGui::Text("FPS: %.2f (%.2gms)", io.Framerate, io.Framerate ? 1000.0f / io.Framerate : 0.0f);
    ImGui::Text("Nodes: %d, Connections: %d, Ports: %d", graph.getNodes().size(), graph.getConnections().size(), graph.getPorts().size());
    ImGui::Text("Copy Buffer Size: %d", copyBuffer.size());
    ImGui::Separator();
}

void FactoryNodeEditor::DrawToolbar() {
    if (ImGui::Button("Show Flow")) {
        for (const auto& connection : graph.getConnections()) {
            ed::Flow(ToLinkId(connection.id)); // Show flow for all connections
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Fit View")) {
        ed::NavigateToContent();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        graph.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Solve")) {
        solver.solve(graph);
    }
}

void FactoryNodeEditor::HandleFirstFrame() {
    if (first_frame) {
        // Set initial positions for nodes based on their IDs
        for (const auto &node : graph.getNodes()) {
            ed::NodeId nodeId = ToNodeId(node.id);
            ImVec2 initialPos = ImVec2((node.id % 5) * 200.0f, (node.id / 5) * 100.0f); // Simple grid layout
            ed::SetNodePosition(nodeId, initialPos);
        }
        first_frame = false; // Reset after first frame
    }
}

void FactoryNodeEditor::DrawNodes() {
    for (const auto &node: graph.getNodes()) {
        ed::NodeId nodeId = ToNodeId(node.id);
        ed::BeginNode(nodeId);
        ImGui::Text("%s", node.name.c_str());
        ImGui::BeginGroup(); // Group inputs/outputs
        int max_port_count = node.input_ports.size() > node.output_ports.size() ? node.input_ports.size() : node.output_ports.size();
        for (int i = 0; i < max_port_count; ++i) {
            if (i < node.input_ports.size()) {
                Port *p = graph.getPort(node.input_ports[i]);
                if (!p) continue; // Skip invalid ports
                ed::PinId pinId = ToPinId(p->id);
                ed::BeginPin(pinId, ed::PinKind::Input);
                ImGui::Text("<%.2f> %s",p->rate ,graph.getGameData().resources.at(p->resource_id).name.c_str()); // Display resource name
                ed::EndPin();
            } else {
                ImGui::Text(" "); // Empty space for alignment
            }
            ImGui::SameLine();
            if (i < node.output_ports.size()) {
                Port *p = graph.getPort(node.output_ports[i]);
                if (!p) continue; // Skip invalid ports
                ed::PinId pinId = ToPinId(p->id);
                ed::BeginPin(pinId, ed::PinKind::Output);
                ImGui::Text("%s <%.2f>",p->rate, graph.getGameData().resources.at(p->resource_id).name.c_str()); // Display resource name
                ed::EndPin();
            } else {
                ImGui::Text(" "); // Empty space for alignment
            }
        }
        ImGui::EndGroup(); // End inputs/outputs group
        ed::EndNode();
    }
}

void FactoryNodeEditor::DrawConnections() {
    for (const auto &c: graph.getConnections()) {
        ed::Link(ToLinkId(c.id), ToPinId(c.from_port), ToPinId(c.to_port));
    }
}

void FactoryNodeEditor::HandleUserInteractions() {
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
                        showLabel("- Remove Link", ImColor(32, 45, 32, 180)); // Show label for removing link
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
                m_storedPopupPosition = ImGui::GetMousePosOnOpeningCurrentPopup();
            }
        }

    }
    ed::EndCreate();

    if (ed::BeginDelete()) {
        // Collect all deletions first
        std::vector<int> nodesToDelete;
        std::vector<int> linksToDelete;

        ed::NodeId nodeId = 0;
        while (ed::QueryDeletedNode(&nodeId)) {
            if (ed::AcceptDeletedItem()) {
                nodesToDelete.push_back(FromNodeId(nodeId));
            }
        }

        ed::LinkId linkId = 0;
        while (ed::QueryDeletedLink(&linkId)) {
            if (ed::AcceptDeletedItem()) {
                linksToDelete.push_back(FromLinkId(linkId));
            }
        }

        // Process deletions: links first, then nodes
        // This ensures we don't try to delete already-removed connections
        for (int id : linksToDelete) {
            auto connection = graph.getConnection(id);
            if (connection != nullptr) {
                int fromPort = connection->from_port;
                int toPort = connection->to_port;
                graph.removeConnection(fromPort, toPort);
            }
        }

        for (int id : nodesToDelete) {
            graph.removeNode(id);
        }
        std::cout << std::endl;
    }
    ed::EndDelete();
}

void FactoryNodeEditor::HandleKeyboardShortcuts() {
    ed::Suspend();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow)) {
        if (ImGui::GetIO().KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_A)) {
                for (const auto &node : graph.getNodes()) {
                    ed::SelectNode(ToNodeId(node.id), true); // Select all nodes
                }
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_C)) {
                copyBuffer.clear();
                for (const auto &node : graph.getNodes()) {
                    if (ed::IsNodeSelected(ToNodeId(node.id))) {
                        copyBuffer.push_back(node.id); // Copy selected nodes to buffer
                    }
                }
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_V)) {
                bool mapExternalConnections = ImGui::GetIO().KeyShift; // Shift key to map external connections
                ed::ClearSelection();
                if (copyBuffer.empty()) return;
                // Calculate offset from original positions to mouse position
                ImVec2 mousePos = ed::ScreenToCanvas(ImGui::GetMousePos());
                ImVec2 originalCenter = ImVec2(0, 0);

                // Calculate center of original nodes
                for (int nodeId : copyBuffer) {
                    auto node = graph.getNode(nodeId);
                    if (node) {
                        ImVec2 nodePos = ed::GetNodePosition(ToNodeId(nodeId));
                        originalCenter.x += nodePos.x;
                        originalCenter.y += nodePos.y;
                    }
                }
                originalCenter.x /= copyBuffer.size();
                originalCenter.y /= copyBuffer.size();

                ImVec2 offset = ImVec2(mousePos.x - originalCenter.x, mousePos.y - originalCenter.y);

                // Map old node IDs to new node IDs
                std::unordered_map<int, int> nodeIdMap;

                for (int oldNodeId : copyBuffer) {
                    auto node = graph.getNode(oldNodeId);
                    if (node) {
                        int newNodeId = graph.addNode(node->name, node->type, node->selected_recipe_id);

                        // Position node relative to mouse with original offset
                        ImVec2 oldPos = ed::GetNodePosition(ToNodeId(oldNodeId));
                        ImVec2 newPos = ImVec2(oldPos.x + offset.x, oldPos.y + offset.y);
                        ed::SetNodePosition(ToNodeId(newNodeId), newPos);

                        ed::SelectNode(ToNodeId(newNodeId), true);
                        nodeIdMap[oldNodeId] = newNodeId;
                    }
                }

                std::unordered_map<int, int> oldToNewPortMap;
                for (const auto& pair : nodeIdMap) {
                    int oldNodeId = pair.first;
                    int newNodeId = pair.second;

                    auto oldNode = graph.getNode(oldNodeId);
                    auto newNode = graph.getNode(newNodeId);

                    if (oldNode && newNode) {
                        // Map all input ports
                        for (size_t i = 0; i < oldNode->input_ports.size(); ++i) {
                            oldToNewPortMap[oldNode->input_ports[i]] = newNode->input_ports[i];
                            graph.getPort(newNode->input_ports[i])->user_constraint = graph.getPort(oldNode->input_ports[i])->user_constraint; // Copy constraints
                        }
                        // Map all output ports
                        for (size_t i = 0; i < oldNode->output_ports.size(); ++i) {
                            oldToNewPortMap[oldNode->output_ports[i]] = newNode->output_ports[i];
                            graph.getPort(newNode->output_ports[i])->user_constraint = graph.getPort(oldNode->output_ports[i])->user_constraint; // Copy constraints
                        }
                    }
                }

                // Iterate through all original connections to recreate all relevant links.
                for (const auto& conn : graph.getConnections()) {
                    auto itFrom = oldToNewPortMap.find(conn.from_port);
                    auto itTo = oldToNewPortMap.find(conn.to_port);

                    bool fromIsCopied = (itFrom != oldToNewPortMap.end());
                    bool toIsCopied = (itTo != oldToNewPortMap.end());

                    // Case 1: Internal connection (copied -> copied)
                    // Both the source and destination nodes were part of the selection.
                    if (fromIsCopied && toIsCopied) {
                        graph.addConnection(itFrom->second, itTo->second);
                    }
                    // Case 2: Outgoing connection (copied -> non-copied)
                    // The source node was copied, but it connects to an existing, external node.
                    if (mapExternalConnections) {
                        if (fromIsCopied && !toIsCopied) {
                            int newFromPort = itFrom->second;
                            int originalToPort = conn.to_port;
                            graph.addConnection(newFromPort, originalToPort);
                        }
                        // Case 3: Incoming connection (non-copied -> copied)
                        // An existing, external node connects to a node that was just pasted.
                        else if (!fromIsCopied && toIsCopied) {
                            int originalFromPort = conn.from_port;
                            int newToPort = itTo->second;
                            graph.addConnection(originalFromPort, newToPort);
                        }
                        // Case 4 (else): The connection is between two non-copied nodes, so we do nothing.
                    }
                }
            }
        }
    }
    ed::Resume();
}

void FactoryNodeEditor::HandleContextMenus() {
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
    ed::Resume();

    if (ed::ShowBackgroundContextMenu()) {
        ed::Suspend();
        ImGui::OpenPopup("Create new node");
        selected_port_id = -1;
        ed::Resume();
        m_storedPopupPosition = ImGui::GetMousePosOnOpeningCurrentPopup();
    }
}

void FactoryNodeEditor::HandlePopups() {

    ed::Suspend();
    if (ImGui::BeginPopup("Node Context Menu")) {
        auto node = graph.getNode(FromNodeId(m_contextNodeId));
        if (node) {
            ImGui::Text("Node ID: %d", node->id);
            ImGui::Text("Name: %s", node->name.c_str());
            ImGui::Text("Type: %s", toString(node->type));
            ImGui::Text("Machine ID: %d", node->machine_id);
            ImGui::Text("Selected Recipe ID: %d", node->selected_recipe_id);
            ImGui::Text("Power usage: %.2f MW", node->power_usage);
            ImGui::Text("Machine count: %.2f", node->machine_count);
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Node")) {
                graph.removeNode(node->id);
            }
        } else {
            ImGui::Text("Unknown node");
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Pin Context Menu")) {
        auto port = graph.getPort(FromPinId(m_contextPinId));
        if (port) {
            ImGui::Text("Port ID: %d", port->id);
            ImGui::Text("Resource ID: %d", port->resource_id);
            ImGui::Text("Is Input: %s", port->isInput ? "Yes" : "No");
            ImGui::Text("Current rate: %.2f", port->rate);
            ImGui::Text("Limit: %.2f", port->user_constraint);
            ImGui::Separator();
            static double new_constraint = 60;
            ImGui::InputDouble("Rate", &new_constraint, 0.1f, 1.0f, "%.2f");
            if (ImGui::MenuItem("Set Constraint")) {
                graph.setPortDemand(port->id, new_constraint);
            }
        } else {
            ImGui::Text("Unknown port");
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Create new node")) {
        if (selected_port_id != -1) {
            auto resourceFilter = graph.getGameData().resources.at(graph.getPort(selected_port_id)->resource_id);
            bool fromInput = graph.getPort(selected_port_id)->isInput;
            for (const auto& recipe : graph.getGameData().recipes) {
                const auto& ports = fromInput ? recipe.output_ports : recipe.input_ports;
                for (const auto& port : ports) {
                    if (port.resource_id == resourceFilter.id) {
                        if (ImGui::Selectable(recipe.name.c_str())) {
                            int new_node_id = graph.addNode(recipe.name, NodeType::PROCESSOR, recipe.id);
                            ed::SetNodePosition(ToNodeId(new_node_id), ed::ScreenToCanvas(m_storedPopupPosition));
                            // Find the corresponding port on the newly created node to connect to
                            const auto& ports_on_new_node = fromInput ? graph.getNode(new_node_id)->output_ports : graph.getNode(new_node_id)->input_ports;
                            for (int new_port_id : ports_on_new_node) {
                                if (graph.getPort(new_port_id)->resource_id == resourceFilter.id) {
                                    // Determine connection direction dynamically
                                    int source_id = fromInput ? new_port_id : selected_port_id;
                                    int target_id = fromInput ? selected_port_id : new_port_id;
                                    graph.addConnection(source_id, target_id);
                                    break; // Connect to the first available port and stop searching
                                }
                            }
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
            }
        } else {
            for (const auto& recipe : graph.getGameData().recipes) {
                if (ImGui::Selectable(recipe.name.c_str())) {
                    int new_node_id = graph.addNode(recipe.name, NodeType::PROCESSOR, recipe.id);
                    ed::SetNodePosition(ToNodeId(new_node_id), m_storedPopupPosition);
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }
    ed::Resume();
}
