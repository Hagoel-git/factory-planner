#define IMGUI_DEFINE_MATH_OPERATORS
#include "FactoryNodeEditor.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_node_editor.h"
#include "imgui_node_editor_internal.h"

#include <unordered_map>
#include <string>
#include <utility>

#include "common/IdUtils.h"
#include "common/CopyBuffer.h"
#include "services/NotificationManager.h"
#include "services/ProjectIo.h"
#include "services/SettingsManager.h"
namespace ed = ax::NodeEditor;

FactoryNodeEditor::FactoryNodeEditor(const GameData& game_data, const std::filesystem::path &projectFilePath, std::string title)
    : m_editorName(std::move(title)), m_projectFilePath(projectFilePath), m_contextNodeId(0), m_contextPinId(0),
      m_contextLinkId(0), m_undoRedoManager(SettingsManager::instance().getSettings().maxUndoHistory) {
    try {
        VLOG(1) << "Initializing FactoryNodeEditor for project: " << projectFilePath;
        m_graph = std::make_unique<FactoryGraph>(game_data);
        m_solver = std::make_unique<FactorySolver>();

        m_nextAutosaveTime = std::chrono::steady_clock::now() +
                           std::chrono::minutes(SettingsManager::instance().getSettings().autoSaveIntervalMinutes);

        ed::Config cfg = ed::Config();
        cfg.DisableInternalFitView = true;
        cfg.AutoSaveEnabled = false;
        cfg.SettingsFile = nullptr;
        cfg.SaveSettings = nullptr;
        cfg.LoadSettings = nullptr;
        cfg.SaveNodeSettings = nullptr;
        cfg.LoadNodeSettings = nullptr;
        cfg.UserPointer = nullptr;
        m_context = ed::CreateEditor(&cfg);
        ed::SetCurrentEditor(m_context);

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

        if (std::filesystem::exists(projectFilePath)) {
            ProjectIO::loadProject(projectFilePath.string(), *m_graph);
        }

        // Initialize quadtree with large world bounds to handle extreme zoom levels
        float worldSize = 262144.0f; // 2^18, very large world
        float halfWorldSize = worldSize * 0.5f;

        quadtree::Box<float> worldBounds(
            quadtree::Vector2<float>(-halfWorldSize, -halfWorldSize),
            quadtree::Vector2<float>(worldSize, worldSize)
        );
        m_nodeQuadtree = std::make_unique<quadtree::Quadtree<NodeQuadtreeData, GetNodeBox> >(worldBounds);

        FactorySolver::SolverResult result = m_solver->solve(*m_graph);
        m_debugInfo.lastTotalSolveDurationMs = result.total_solve_time_ms;
        m_debugInfo.lastSetupSolveDurationMs = result.setup_time_ms;
        m_debugInfo.lastSolverDurationMs = result.solve_time_ms;
        m_debugInfo.lastUpdateFactoryDurationMs = result.update_factory_time_ms;
        m_debugInfo.lastSolverResult = result.status;

        m_quadtreeNeedsRebuild = true;
        LOG(INFO) << "FactoryNodeEditor initialized successfully for project: " << projectFilePath;
    } catch (const std::exception &e) {
        LOG(ERROR) << "Exception initializing FactoryNodeEditor: " << e.what();
        NotificationManager::instance().addNotification(
            "Error Initializing Editor",
            "An error occurred while initializing the editor: " + std::string(e.what()),
            NotificationType::Error
        );
    }
}

FactoryNodeEditor::~FactoryNodeEditor() {
    // Clear graph and solver
    if (m_graph) m_graph->clear();
    m_solver = nullptr;

    // Destroy node editor context
    if (m_context) {
        ed::SetCurrentEditor(m_context);
        ed::DestroyEditor(m_context);
        m_context = nullptr;
        ed::SetCurrentEditor(nullptr);
    }

    // Reset other state
    m_nodeQuadtree = nullptr;
    m_selectedPortId = -1;
    m_contextNodeId = 0;
    m_contextPinId = 0;
    m_contextLinkId = 0;
    m_quadtreeNeedsRebuild = false;
    LOG(INFO) << "FactoryNodeEditor destroyed for project: " << m_projectFilePath;
}

void FactoryNodeEditor::draw() {
    if (!m_context) return;
    m_isFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    if (std::chrono::steady_clock::now() > m_nextAutosaveTime && SettingsManager::instance().getSettings().autoSaveEnabled) {
        LOG(INFO) << "Auto-saved project: " << m_projectFilePath;
        save();
        m_nextAutosaveTime = std::chrono::steady_clock::now() +
                           std::chrono::minutes(SettingsManager::instance().getSettings().autoSaveIntervalMinutes);
    }

    ed::SetCurrentEditor(m_context);

    updateDebugInfo();

    // Begin the node editor canvas
    m_windowPos = ImGui::GetWindowPos();
    m_windowSize = ImGui::GetWindowSize();
    ed::Begin(m_editorName.c_str());

    // Rebuild quadtree if needed
    if (m_quadtreeNeedsRebuild) {
        rebuildQuadtree();
        m_quadtreeNeedsRebuild = false;
    }

    drawNodes();
    drawConnections();
    if (m_isFocused) {
        handleUserInteractions();
        handleContextMenus();
    }
    handlePopups();

    ed::End(); // End node editor

    ed::SetCurrentEditor(nullptr);
}

bool FactoryNodeEditor::save() {
    if (ProjectIO::saveProject(m_projectFilePath.string(), *m_graph, this->m_context)) {
        return true;
    }
    return false;
}

bool FactoryNodeEditor::saveAs(const std::string &newFilePath, SaveAsMode mode) {
    if (ProjectIO::saveProject(newFilePath, *m_graph, this->m_context)) {
        if (mode == SaveAsMode::SwitchToNewFile) {
            m_projectFilePath = newFilePath;
            m_editorName = std::filesystem::path(newFilePath).stem().string();
        }
        LOG(INFO) << "Project saved as: " << newFilePath;
        return true;
    }
    LOG(ERROR) << "Failed to save project as: " << newFilePath;
    return false;

}

