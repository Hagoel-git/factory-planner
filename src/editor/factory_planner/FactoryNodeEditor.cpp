#define IMGUI_DEFINE_MATH_OPERATORS
#include "FactoryNodeEditor.h"
#include "imgui.h"
#include "imgui_node_editor.h"
#include "imgui_internal.h"
#include "ProjectIo.h"
#include "IdUtils.h"
#include "CopyBuffer.h"

#include <unordered_map>
#include <string>
#include <utility>

#include "imgui_node_editor_internal.h"
#include "SettingsManager.h"
#include "TextureUtils.h"
namespace ed = ax::NodeEditor;

FactoryNodeEditor::FactoryNodeEditor(const GameData& game_data, const std::string &projectFilePath, std::string title)
    : name(std::move(title)), projectFilePath(projectFilePath), m_contextNodeId(0), m_contextPinId(0),
      m_contextLinkId(0), undoRedoManager(SettingsManager::instance().getSettings().maxUndoHistory) {
    try {
        graph = std::make_unique<FactoryGraph>(game_data);
        solver = std::make_unique<FactorySolver>();

        nextAutosaveTime = std::chrono::steady_clock::now() +
                           std::chrono::minutes(SettingsManager::instance().getSettings().autoSaveIntervalMinutes);

        ed::Config cfg = ed::Config();
        cfg.AutoSaveEnabled = false;
        cfg.SettingsFile = nullptr;
        cfg.SaveSettings = nullptr;
        cfg.LoadSettings = nullptr;
        cfg.SaveNodeSettings = nullptr;
        cfg.LoadNodeSettings = nullptr;
        cfg.UserPointer = nullptr;
        context = ed::CreateEditor(&cfg);
        ed::SetCurrentEditor(context);

        auto& ed_style = ed::GetStyle();
        ed_style.NodePadding = ImVec4(2,6,2,6);
        ed_style.NodeRounding = 2.0f;
        ed_style.PinRounding = 0.0f;
        ed_style.NodeBorderWidth = 1.0f;
        ed_style.HoveredNodeBorderWidth = 1.5f;
        ed_style.HoverNodeBorderOffset = ed_style.NodeBorderWidth;
        ed_style.SelectedNodeBorderWidth = 1.5f;
        ed_style.SelectedNodeBorderOffset = ed_style.NodeBorderWidth;
        ed_style.PinRadius = 0.0f;
        ed_style.PivotAlignment = ImVec2(0.5f, 0.5f);

        ed_style.FlowDuration = 6.0f;

        ProjectIO::LoadProject(projectFilePath, *graph);

        // Initialize quadtree with large world bounds to handle extreme zoom levels
        float worldSize = 262144.0f; // 2^18, very large world
        float halfWorldSize = worldSize * 0.5f;

        quadtree::Box<float> worldBounds(
            quadtree::Vector2<float>(-halfWorldSize, -halfWorldSize),
            quadtree::Vector2<float>(worldSize, worldSize)
        );
        nodeQuadtree = std::make_unique<quadtree::Quadtree<NodeQuadtreeData, GetNodeBox> >(worldBounds);

        FactorySolver::SolverResult result = solver->solve(*graph);
        debugInfo.lastTotalSolveDurationMs = result.total_solve_time_ms;
        debugInfo.lastSetupSolveDurationMs = result.setup_time_ms;
        debugInfo.lastSolverDurationMs = result.solve_time_ms;
        debugInfo.lastUpdateFactoryDurationMs = result.update_factory_time_ms;
        debugInfo.lastSolverResult = result.status;

        quadtreeNeedsRebuild = true;
    } catch (const std::exception &e) {
        std::cerr << "Error initializing FactoryNodeEditor: " << e.what() << std::endl;
    }
}

FactoryNodeEditor::~FactoryNodeEditor() {
    // Clear graph and solver
    if (graph) graph->clear();
    solver = nullptr;

    // Destroy node editor context
    if (context) {
        ed::SetCurrentEditor(context);
        ed::DestroyEditor(context);
        context = nullptr;
        ed::SetCurrentEditor(nullptr);
    }

    // Reset other state
    nodeQuadtree = nullptr;
    selected_port_id = -1;
    m_contextNodeId = 0;
    m_contextPinId = 0;
    m_contextLinkId = 0;
    quadtreeNeedsRebuild = false;
}