void FactoryNodeEditor::copy(CopyBuffer &copy_buffer) {
    std::vector<ed::NodeId> selectedNodes;
    selectedNodes.resize(ed::GetSelectedObjectCount());
    int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));
    selectedNodes.resize(nodeCount);

    if (nodeCount == 0) return;

    copy_buffer.clear();

    copy_buffer.gameName = m_graph->getGameData().gameName;
    copy_buffer.sourceEditor = this;

    for (const auto &nodeId : selectedNodes) {
        uint64_t nodeIdInt = IdUtils::fromNodeId(nodeId);
        auto node = m_graph->getNode(nodeIdInt);
        if (!node) continue; // Skip invalid nodes

        copy_buffer.nodes[nodeIdInt] = *node; // Copy node
        copy_buffer.nodePositions[nodeIdInt] = ed::GetNodePosition(nodeId); // Store position

        for (const auto &portId : node->input_ports) {
            auto port = m_graph->getPort(portId);
            if (port) {
                copy_buffer.ports[portId] = *port; // Copy input port
                for (const auto &conn : m_graph->getConnectionsForPort(portId)) {
                        copy_buffer.connections.insert(*conn); // Copy incoming connection

                }
            }
        }
        for (const auto &portId : node->output_ports) {
            auto port = m_graph->getPort(portId);
            if (port) {
                copy_buffer.ports[portId] = *port; // Copy output port
                for (const auto &conn : m_graph->getConnectionsForPort(portId)) {
                        copy_buffer.connections.insert(*conn); // Copy incoming connection

                }
            }
        }
    }
    VLOG(1) << "Copied " << copy_buffer.nodes.size() << " nodes to clipboard. At " << m_editorName;
}

void FactoryNodeEditor::cut(CopyBuffer &copyBuffer) {
    copy(copyBuffer);

    if (copyBuffer.isEmpty()) return;

    auto cmd = std::make_unique<CompositeCommand>("Cut Nodes");

    for (const auto &pair : copyBuffer.nodes) {
        uint64_t nodeId = pair.first;
        cmd->addCommand(std::make_unique<RemoveNodeCommand>(nodeId));
    }
    executeCommand(std::move(cmd));
    VLOG(1) << "Cut " << copyBuffer.nodes.size() << " nodes to clipboard. At " << m_editorName;
}

void FactoryNodeEditor::paste(const CopyBuffer &copy_buffer, bool mapExternalConnections) {
    if (copy_buffer.isEmpty()) return;

    if (copy_buffer.gameName != m_graph->getGameData().gameName) {
        NotificationManager::instance().addNotification(
            "Paste Failed",
            "Cannot paste: Game mismatch ('" + copy_buffer.gameName + "' vs '" + m_graph->getGameData().gameName + "')",
            NotificationType::Warning
        );
        return;
    }

    if (copy_buffer.sourceEditor != this) {
        mapExternalConnections = false;
    }

    CopyBuffer filteredBuffer;
    filteredBuffer.gameName = copy_buffer.gameName;
    filteredBuffer.sourceEditor = copy_buffer.sourceEditor;

    const auto& gameData = m_graph->getGameData();
    std::unordered_set<uint64_t> validNodeIds;
    int skippedNodes = 0;

    for (const auto& [id, node] : copy_buffer.nodes) {
        bool machineValid = gameData.machines.count(node.machine_key);
        bool recipeValid = gameData.recipes.count(node.selected_recipe_key);

        if (recipeValid) {
            filteredBuffer.nodes[id] = node;
            if (!machineValid) {
                const Recipe& recipe = gameData.recipes.at(node.selected_recipe_key);
                if (!recipe.produced_in_machines_keys.empty()) {
                    filteredBuffer.nodes[id].machine_key = m_graph->resolvePreferredMachine(recipe.produced_in_machines_keys);
                } else {
                    filteredBuffer.nodes[id].machine_key = "";
                }
            }
            filteredBuffer.nodePositions[id] = copy_buffer.nodePositions.at(id);
            validNodeIds.insert(id);

            for (const auto& [pid, port] : copy_buffer.ports) {
                if (port.node_id == id) {
                    filteredBuffer.ports[pid] = port;
                }
            }
        } else {
            skippedNodes++;
            VLOG(2) << "Skipping paste of node " << node.name << " (ID: " << id << ") due to missing recipe/machine.";
        }
    }
    if (filteredBuffer.nodes.empty()) {
        if (skippedNodes > 0) {
            NotificationManager::instance().addNotification("Paste Failed", "All" + std::to_string(skippedNodes) + " copied nodes contained invalid recipes/machines for this game version.", NotificationType::Warning);
        }
        return;
    }

    for (const auto& conn : copy_buffer.connections) {
        // Check if the ports exist in the source buffer at all
        bool fromInSource = copy_buffer.ports.count(conn.from_port);
        bool toInSource = copy_buffer.ports.count(conn.to_port);

        // Check if they are valid (i.e., belong to a node we are actually pasting)
        bool fromValid = fromInSource && validNodeIds.count(copy_buffer.ports.at(conn.from_port).node_id);
        bool toValid = toInSource && validNodeIds.count(copy_buffer.ports.at(conn.to_port).node_id);

        // A port is skipped if it WAS in the buffer but is NOT valid
        bool fromSkipped = fromInSource && !fromValid;
        bool toSkipped = toInSource && !toValid;

        // Only keep connections where neither side refers to a skipped/invalid node
        if (!fromSkipped && !toSkipped) {
            filteredBuffer.connections.insert(conn);
        }
    }

    if (skippedNodes > 0) {
        NotificationManager::instance().addNotification("Partial Paste", std::to_string(skippedNodes) + " node(s) were skipped because their recipes or machines don't exist in this game data.", NotificationType::Warning);
    }

    executeCommand(std::make_unique<PasteCommand>(filteredBuffer, mapExternalConnections));
    VLOG(1) << "Pasted " << filteredBuffer.nodes.size() << " nodes from clipboard. At " << m_editorName;
}

void FactoryNodeEditor::selectAll() {
    ed::ClearSelection();
    for (const auto &node : m_graph->getNodes()) {
        ed::SelectNode(IdUtils::toNodeId(node.id), true); // Select all nodes
    }
    VLOG(1) << "Selected all nodes in the graph. At " << m_editorName;
}