void FactoryNodeEditor::Draw() {
    if (!context) return;

    if (std::chrono::steady_clock::now() > nextAutosaveTime && SettingsManager::instance().getSettings().autoSaveEnabled) {
        Save();
        std::cout << "Auto-saved project: " << projectFilePath << std::endl;
        nextAutosaveTime = std::chrono::steady_clock::now() +
                           std::chrono::minutes(SettingsManager::instance().getSettings().autoSaveIntervalMinutes);
    }

    ed::SetCurrentEditor(context);

    UpdateDebugInfo();
    DrawToolbar();

    // Begin the node editor canvas
    windowPos = ImGui::GetWindowPos();
    windowSize = ImGui::GetWindowSize();
    ed::Begin(name.c_str());

    // Rebuild quadtree if needed
    if (quadtreeNeedsRebuild) {
        RebuildQuadtree();
        quadtreeNeedsRebuild = false;
    }

    DrawNodes();
    DrawConnections();
    HandleUserInteractions();
    HandleContextMenus();
    HandlePopups();

    ed::End(); // End node editor

    ed::SetCurrentEditor(nullptr);
}

bool FactoryNodeEditor::Save() {
    if (ProjectIO::SaveProject(projectFilePath, *graph, this->context)) {
        return true;
    }
    return false;
}

bool FactoryNodeEditor::SaveAs(const std::string &newFilePath, SaveAsMode mode) {
    if (ProjectIO::SaveProject(newFilePath, *graph, this->context)) {
        if (mode == SaveAsMode::SwitchToNewFile) {
            projectFilePath = newFilePath;
            name = std::filesystem::path(newFilePath).stem().string();
        }
        return true;
    }
    return false;

}

void FactoryNodeEditor::copy(CopyBuffer &copy_buffer) {
    std::vector<ed::NodeId> selectedNodes;
    selectedNodes.resize(ed::GetSelectedObjectCount());
    int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));
    selectedNodes.resize(nodeCount);

    if (nodeCount == 0) return;

    copy_buffer.clear();

    copy_buffer.gameDataFilePath = GetGameDataFilePath(); // Store game data file path

    for (const auto &nodeId : selectedNodes) {
        int nodeIdInt = IdUtils::FromNodeId(nodeId);
        auto node = graph->getNode(nodeIdInt);
        if (!node) continue; // Skip invalid nodes

        copy_buffer.nodes[nodeIdInt] = *node; // Copy node
        copy_buffer.nodePositions[nodeIdInt] = ed::GetNodePosition(nodeId); // Store position

        for (const auto &portId : node->input_ports) {
            auto port = graph->getPort(portId);
            if (port) {
                copy_buffer.ports[portId] = *port; // Copy input port
                for (const auto &conn : graph->getConnectionsForPort(portId)) {
                        copy_buffer.connections.insert(*conn); // Copy incoming connection

                }
            }
        }
        for (const auto &portId : node->output_ports) {
            auto port = graph->getPort(portId);
            if (port) {
                copy_buffer.ports[portId] = *port; // Copy output port
                for (const auto &conn : graph->getConnectionsForPort(portId)) {
                        copy_buffer.connections.insert(*conn); // Copy incoming connection

                }
            }
        }
    }
}

void FactoryNodeEditor::cut(CopyBuffer &copyBuffer) {
    copy(copyBuffer);

    if (copyBuffer.isEmpty()) return;

    auto cmd = std::make_unique<CompositeCommand>("Cut Nodes");

    for (const auto &pair : copyBuffer.nodes) {
        int nodeId = pair.first;
        cmd->addCommand(std::make_unique<RemoveNodeCommand>(nodeId));
    }
    executeCommand(std::move(cmd));
}

void FactoryNodeEditor::paste(const CopyBuffer &copy_buffer, bool mapExternalConnections) {
    executeCommand(std::make_unique<PasteCommand>(copy_buffer, mapExternalConnections));
}

void FactoryNodeEditor::selectAll() {
    ed::ClearSelection();
    for (const auto &node : graph->getNodes()) {
        ed::SelectNode(IdUtils::ToNodeId(node.id), true); // Select all nodes
    }
}

void FactoryNodeEditor::UpdateDebugInfo() {
    debugInfo.filePath = projectFilePath;
    debugInfo.gameDataPath = graph->getGameData().gameDataFilePath;
    debugInfo.totalNodes = static_cast<int>(graph->getNodes().size());
    debugInfo.totalConnections = static_cast<int>(graph->getConnections().size());
    debugInfo.totalPorts = static_cast<int>(graph->getPorts().size());
    debugInfo.selectionSize = ed::GetSelectedObjectCount();
    if (nodeQuadtree) {
        auto bounds = nodeQuadtree->getBox();
        debugInfo.quadtreeBoundsMin[0] = bounds.getTopLeft().x;
        debugInfo.quadtreeBoundsMin[1] = bounds.getTopLeft().y;
        debugInfo.quadtreeBoundsMax[0] = bounds.getTopLeft().x + bounds.getSize().x;
        debugInfo.quadtreeBoundsMax[1] = bounds.getTopLeft().y + bounds.getSize().y;

        ImVec2 canvasMin = ed::ScreenToCanvas(windowPos);
        ImVec2 canvasMax = ed::ScreenToCanvas(windowPos + windowSize);

        debugInfo.viewBoundsMin[0] = canvasMin.x;
        debugInfo.viewBoundsMin[1] = canvasMin.y;
        debugInfo.viewBoundsMax[0] = canvasMax.x;
        debugInfo.viewBoundsMax[1] = canvasMax.y;
    }

    debugInfo.undoStackSize = undoRedoManager.getUndoStackSize();
    debugInfo.redoStackSize = undoRedoManager.getRedoStackSize();
}