void FactoryNodeEditor::showFlow() {
    for (const auto& connection : m_graph->getConnections()) {
        ed::Flow(IdUtils::toLinkId(connection.id)); // Show flow for all connections
    }
}

void FactoryNodeEditor::fitView(bool force) {
    const auto& allNodes = m_graph->getNodes();

    if (!force && allNodes.size() > 1000) {
        m_showFitViewConfirmation = true;
        return;
    }

    if (!allNodes.empty()) {
        ImRect contentBounds;
        bool isFirstNode = true;

        for (const auto& node : allNodes) {
            ed::NodeId nodeId = IdUtils::toNodeId(node.id);
            ImVec2 nodePos = ed::GetNodePosition(nodeId);
            ImVec2 nodeSize = ed::GetNodeSize(nodeId);

            // A node that has never been drawn will have a size of (0,0).
            // We'll use a default fallback size to ensure it's included in the bounds.
            if (nodeSize.x <= 0.0f || nodeSize.y <= 0.0f) {
                nodeSize = ImVec2(300.0f, 100.0f); // A reasonable default estimate.
            }

            ImRect nodeBounds(nodePos, nodePos + nodeSize);

            if (isFirstNode) {
                contentBounds = nodeBounds;
                isFirstNode = false;
            } else {
                contentBounds.Add(nodeBounds);
            }
        }

        // Add some padding so nodes aren't right at the edge of the view
        const float padding = 100.0f;
        contentBounds.Min.x -= padding;
        contentBounds.Min.y -= padding;
        contentBounds.Max.x += padding;
        contentBounds.Max.y += padding;

        ed::NavigateTo(contentBounds);
    } else {
        ed::NavigateToContent();
    }
}

void FactoryNodeEditor::undo() {
    if (!m_undoRedoManager.canUndo()) return;

    const Command *command = m_undoRedoManager.getCommandToUndo();
    if (!command) return;

    CommandFlags flags = command->getFlags();

    m_undoRedoManager.undo(*m_graph);
    VLOG(1) << "Undo command '" << command->getDescription() << "' executed in editor: " << m_editorName;

    if (flags.needsSolve) {
        FactorySolver::SolverResult result = m_solver->solve(*m_graph);
        m_debugInfo.lastTotalSolveDurationMs = result.total_solve_time_ms;
        m_debugInfo.lastSetupSolveDurationMs = result.setup_time_ms;
        m_debugInfo.lastSolverDurationMs = result.solve_time_ms;
        m_debugInfo.lastUpdateFactoryDurationMs = result.update_factory_time_ms;
        m_debugInfo.lastSolverResult = result.status;
    }
    m_quadtreeNeedsRebuild = flags.needsRebuild;
}

void FactoryNodeEditor::redo() {
    if (!m_undoRedoManager.canRedo()) return;

    const Command *command = m_undoRedoManager.getCommandToRedo();
    if (!command) return;

    CommandFlags flags = command->getFlags();

    m_undoRedoManager.redo(*m_graph);
    VLOG(1) << "Redo command '" << command->getDescription() << "' executed in editor: " << m_editorName;

    if (flags.needsSolve) {
        FactorySolver::SolverResult result = m_solver->solve(*m_graph);
        m_debugInfo.lastTotalSolveDurationMs = result.total_solve_time_ms;
        m_debugInfo.lastSetupSolveDurationMs = result.setup_time_ms;
        m_debugInfo.lastSolverDurationMs = result.solve_time_ms;
        m_debugInfo.lastUpdateFactoryDurationMs = result.update_factory_time_ms;
        m_debugInfo.lastSolverResult = result.status;
    }
    m_quadtreeNeedsRebuild = flags.needsRebuild;
}


void FactoryNodeEditor::executeCommand(std::unique_ptr<Command> command) {
    if (!command) return;
    // check if it is a composite command and if it has no sub-commands, then ignore it
    if (auto compositeCommand = dynamic_cast<CompositeCommand*>(command.get())) {
        if (compositeCommand->isEmpty()) {
            return;
        }
    }
    CommandFlags flags = command->getFlags();
    m_undoRedoManager.executeCommand(std::move(command), *m_graph);
    if (flags.needsSolve) {
        FactorySolver::SolverResult result = m_solver->solve(*m_graph);
        m_debugInfo.lastTotalSolveDurationMs = result.total_solve_time_ms;
        m_debugInfo.lastSetupSolveDurationMs = result.setup_time_ms;
        m_debugInfo.lastSolverDurationMs = result.solve_time_ms;
        m_debugInfo.lastUpdateFactoryDurationMs = result.update_factory_time_ms;
        m_debugInfo.lastSolverResult = result.status;
    }
    m_quadtreeNeedsRebuild = flags.needsRebuild;
}

void FactoryNodeEditor::updateDebugInfo() {
    m_debugInfo.filePath = m_projectFilePath;
    m_debugInfo.gameDataPath = m_graph->getGameData().gameDataFilePath;
    m_debugInfo.totalNodes = static_cast<int>(m_graph->getNodes().size());
    m_debugInfo.totalConnections = static_cast<int>(m_graph->getConnections().size());
    m_debugInfo.totalPorts = static_cast<int>(m_graph->getPorts().size());
    m_debugInfo.selectionSize = ed::GetSelectedObjectCount();
    if (m_nodeQuadtree) {
        auto bounds = m_nodeQuadtree->getBox();
        m_debugInfo.quadtreeBoundsMin[0] = bounds.getTopLeft().x;
        m_debugInfo.quadtreeBoundsMin[1] = bounds.getTopLeft().y;
        m_debugInfo.quadtreeBoundsMax[0] = bounds.getTopLeft().x + bounds.getSize().x;
        m_debugInfo.quadtreeBoundsMax[1] = bounds.getTopLeft().y + bounds.getSize().y;

        ImVec2 canvasMin = ed::ScreenToCanvas(m_windowPos);
        ImVec2 canvasMax = ed::ScreenToCanvas(m_windowPos + m_windowSize);

        m_debugInfo.viewBoundsMin[0] = canvasMin.x;
        m_debugInfo.viewBoundsMin[1] = canvasMin.y;
        m_debugInfo.viewBoundsMax[0] = canvasMax.x;
        m_debugInfo.viewBoundsMax[1] = canvasMax.y;
    }

    m_debugInfo.undoStackSize = m_undoRedoManager.getUndoStackSize();
    m_debugInfo.redoStackSize = m_undoRedoManager.getRedoStackSize();
}