void FactoryNodeEditor::DrawToolbar() {
    if (ImGui::Button("Show Flow")) {
        for (const auto& connection : graph->getConnections()) {
            ed::Flow(IdUtils::ToLinkId(connection.id)); // Show flow for all connections
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit View")) {
        ed::NavigateToContent();
    }
}

void FactoryNodeEditor::RebuildQuadtree() {
    // Use very large world bounds to handle any zoom level
    // This ensures the quadtree can handle extreme zoom levels
    float worldSize = 262144.0f; // 2^18, very large world
    float halfWorldSize = worldSize * 0.5f;

    quadtree::Box<float> worldBounds(
        quadtree::Vector2<float>(-halfWorldSize, -halfWorldSize),
        quadtree::Vector2<float>(worldSize, worldSize)
    );
    nodeQuadtree = std::make_unique<quadtree::Quadtree<NodeQuadtreeData, GetNodeBox>>(worldBounds);

    // Add all nodes to quadtree
    for (const auto &node : graph->getNodes()) {
        ed::NodeId nodeId = IdUtils::ToNodeId(node.id);
        ImVec2 nodePos = ed::GetNodePosition(nodeId);
        ImVec2 nodeSize = ed::GetNodeSize(nodeId);
        if (nodeSize.x <= 0 || nodeSize.y <= 0) {
            nodeSize = ImVec2(300.0f, 100.0f); // Default size if not provided
        }
        nodeQuadtree->add(NodeQuadtreeData(node.id, nodePos, nodeSize));
    }
}

std::vector<NodeQuadtreeData> FactoryNodeEditor::GetVisibleNodes(const ImVec2& viewMin, const ImVec2& viewMax) {
    if (!nodeQuadtree) {
        return {};
    }

    // Add some padding to the view to catch nodes that are partially visible
    float padding = 100.0;

    quadtree::Box<float> viewBox(
        quadtree::Vector2<float>(viewMin.x - padding, viewMin.y - padding),
        quadtree::Vector2<float>((viewMax.x - viewMin.x) + 2.0f * padding, (viewMax.y - viewMin.y) + 2.0f * padding)
    );

    // Check if view box intersects with quadtree bounds before querying
    auto quadtreeBounds = nodeQuadtree->getBox();
    if (!viewBox.intersects(quadtreeBounds)) {
        return {}; // No intersection, return empty result
    }

    return nodeQuadtree->query(viewBox);
}

void FactoryNodeEditor::DrawNodes() {
    // Get the visible screen area and convert it to canvas coordinates

    ImVec2 viewMin = windowPos;
    ImVec2 viewMax = viewMin + windowSize;
    ImVec2 canvasMin = ed::ScreenToCanvas(viewMin);
    ImVec2 canvasMax = ed::ScreenToCanvas(viewMax);

    // Get visible nodes from quadtree
    auto visibleNodes = GetVisibleNodes(canvasMin, canvasMax);

    // Build set of visible node ids
    std::unordered_set<int> visibleNodeIds;
    visibleNodeIds.reserve(visibleNodes.size());
    for (const auto &nd : visibleNodes) visibleNodeIds.insert(nd.nodeId);

    std::unordered_set<int> nodesToRegister = visibleNodeIds;

    // Iterate only through the visible nodes
    for (int nodeId : visibleNodeIds) {
        auto node = graph->getNode(nodeId);
        if (!node) continue;

        // Check input ports
        for (int portId : node->input_ports) {
            for (const auto& conn : graph->getConnectionsForPort(portId)) {
                // Check if the connection is incoming or outgoing to determine the remote node
                if (conn->to_port == portId) {
                    auto fromPort = graph->getPort(conn->from_port);
                    if (fromPort) {
                        nodesToRegister.insert(fromPort->node_id);
                    } else {
                        std::cout << "Warning: Input port " << portId << " of node " << nodeId << " has no valid connection." << std::endl;
                    }
                }
            }
        }

        // Check output ports
        for (int portId : node->output_ports) {
            for (const auto& conn : graph->getConnectionsForPort(portId)) {
                // Check if the connection is incoming or outgoing to determine the remote node
                if (conn->from_port == portId) {
                    auto toPort = graph->getPort(conn->to_port);
                    if (toPort) {
                        nodesToRegister.insert(toPort->node_id);
                    } else {
                        std::cout << "Warning: Output port " << portId << " of node " << nodeId << " has no valid connection." << std::endl;
                    }
                }
            }
        }
    }

    debugInfo.visibleNodes = static_cast<int>(nodesToRegister.size());
    // Draw only visible nodes
    for (const auto& nodeData : nodesToRegister) {
        auto node = graph->getNode(nodeData);
        if (!node) continue;

        ed::NodeId nodeId = IdUtils::ToNodeId(node->id);
        ed::BeginNode(nodeId);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2,0));

        // --- PRE-CALC: count visible ports and heights ----
        ImGuiStyle &style = ImGui::GetStyle();
        float font_h = ImGui::GetFontSize();                // text height
        float port_img_h = 24.0f;                          // the image height for pins
        float port_line_h = std::max(port_img_h, font_h);  // effective per-pin height
        float spacing_y = style.ItemSpacing.y;             // vertical spacing between items

        // Count only visible ports (skip resource_key == "nothing")
        int visible_inputs = 0, visible_outputs = 0;
        for (int portId : node->input_ports) {
            Port *p = graph->getPort(portId);
            if (p && p->resource_key != "nothing") ++visible_inputs;
        }
        for (int portId : node->output_ports) {
            Port *p = graph->getPort(portId);
            if (p && p->resource_key != "nothing") ++visible_outputs;
        }

        // Heights of each column
        float inputs_h = (visible_inputs > 0) ? (visible_inputs * port_line_h + (visible_inputs - 1) * spacing_y) : 0.0f;
        float outputs_h = (visible_outputs > 0) ? (visible_outputs * port_line_h + (visible_outputs - 1) * spacing_y) : 0.0f;
        float machine_h = 48.0f; // the machine image height
        // Node height = tallest column
        float node_h = std::max(machine_h, std::max(inputs_h, outputs_h));

        // Offsets to vertically center each column within node_h
        float inputs_offset = (node_h - inputs_h) * 0.5f;
        float machine_offset = (node_h - machine_h) * 0.5f;
        float outputs_offset = (node_h - outputs_h) * 0.5f;

        // --- LEFT (inputs) ---
        ImGui::BeginGroup();
        // add vertical offset to center inputs column
        if (inputs_offset > 0.0f) ImGui::Dummy(ImVec2(0, inputs_offset));

        if (visible_inputs > 0) {
            ImGui::BeginGroup();
            for (int port : node->input_ports) {
                Port *p = graph->getPort(port);
                if (!p || p->resource_key == "nothing") continue;

                Resource res = graph->getGameData().resources.at(p->resource_key);
                ed::BeginPin(IdUtils::ToPinId(p->id), ed::PinKind::Input);
                ImGui::Image(res.texture, ImVec2(24,24)); ImGui::SameLine();
                ImGui::Text("%.2f", p->rate);
                if (SettingsManager::instance().getSettings().showResourceNames) {
                    ImGui::Text("%s", res.name.c_str());
                }
                ed::PinPivotAlignment(ImVec2{0.0f, 0.5f});
                ed::EndPin();
            }
            ImGui::EndGroup();
        }
        ImGui::EndGroup();

        ImGui::SameLine();

        // --- MIDDLE (machine) ---
        ImGui::BeginGroup();
        if (machine_offset > 0.0f) ImGui::Dummy(ImVec2(0, machine_offset));
        if (graph->getGameData().machines.find(node->machine_key) != graph->getGameData().machines.end()) {
            ImGui::Image(graph->getGameData().machines.at(node->machine_key).texture, ImVec2(48,48));
        } else {
            // reserve the same space if machine missing
            ImGui::Dummy(ImVec2(48, machine_h));
        }
        const char* countText = ImGui::GetCurrentContext() ? "%.2f" : "%.2f";
        char buf[32];
        snprintf(buf, sizeof(buf), countText, node->machine_count);
        float textWidth = ImGui::CalcTextSize(buf).x;
        float imageWidth = 48.0f;
        float groupStartX = ImGui::GetCursorPosX();
        ImGui::SetCursorPosX(groupStartX + (imageWidth - textWidth) * 0.5f);
        ImGui::Text("%s", buf);
        ImGui::EndGroup();

        ImGui::SameLine();

        // --- RIGHT (outputs) ---
        ImGui::BeginGroup();
        if (outputs_offset > 0.0f) ImGui::Dummy(ImVec2(0, outputs_offset));

        if (visible_outputs > 0) {
            ImGui::BeginGroup();
            for (int port : node->output_ports) {
                Port *p = graph->getPort(port);
                if (!p || p->resource_key == "nothing") continue;

                Resource res = graph->getGameData().resources.at(p->resource_key);
                ed::BeginPin(IdUtils::ToPinId(p->id), ed::PinKind::Output);
                // text first then image on the right (keeps pin pivot consistent)
                ImGui::Text("%.2f", p->rate); ImGui::SameLine();
                ImGui::Image(res.texture, ImVec2(24,24));
                if (SettingsManager::instance().getSettings().showResourceNames) {
                    ImGui::Text("%s", res.name.c_str());
                }
                // depending on your desired layout you can switch order above
                ed::PinPivotAlignment(ImVec2{1.0f, 0.5f});
                ed::EndPin();
            }
            ImGui::EndGroup();
        }
        ImGui::EndGroup();

        ImGui::PopStyleVar(2);
        ed::EndNode();
    }
}