void FactoryNodeEditor::rebuildQuadtree() {
    // Use very large world bounds to handle any zoom level
    // This ensures the quadtree can handle extreme zoom levels
    float worldSize = 262144.0f; // 2^18, very large world
    float halfWorldSize = worldSize * 0.5f;

    quadtree::Box<float> worldBounds(
        quadtree::Vector2<float>(-halfWorldSize, -halfWorldSize),
        quadtree::Vector2<float>(worldSize, worldSize)
    );
    m_nodeQuadtree = std::make_unique<quadtree::Quadtree<NodeQuadtreeData, GetNodeBox>>(worldBounds);

    // Add all nodes to quadtree
    for (const auto &node : m_graph->getNodes()) {
        ed::NodeId nodeId = IdUtils::toNodeId(node.id);
        ImVec2 nodePos = ed::GetNodePosition(nodeId);
        ImVec2 nodeSize = ed::GetNodeSize(nodeId);
        if (nodeSize.x <= 0 || nodeSize.y <= 0) {
            nodeSize = ImVec2(300.0f, 100.0f); // Default size if not provided
        }
        m_nodeQuadtree->add(NodeQuadtreeData(node.id, nodePos, nodeSize));
    }
}

std::vector<NodeQuadtreeData> FactoryNodeEditor::getVisibleNodes(const ImVec2& viewMin, const ImVec2& viewMax) {
    if (!m_nodeQuadtree) {
        return {};
    }

    // Add some padding to the view to catch nodes that are partially visible
    float padding = 100.0;

    quadtree::Box<float> viewBox(
        quadtree::Vector2<float>(viewMin.x - padding, viewMin.y - padding),
        quadtree::Vector2<float>((viewMax.x - viewMin.x) + 2.0f * padding, (viewMax.y - viewMin.y) + 2.0f * padding)
    );

    // Check if view box intersects with quadtree bounds before querying
    auto quadtreeBounds = m_nodeQuadtree->getBox();
    if (!viewBox.intersects(quadtreeBounds)) {
        return {}; // No intersection, return empty result
    }

    return m_nodeQuadtree->query(viewBox);
}

void FactoryNodeEditor::drawNodes() {
    // Get the visible screen area and convert it to canvas coordinates

    ImVec2 viewMin = m_windowPos;
    ImVec2 viewMax = viewMin + m_windowSize;
    ImVec2 canvasMin = ed::ScreenToCanvas(viewMin);
    ImVec2 canvasMax = ed::ScreenToCanvas(viewMax);

    // Get visible nodes from quadtree
    auto visibleNodes = getVisibleNodes(canvasMin, canvasMax);

    // Build set of visible node ids
    std::unordered_set<uint64_t> visibleNodeIds;
    visibleNodeIds.reserve(visibleNodes.size());
    for (const auto &nd : visibleNodes) visibleNodeIds.insert(nd.nodeId);

    std::unordered_set<uint64_t> nodesToRegister = visibleNodeIds;

    // Iterate only through the visible nodes
    for (uint64_t nodeId : visibleNodeIds) {
        auto node = m_graph->getNode(nodeId);
        if (!node) continue;

        // Check input ports
        for (uint64_t portId : node->input_ports) {
            for (const auto& conn : m_graph->getConnectionsForPort(portId)) {
                // Check if the connection is incoming or outgoing to determine the remote node
                if (conn->to_port == portId) {
                    auto fromPort = m_graph->getPort(conn->from_port);
                    if (fromPort) {
                        nodesToRegister.insert(fromPort->node_id);
                    } else {
                        LOG(WARNING) << "Input port " << portId << " of node " << nodeId << " has no valid connection.";
                    }
                }
            }
        }

        // Check output ports
        for (uint64_t portId : node->output_ports) {
            for (const auto& conn : m_graph->getConnectionsForPort(portId)) {
                // Check if the connection is incoming or outgoing to determine the remote node
                if (conn->from_port == portId) {
                    auto toPort = m_graph->getPort(conn->to_port);
                    if (toPort) {
                        nodesToRegister.insert(toPort->node_id);
                    } else {
                        LOG(WARNING) << "Output port " << portId << " of node " << nodeId << " has no valid connection.";
                    }
                }
            }
        }
    }

    m_debugInfo.visibleNodes = static_cast<int>(nodesToRegister.size());

    std::string deferredTooltip;
    // Draw only visible nodes
    for (const auto& nodeData : nodesToRegister) {
        auto node = m_graph->getNode(nodeData);
        if (!node) continue;

        ed::NodeId nodeId = IdUtils::toNodeId(node->id);
        ed::BeginNode(nodeId);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2,0));

        // --- PRE-CALC: count visible ports and heights ----
        ImGuiStyle &style = ImGui::GetStyle();
        float font_h = ImGui::GetFontSize();                // text height
        float port_img_h = font_h;                          // the image height for pins
        float port_line_h = std::max(port_img_h, font_h);  // effective per-pin height
        float spacing_y = style.ItemSpacing.y;             // vertical spacing between items

        // Count only visible ports (skip resource_key == "nothing")
        int visible_inputs = 0, visible_outputs = 0;
        for (uint64_t portId : node->input_ports) {
            Port *p = m_graph->getPort(portId);
            if (p && p->resource_key != "nothing") ++visible_inputs;
        }
        for (uint64_t portId : node->output_ports) {
            Port *p = m_graph->getPort(portId);
            if (p && p->resource_key != "nothing") ++visible_outputs;
        }

        // Heights of each column
        float inputs_h = (visible_inputs > 0) ? (visible_inputs * port_line_h + (visible_inputs - 1) * spacing_y) : 0.0f;
        float outputs_h = (visible_outputs > 0) ? (visible_outputs * port_line_h + (visible_outputs - 1) * spacing_y) : 0.0f;
        float machine_h = port_img_h * 2; // the machine image height
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
            for (uint64_t port : node->input_ports) {
                Port *p = m_graph->getPort(port);
                if (!p || p->resource_key == "nothing") continue;

                Resource res = m_graph->getGameData().resources.at(p->resource_key);
                ed::BeginPin(IdUtils::toPinId(p->id), ed::PinKind::Input);
                if (m_graph->getConnectionsForPort(p->id).empty()) {
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    ImVec2 size(port_img_h, port_img_h);

                    bool isDark = SettingsManager::instance().getSettings().themeName == "Dark";
                    ImU32 bgColor = isDark ? IM_COL32(190, 70, 70, 200)
                                           : IM_COL32(235, 180, 180, 255);

                    ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bgColor, 0.0f);
                }
                ImGui::Image(res.texture, ImVec2(port_img_h,port_img_h)); ImGui::SameLine();
                if (ImGui::IsItemHovered()) {
                    deferredTooltip = res.name;
                }
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
        if (m_graph->getGameData().machines.find(node->machine_key) != m_graph->getGameData().machines.end()) {
            ImGui::Image(m_graph->getGameData().machines.at(node->machine_key).texture, ImVec2(machine_h,machine_h));
        } else {
            ImGui::Dummy(ImVec2(48, machine_h));
        }
        const char* countText = ImGui::GetCurrentContext() ? "%.2f" : "%.2f";
        char buf[32];
        snprintf(buf, sizeof(buf), countText, node->machine_count);
        float textWidth = ImGui::CalcTextSize(buf).x;
        float imageWidth = machine_h;
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
            for (uint64_t port : node->output_ports) {
                Port *p = m_graph->getPort(port);
                if (!p || p->resource_key == "nothing") continue;

                Resource res = m_graph->getGameData().resources.at(p->resource_key);
                ed::BeginPin(IdUtils::toPinId(p->id), ed::PinKind::Output);
                ImGui::Text("%.2f", p->rate); ImGui::SameLine();
                if (m_graph->getConnectionsForPort(p->id).empty()) {
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    ImVec2 size(port_img_h, port_img_h);

                    bool isDark = SettingsManager::instance().getSettings().themeName == "Dark";
                    ImU32 bgColor = isDark ? IM_COL32(70, 190, 70, 200)
                                           : IM_COL32(180, 235, 180, 255);

                    ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bgColor, 0.0f);
                }
                ImGui::Image(res.texture, ImVec2(port_img_h,port_img_h));
                if (ImGui::IsItemHovered()) {
                    deferredTooltip = res.name;
                }
                if (SettingsManager::instance().getSettings().showResourceNames) {
                    ImGui::Text("%s", res.name.c_str());
                }
                ed::PinPivotAlignment(ImVec2{1.0f, 0.5f});
                ed::EndPin();
            }
            ImGui::EndGroup();
        }
        ImGui::EndGroup();

        ImGui::PopStyleVar(2);
        ed::EndNode();
    }

    if (!deferredTooltip.empty()) {
        ed::Suspend();

        ImGui::SetNextWindowPos(ImGui::GetMousePos() + ImVec2(15, 15));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.1f, 0.1f, 0.1f, 0.95f));

        ImGui::BeginTooltip();
        ImGui::Text("%s", deferredTooltip.c_str());
        ImGui::EndTooltip();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ed::Resume();
    }
}

void FactoryNodeEditor::drawConnections() {
    for (const auto &c: m_graph->getConnections()) {
        ImVec4 color = SettingsManager::instance().getSettings().themeName == "Dark" ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f,  1.0f);
        ed::Link(IdUtils::toLinkId(c.id), IdUtils::toPinId(c.from_port), IdUtils::toPinId(c.to_port), color, 1.5f);
    }
}

void FactoryNodeEditor::handleUserInteractions() {
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
            uint64_t startId = IdUtils::fromPinId(start);
            uint64_t endId = IdUtils::fromPinId(end);

            m_selectedPortId = startId;

            if (m_graph->isInputPort(startId)) {
                std::swap(startId, endId); // Ensure start is always output
            }

            // ask the graph if the connection is valid (it knows which is input/output)
            if (!m_graph->isValidConnection(startId, endId)) {
                ed::RejectNewItem(ImColor(255, 128, 128), 1.0f); // Reject with red color
            } else {
                const bool connectionExists = m_graph->connectionExists(startId, endId);
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
            m_selectedPortId = IdUtils::fromPinId(start);
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
        std::vector<uint64_t> nodesToDelete;
        std::vector<uint64_t> linksToDelete;

        ed::NodeId nodeId = 0;
        while (ed::QueryDeletedNode(&nodeId)) {
            if (ed::AcceptDeletedItem()) {
                nodesToDelete.push_back(IdUtils::fromNodeId(nodeId));
            }
        }

        ed::LinkId linkId = 0;
        while (ed::QueryDeletedLink(&linkId)) {
            if (ed::AcceptDeletedItem()) {
                linksToDelete.push_back(IdUtils::fromLinkId(linkId));
            }
        }

        auto cmd = std::make_unique<CompositeCommand>("Delete operation");
        for (uint64_t id : linksToDelete) {
            auto connection = m_graph->getConnection(id);
            if (connection != nullptr) {
                cmd->addCommand(std::make_unique<RemoveConnectionCommand>(connection->from_port, connection->to_port));
            }
        }

        for (uint64_t id : nodesToDelete) {
            cmd->addCommand(std::make_unique<RemoveNodeCommand>(id));
        }

        executeCommand(std::move(cmd));
        std::cout << std::endl;
    }
    ed::EndDelete();

    bool isDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1);
    if (isDragging && !m_wasDragging) {
        // Drag just started - check if we're over a node BUT NOT over a pin
        if (ed::GetHoveredNode() && !ed::GetHoveredPin()) {
            m_draggedNodeId = ed::GetHoveredNode();
            m_draggedNodeOriginalPos = ed::GetNodePosition(m_draggedNodeId);
            m_draggingNodes = true;
        }
    } else if (!isDragging && m_wasDragging) {
        // Drag just ended
        if (m_draggingNodes) {
            std::vector<ed::NodeId> selectedNodes;
            selectedNodes.resize(ed::GetSelectedObjectCount());
            int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));
            selectedNodes.resize(nodeCount);

            m_draggedNodeNewPos = ed::GetNodePosition(m_draggedNodeId);

            // check if dragged node is in the selection
            if (std::find(selectedNodes.begin(), selectedNodes.end(), m_draggedNodeId) != selectedNodes.end()) {
                auto cmd = std::make_unique<CompositeCommand>("Move Nodes Command", CommandFlags{false, true});
                for (const auto &nodeId : selectedNodes) {
                    ImVec2 originalPosition = ed::GetNodePosition(nodeId) - (m_draggedNodeNewPos - m_draggedNodeOriginalPos);
                    ImVec2 newPos = ed::GetNodePosition(nodeId);;
                    if (m_draggedNodeOriginalPos != m_draggedNodeNewPos) {
                        auto node = m_graph->getNode(IdUtils::fromNodeId(nodeId));
                        if (node) {
                            cmd->addCommand(std::make_unique<MoveNodeCommand>(node->id, originalPosition, newPos));
                        }
                    }
                }
                executeCommand(std::move(cmd));
            } else {
                // If the dragged node is not in the selection, just move it
                auto node = m_graph->getNode(IdUtils::fromNodeId(m_draggedNodeId));
                if (node) {
                    executeCommand(std::make_unique<MoveNodeCommand>(node->id, m_draggedNodeOriginalPos, m_draggedNodeNewPos));
                }
            }
            m_draggingNodes = false;
        }
    }
    m_wasDragging = isDragging;
}