void FactoryNodeEditor::DrawConnections() {
    for (const auto &c: graph->getConnections()) {
        ImVec4 color = SettingsManager::instance().getSettings().themeName == "Dark" ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f,  1.0f);
        ed::Link(IdUtils::ToLinkId(c.id), IdUtils::ToPinId(c.from_port), IdUtils::ToPinId(c.to_port), color, 1.5f);
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
            int startId = IdUtils::FromPinId(start);
            int endId = IdUtils::FromPinId(end);

            selected_port_id = startId;

            if (graph->getPort(startId)->isInput) {
                std::swap(startId, endId); // Ensure start is always output
            }

            // ask the graph if the connection is valid (it knows which is input/output)
            if (!graph->isValidConnection(startId, endId)) {
                ed::RejectNewItem(ImColor(255, 128, 128), 1.0f); // Reject with red color
            } else {
                const bool connectionExists = graph->connectionExists(startId, endId);
                const ImColor color = connectionExists ? ImColor(255,128,128) : ImColor(128,255,128);

                if (ed::AcceptNewItem(color, 1.0f)) {
                    if (connectionExists) {
                        executeCommand(std::make_unique<RemoveConnectionCommand>(startId, endId));
                    } else {
                        executeCommand(std::make_unique<AddConnectionCommand>(startId, endId));
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
            selected_port_id = IdUtils::FromPinId(start);
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
                nodesToDelete.push_back(IdUtils::FromNodeId(nodeId));
            }
        }

        ed::LinkId linkId = 0;
        while (ed::QueryDeletedLink(&linkId)) {
            if (ed::AcceptDeletedItem()) {
                linksToDelete.push_back(IdUtils::FromLinkId(linkId));
            }
        }

        auto cmd = std::make_unique<CompositeCommand>("Delete operation");
        for (int id : linksToDelete) {
            auto connection = graph->getConnection(id);
            if (connection != nullptr) {
                cmd->addCommand(std::make_unique<RemoveConnectionCommand>(connection->from_port, connection->to_port));
            }
        }

        for (int id : nodesToDelete) {
            cmd->addCommand(std::make_unique<RemoveNodeCommand>(id));
        }

        executeCommand(std::move(cmd));
        std::cout << std::endl;
    }
    ed::EndDelete();

    bool isDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1);
    if (isDragging && !wasDragging) {
        // Drag just started - check if we're over a node BUT NOT over a pin
        if (ed::GetHoveredNode() && !ed::GetHoveredPin()) {
            draggedNodeId = ed::GetHoveredNode();
            draggedNodeOriginalPos = ed::GetNodePosition(draggedNodeId);
            draggingNodes = true;
        }
    } else if (!isDragging && wasDragging) {
        // Drag just ended
        if (draggingNodes) {
            std::vector<ed::NodeId> selectedNodes;
            selectedNodes.resize(ed::GetSelectedObjectCount());
            int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));
            selectedNodes.resize(nodeCount);

            draggedNodeNewPos = ed::GetNodePosition(draggedNodeId);

            // check if dragged node is in the selection
            if (std::find(selectedNodes.begin(), selectedNodes.end(), draggedNodeId) != selectedNodes.end()) {
                auto cmd = std::make_unique<CompositeCommand>("Move Nodes Command", CommandFlags{false, true});
                for (const auto &nodeId : selectedNodes) {
                    ImVec2 originalPosition = ed::GetNodePosition(nodeId) - (draggedNodeNewPos - draggedNodeOriginalPos);
                    ImVec2 newPos = ed::GetNodePosition(nodeId);;
                    if (draggedNodeOriginalPos != draggedNodeNewPos) {
                        auto node = graph->getNode(IdUtils::FromNodeId(nodeId));
                        if (node) {
                            cmd->addCommand(std::make_unique<MoveNodeCommand>(node->id, originalPosition, newPos));
                        }
                    }
                }
                executeCommand(std::move(cmd));
            } else {
                // If the dragged node is not in the selection, just move it
                auto node = graph->getNode(IdUtils::FromNodeId(draggedNodeId));
                if (node) {
                    executeCommand(std::make_unique<MoveNodeCommand>(node->id, draggedNodeOriginalPos, draggedNodeNewPos));
                }
            }
            draggingNodes = false;
        }
    }
    wasDragging = isDragging;
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
        auto node = graph->getNode(IdUtils::FromNodeId(m_contextNodeId));
        if (node) {
            ImGui::Text("Node ID: %d", node->id);
            ImGui::Text("Name: %s", node->name.c_str());
            ImGui::Text("Machine: %s", node->machine_key.c_str());
            ImGui::Text("Selected Recipe: %s", node->selected_recipe_key.c_str());
            ImGui::Text("Machine count: %.2f", node->machine_count);
            // Display input ports
            ImGui::Text("Input Ports:");
            for (int portId : node->input_ports) {
                auto port = graph->getPort(portId);
                if (port) {
                    ImGui::BulletText("Port ID: %d, Resource ID: %s, Rate: %.2f", port->id, port->resource_key.c_str(), port->rate);
                }
            }
            // Display output ports
            ImGui::Text("Output Ports:");
            for (int portId : node->output_ports) {
                auto port = graph->getPort(portId);
                if (port) {
                    ImGui::BulletText("Port ID: %d, Resource ID: %s, Rate: %.2f", port->id, port->resource_key.c_str(), port->rate);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Node")) {
                executeCommand(std::make_unique<RemoveNodeCommand>(node->id));
            }
        } else {
            ImGui::Text("Unknown node");
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Pin Context Menu")) {
        auto port = graph->getPort(IdUtils::FromPinId(m_contextPinId));
        if (port) {
            // --- Initialization on first frame ---
            if (!m_isContextMenuInitialized) {
                m_contextPinOriginalConstraint = port->user_constraint;
                m_contextPinCurrentConstraint = port->user_constraint;

                if (port->user_constraint < 0.0) {
                    m_contextPinConstraintBuf[0] = '\0';
                } else {
                    snprintf(m_contextPinConstraintBuf, sizeof(m_contextPinConstraintBuf), "%.15g", port->user_constraint);
                }
                m_isContextMenuInitialized = true; // Mark as initialized
                ImGui::SetKeyboardFocusHere();
            }

            // --- Display Port Info ---
            ImGui::Text("Port ID: %d", port->id);
            ImGui::Text("Resource Key: %s", port->resource_key.c_str());
            ImGui::Text("Is Input: %s", port->isInput ? "Yes" : "No");
            ImGui::Text("Current rate: %.2f", port->rate);
            ImGui::Text("Limit: %.2f", port->user_constraint);
            ImGui::Separator();
            ImGui::PushItemWidth(150);

            // --- User Input Handling ---
            if (ImGui::InputText("Rate", m_contextPinConstraintBuf, sizeof(m_contextPinConstraintBuf), ImGuiInputTextFlags_AutoSelectAll)) {
                // Value was edited, so we update the solver in real-time
                double v = strtod(m_contextPinConstraintBuf, nullptr);
                // Check for empty string or parse failure (strtod returns 0.0)
                if (m_contextPinConstraintBuf[0] == '\0') {
                    m_contextPinCurrentConstraint = -1.0;
                } else {
                    // Ensure constraint is not negative
                    m_contextPinCurrentConstraint = (v < 0.0) ? 0.0 : v;
                }

                graph->setPortDemand(port->id, m_contextPinCurrentConstraint);
                FactorySolver::SolverResult result = solver->solve(*graph);
                debugInfo.lastTotalSolveDurationMs = result.total_solve_time_ms;
                debugInfo.lastSetupSolveDurationMs = result.setup_time_ms;
                debugInfo.lastSolverDurationMs = result.solve_time_ms;
                debugInfo.lastUpdateFactoryDurationMs = result.update_factory_time_ms;
                debugInfo.lastSolverResult = result.status;
            }

            // --- Deactivation Logic (Enter pressed or focus lost) ---
            if (ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                if (m_contextPinCurrentConstraint != m_contextPinOriginalConstraint) {
                    // Create a single undo command for the entire change
                    executeCommand(std::make_unique<SetPortConstraintCommand>(
                        port->id, m_contextPinOriginalConstraint, m_contextPinCurrentConstraint
                    ));
                }

                ImGui::CloseCurrentPopup();
                m_isContextMenuInitialized = false;
            }
        } else {
            ImGui::Text("Unknown port");
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Link Context Menu")) {
        auto connection = graph->getConnection(IdUtils::FromLinkId(m_contextLinkId));
        if (connection) {
            ImGui::Text("Connection ID: %d", connection->id);
            ImGui::Text("From Port: %d", connection->from_port);
            ImGui::Text("To Port: %d", connection->to_port);
            ImGui::Text("Resource Key: %s", connection->resource_key.c_str());
            ImGui::Text("Current rate: %.2f", connection->rate);
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Link")) {
                executeCommand(std::make_unique<RemoveConnectionCommand>(connection->from_port, connection->to_port));
            }
        } else {
            ImGui::Text("Unknown link");
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Create new node")) {
        if (selected_port_id != -1) {
            std::string resourceFilter = graph->getGameData().resources.find(graph->getPort(selected_port_id)->resource_key)->first;
            bool fromInput = graph->getPort(selected_port_id)->isInput;
            for (const auto& recipe : graph->getGameData().recipes) {
                const auto& ports = fromInput ? recipe.second.output_ports : recipe.second.input_ports;
                for (const auto& port : ports) {
                    if (port.resource_key == resourceFilter) {
                        if (ImGui::Selectable(recipe.second.name.c_str())) {
                            executeCommand(std::make_unique<AddNodeCommand>(recipe.second.name, recipe.first, selected_port_id, ed::ScreenToCanvas(m_storedPopupPosition)));
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
            }
        } else {
            for (const auto& recipe : graph->getGameData().recipes) {
                if (ImGui::Selectable(recipe.second.name.c_str())) {
                    executeCommand(std::make_unique<AddNodeCommand>(recipe.second.name, recipe.first, -1, m_storedPopupPosition));
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }
    ed::Resume();
}