void FactoryNodeEditor::handleContextMenus() {
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
        m_selectedPortId = -1;
        ed::Resume();
        m_storedPopupPosition = ImGui::GetMousePosOnOpeningCurrentPopup();
    }
}

void FactoryNodeEditor::handlePopups() {
    ed::Suspend();

    if (m_showFitViewConfirmation) {
        ImGui::OpenPopup("Confirm Fit View");
        m_showFitViewConfirmation = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Confirm Fit View", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("The graph contains %zu nodes.", m_graph->getNodes().size());
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Drawing this many nodes might cause a freeze, crash, or very low FPS.");
        ImGui::Text("Do you want to continue?");

        ImGui::Separator();

        if (ImGui::Button("Yes", ImVec2(120, 0))) {
            fitView(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    const bool showDebug = SettingsManager::instance().getSettings().showDebugInfo;
    const auto& gameData = m_graph->getGameData();
    auto fontSize = ImGui::GetFontSize();
    if (ImGui::BeginPopup("Node Context Menu")) {
        auto node = m_graph->getNode(IdUtils::fromNodeId(m_contextNodeId));
        if (node) {
            if (gameData.machines.count(node->machine_key)) {
                ImTextureID icon = gameData.machines.at(node->machine_key).texture;
                ImGui::Image(icon, ImVec2(fontSize, fontSize));
                ImGui::SameLine();
            }
            ImGui::Text("%s", node->name.c_str());

            ImGui::Separator();

            std::vector<std::string> allowedMachines;
            if (gameData.recipes.count(node->selected_recipe_key)) {
                allowedMachines = gameData.recipes.at(node->selected_recipe_key).produced_in_machines_keys;
            }

            bool isCurrentMachineValid = std::find(allowedMachines.begin(), allowedMachines.end(), node->machine_key) != allowedMachines.end();

            if (allowedMachines.size() > 1 || (!allowedMachines.empty() && !isCurrentMachineValid)) {
                ImGui::TextDisabled("Change Machine:");

                std::string currentMachineName = node->machine_key;
                if (gameData.machines.count(node->machine_key)) {
                    currentMachineName = gameData.machines.at(node->machine_key).name;
                } else if (node->machine_key.empty()) {
                    currentMachineName = "None";
                }

                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::BeginCombo("##machine_selector", currentMachineName.c_str())) {
                    for (const auto& mKey : allowedMachines) {
                        if (gameData.machines.find(mKey) == gameData.machines.end()) continue;

                        const auto& machine = gameData.machines.at(mKey);
                        bool isSelected = (node->machine_key == mKey);

                        ImGui::PushID(mKey.c_str());


                        ImGui::Image(machine.texture, ImVec2(fontSize, fontSize));
                        ImGui::SameLine();

                        if (ImGui::Selectable(machine.name.c_str(), isSelected)) {
                            if (node->machine_key != mKey) {
                                executeCommand(std::make_unique<ChangeMachineCommand>(
                                    node->id, node->machine_key, mKey
                                ));
                            }
                        }

                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Speed: %.2fx", machine.base_crafting_speed);
                        }

                        if (isSelected) ImGui::SetItemDefaultFocus();
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                bool isPref = m_graph->isMachinePreferred(node->machine_key);
                if (isPref) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
                if (ImGui::Button("P")) {
                    m_graph->setMachinePreferred(node->machine_key, !isPref);
                }
                if (isPref) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(isPref ? "Unmark as Preferred Default" : "Set as Preferred Default for new nodes");
                }
            } else {
                ImGui::TextDisabled("Machine:");
                ImGui::SameLine();
                ImGui::Text("%s", gameData.machines.count(node->machine_key) ? gameData.machines.at(node->machine_key).name.c_str() : node->machine_key.c_str());
            }

            static double s_startClockSpeed = 0.0;
            float clockSpeedFlt = node->clock_speed;

            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::DragFloat("Clock speed (%)", &clockSpeedFlt, 1, 0, FLT_MAX, "%.2f")) {
                node->clock_speed = static_cast<double>(clockSpeedFlt);
            }

            if (ImGui::IsItemActivated()) {
                s_startClockSpeed = node->clock_speed;
            }

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                if (s_startClockSpeed != node->clock_speed) {
                    executeCommand(std::make_unique<ChangeClockSpeedCommand>(
                        node->id, s_startClockSpeed, node->clock_speed
                    ));
                }
            }

            static double s_startProductionMultiplier = 0.0;
            float prodMultFlt = node->production_multiplier;

            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::DragFloat("Production Multiplier (%)", &prodMultFlt, 1, 0, FLT_MAX, "%.2f")) {
                node->production_multiplier = static_cast<double>(prodMultFlt);
            }

            if (ImGui::IsItemActivated()) {
                s_startProductionMultiplier = node->production_multiplier;
            }

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                if (s_startProductionMultiplier != node->production_multiplier) {
                    executeCommand(std::make_unique<ChangeProductionMultiplierCommand>(
                        node->id, s_startProductionMultiplier, node->production_multiplier
                    ));
                }
            }
            ImGui::Separator();

            if (ImGui::MenuItem("Delete Node")) {
                executeCommand(std::make_unique<RemoveNodeCommand>(node->id));
            }

            if (showDebug) {
                ImGui::Separator();
                ImGui::TextDisabled("Debug Info");
                ImGui::Text("Node ID: %lu", node->id);
                ImGui::Text("Recipe Key: %s", node->selected_recipe_key.c_str());
                ImGui::Text("Machine Key: %s", node->machine_key.c_str());
                ImGui::Text("Machine Count: %.4f", node->machine_count);
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("Pin Context Menu")) {
        auto port = m_graph->getPort(IdUtils::fromPinId(m_contextPinId));
        if (port) {
            bool shouldFocusRate = false;

            if (!m_isContextMenuInitialized) {
                m_contextPinOriginalConstraint = port->user_constraint;
                m_contextPinCurrentConstraint = port->user_constraint;
                if (port->user_constraint < 0.0) m_contextPinConstraintBuf[0] = '\0';
                else snprintf(m_contextPinConstraintBuf, sizeof(m_contextPinConstraintBuf), "%.15g", port->user_constraint);

                m_isContextMenuInitialized = true;
                shouldFocusRate = true; // Signal to focus the input later
            }

            if (gameData.resources.count(port->resource_key)) {
                double imageSize = ImGui::GetFontSize();
                ImGui::Image(gameData.resources.at(port->resource_key).texture, ImVec2(imageSize, imageSize));
                ImGui::SameLine();
                ImGui::Text("%s", gameData.resources.at(port->resource_key).name.c_str());
            } else {
                ImGui::Text("%s", port->resource_key.c_str());
            }

            ImGui::SameLine();
            ImGui::TextDisabled(m_graph->isInputPort(port->id) ? "(Input)" : "(Output)");

            ImGui::Separator();

            ImGui::Text("Flow Rate Limit:");
            ImGui::PushItemWidth(-1);

            bool hugeGraph = m_graph->getNodes().size() >= 1000;

            if (shouldFocusRate) ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##rate", m_contextPinConstraintBuf, sizeof(m_contextPinConstraintBuf), ImGuiInputTextFlags_AutoSelectAll)) {
                double v = strtod(m_contextPinConstraintBuf, nullptr);
                if (m_contextPinConstraintBuf[0] == '\0') m_contextPinCurrentConstraint = -1.0;
                else m_contextPinCurrentConstraint = (v < 0.0) ? 0.0 : v;

                // Only solve LIVE if graph is small
                if (!hugeGraph) {
                    m_graph->setPortConstraint(port->id, m_contextPinCurrentConstraint);
                    m_solver->solve(*m_graph);
                }
            }
            ImGui::PopItemWidth();
            if (ImGui::IsItemDeactivatedAfterEdit() || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
                if (m_contextPinCurrentConstraint != m_contextPinOriginalConstraint) {
                    executeCommand(std::make_unique<SetPortConstraintCommand>(
                        port->id, m_contextPinOriginalConstraint, m_contextPinCurrentConstraint
                    ));
                }
                ImGui::CloseCurrentPopup();
                m_isContextMenuInitialized = false;
            }

            ImGui::TextDisabled("Current Actual Flow: %.2f", port->rate);


            if (showDebug) {
                ImGui::Separator();
                ImGui::TextDisabled("Debug Info");
                ImGui::Text("Port ID: %lu", port->id);
                ImGui::Text("Node ID: %lu", port->node_id);
                ImGui::Text("Constraint Val: %f", port->user_constraint);
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Link Context Menu")) {
        auto connection = m_graph->getConnection(IdUtils::fromLinkId(m_contextLinkId));
        if (connection) {
            if (gameData.resources.count(connection->resource_key)) {
                ImGui::Image(gameData.resources.at(connection->resource_key).texture, ImVec2(24, 24));
                ImGui::SameLine();
                ImGui::Text("%s Flow", gameData.resources.at(connection->resource_key).name.c_str());
            } else {
                ImGui::Text("%s Flow", connection->resource_key.c_str());
            }
            ImGui::Separator();

            std::string timeUnit = gameData.time_unit;
            if (timeUnit == "seconds") timeUnit = "sec";
            else if (timeUnit == "minutes") timeUnit = "min";
            else if (timeUnit == "hours") timeUnit = "hour";

            ImGui::Text("Current Rate: %.2f / %s", connection->rate, timeUnit.c_str());

            ImGui::Separator();

            if (ImGui::MenuItem("Delete Link")) {
                executeCommand(std::make_unique<RemoveConnectionCommand>(connection->from_port, connection->to_port));
            }

            if (showDebug) {
                ImGui::Separator();
                ImGui::TextDisabled("Debug Info");
                ImGui::Text("Link ID: %lu", connection->id);
                ImGui::Text("From Port: %lu", connection->from_port);
                ImGui::Text("To Port: %lu", connection->to_port);
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Create new node")) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
            m_recipeSearchBuffer[0] = '\0';
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##search", "Search Recipe...", m_recipeSearchBuffer, sizeof(m_recipeSearchBuffer));

        ImGui::Checkbox("Generate", &m_useGenerate);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle to use generate functionality");

        ImGui::Separator();

        const float iconSize = ImGui::GetFontSize();
        const float sideColWidth = 158.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::BeginTable("RecipeList", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg, ImVec2(640, 480))) {
            ImGui::TableSetupColumn("Inputs", ImGuiTableColumnFlags_WidthFixed, sideColWidth);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Outputs", ImGuiTableColumnFlags_WidthFixed, sideColWidth);

            std::string filter = m_recipeSearchBuffer;
            std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

            std::string portResourceKey;
            bool portIsInput = false;
            bool hasContext = (m_selectedPortId != -1);
            if (hasContext) {
                auto p = m_graph->getPort(m_selectedPortId);
                if (p) {
                    portResourceKey = p->resource_key;
                    portIsInput = m_graph->isInputPort(m_selectedPortId);
                } else {
                    hasContext = false;
                }
            }

            const auto& resources = m_graph->getGameData().resources;

            for (const auto& recipePair : m_graph->getGameData().recipes) {
                const auto& recipe = recipePair.second;

                if (hasContext) {
                    bool compatible = false;
                    const auto& targetPorts = portIsInput ? recipe.output_ports : recipe.input_ports;
                    for (const auto& p : targetPorts) {
                        if (p.resource_key == portResourceKey) {
                            compatible = true;
                            break;
                        }
                    }
                    if (!compatible) continue;
                }

                bool match = false;
                if (filter.empty()) {
                    match = true;
                } else {
                    std::string nameLower = recipe.name;
                    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                    if (nameLower.find(filter) != std::string::npos) match = true;

                    if (!match) {
                        for (const auto& p : recipe.input_ports) {
                            if (resources.count(p.resource_key)) {
                                std::string resName = resources.at(p.resource_key).name;
                                std::transform(resName.begin(), resName.end(), resName.begin(), ::tolower);
                                if (resName.find(filter) != std::string::npos) {
                                    match = true;
                                    break;
                                }
                            }
                        }
                    }

                    if (!match) {
                        for (const auto& p : recipe.output_ports) {
                            if (resources.count(p.resource_key)) {
                                std::string resName = resources.at(p.resource_key).name;
                                std::transform(resName.begin(), resName.end(), resName.begin(), ::tolower);
                                if (resName.find(filter) != std::string::npos) {
                                    match = true;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (!match) continue;

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                for (const auto& p : recipe.input_ports) {
                    if (p.resource_key == "nothing") continue;
                    auto it = resources.find(p.resource_key);
                    if (it != resources.end()) {
                        ImGui::Image(it->second.texture, ImVec2(iconSize, iconSize));
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", it->second.name.c_str());
                        ImGui::SameLine();
                    }
                }

                ImGui::TableNextColumn();
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
                if (ImGui::Selectable(recipe.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                    uint64_t fromPort = hasContext ? m_selectedPortId : -1;
                    ImVec2 nodePos = hasContext ? ed::ScreenToCanvas(m_storedPopupPosition) : ed::ScreenToCanvas(ImGui::GetMousePosOnOpeningCurrentPopup());

                    if (hasContext) {
                        // 1. Setup Style Constants
                        auto& style = ImGui::GetStyle();
                        const float port_h = std::max(24.0f, ImGui::GetFontSize()); // Height of one port row
                        const float space_y = style.ItemSpacing.y;
                        const float space_x = style.ItemSpacing.x;
                        const float pad_y = ed::GetStyle().NodePadding.y;
                        const float pad_x = ed::GetStyle().NodePadding.x;

                        // 2. Helper Lambda to calculate Column Metrics (Height & Max Width)
                        // Returns {column_height, max_width, target_index}
                        auto getColMetrics = [&](const std::vector<RecipePort>& ports, const std::string& targetKey) {
                            float max_w = 0.0f;
                            int count = 0;
                            int target_idx = -1;

                            for (const auto& p : ports) {
                                if (p.resource_key == "nothing") continue;

                                // Track target index
                                if (p.resource_key == targetKey) target_idx = count;

                                // Calculate width (Icon + Spacing + Rate + Spacing + Optional Text)
                                float w = 24.0f + 30.0f + space_x;
                                if (SettingsManager::instance().getSettings().showResourceNames && resources.count(p.resource_key)) {
                                    w += space_x + ImGui::CalcTextSize(resources.at(p.resource_key).name.c_str()).x;
                                }
                                max_w = std::max(max_w, w);
                                count++;
                            }

                            float h = (count > 0) ? (count * port_h + (count - 1) * space_y) : 0.0f;
                            return std::make_tuple(h, max_w, target_idx);
                        };

                        // 3. Calculate Metrics
                        // If dragging FROM Input, we connect TO Output (target is output), and vice versa
                        bool is_target_input = !portIsInput;

                        auto [in_h, in_w, in_idx]    = getColMetrics(recipe.input_ports,  is_target_input ? portResourceKey : "");
                        auto [out_h, out_w, out_idx] = getColMetrics(recipe.output_ports, !is_target_input ? portResourceKey : "");

                        // 4. Apply Offsets
                        int target_idx = is_target_input ? in_idx : out_idx;

                        if (target_idx != -1) {
                            float node_h = std::max(48.0f, std::max(in_h, out_h)); // 48.0f is machine_h
                            float col_h  = is_target_input ? in_h : out_h;

                            // Vertical: Center of node -> Center of column -> Center of specific port
                            float col_offset_y = (node_h - col_h) * 0.5f;
                            float port_y_rel = pad_y + col_offset_y + target_idx * (port_h + space_y) + (port_h * 0.5f);

                            nodePos.y -= port_y_rel;

                            // Horizontal: If target is Output (Right side), shift node left by full width
                            if (!is_target_input) {
                                float node_w = pad_x + in_w + space_x + 48.0f + space_x + out_w + pad_x;
                                nodePos.x -= node_w;
                            }
                        }
                    }
                    executeCommand(std::make_unique<AddNodeCommand>(recipe.name, recipePair.first, fromPort, nodePos));
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleVar();

                ImGui::TableNextColumn();
                float contentWidth = 0.0f;
                int count = 0;
                for (const auto& p : recipe.output_ports) {
                    if (p.resource_key != "nothing" && resources.count(p.resource_key)) count++;
                }

                if (count > 0) {
                    contentWidth = (count * iconSize) + ((count - 1) * ImGui::GetStyle().ItemSpacing.x);

                    float avail = ImGui::GetContentRegionAvail().x;
                    float off = avail - contentWidth;
                    if (off > 0.0f) {
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
                    }

                    bool first = true;
                    for (auto it = recipe.output_ports.rbegin(); it != recipe.output_ports.rend(); ++it) {
                        const auto& p = *it;
                        if (p.resource_key == "nothing") continue;
                        auto resIt = resources.find(p.resource_key);
                        if (resIt != resources.end()) {
                            if (!first) ImGui::SameLine();
                            ImGui::Image(resIt->second.texture, ImVec2(iconSize, iconSize));
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", resIt->second.name.c_str());
                            first = false;
                        }
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
    ed::Resume();
}