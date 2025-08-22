#define IMGUI_DEFINE_MATH_OPERATORS
#include "FactoryNodeEditor.h"
#include "imgui.h"
#include "imgui_node_editor.h"
#include "imgui_internal.h"
#include "../utils/ProjectIo.h"
#include "../common/IdUtils.h"
#include "../common/CopyBuffer.h"

#include <unordered_map>
#include <string>
namespace ed = ax::NodeEditor;

FactoryNodeEditor::FactoryNodeEditor(const std::string &gameDataFilePath, const std::string &projectFilePath, const std::string &title)
    : name(title), projectFilePath(projectFilePath), gameDataFilePath(gameDataFilePath), m_contextNodeId(0), m_contextPinId(0), m_contextLinkId(0), undoRedoManager(100) {
    Initialize();
}

FactoryNodeEditor::~FactoryNodeEditor() {
    Close();
}

bool FactoryNodeEditor::Initialize() {
    try {
        graph = std::make_unique<FactoryGraph>(gameDataFilePath);
        solver = std::make_unique<FactorySolver>();

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

        // Initialize quadtree with large world bounds to handle extreme zoom levels
        float worldSize = 262144.0f; // 2^18, very large world
        float halfWorldSize = worldSize * 0.5f;

        quadtree::Box<float> worldBounds(
            quadtree::Vector2<float>(-halfWorldSize, -halfWorldSize),
            quadtree::Vector2<float>(worldSize, worldSize)
        );
        nodeQuadtree = std::make_unique<quadtree::Quadtree<NodeQuadtreeData, GetNodeBox>>(worldBounds);
        return true;
    } catch (const std::exception &e) {
        std::cerr << "Error initializing FactoryNodeEditor: " << e.what() << std::endl;
        return false;
    }
}

bool FactoryNodeEditor::Close() {
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
    first_frame = true;

    return true;
}

void FactoryNodeEditor::Draw() {
    if (!context) return;

    ed::SetCurrentEditor(context);

    DrawHeader();
    DrawToolbar();

    // Begin the node editor canvas
    windowPos = ImGui::GetWindowPos();
    windowSize = ImGui::GetWindowSize();
    HandleFirstFrame();
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
static long copyBufferSize = 0;

void FactoryNodeEditor::copy(CopyBuffer &copy_buffer) {
    std::vector<ed::NodeId> selectedNodes;
    selectedNodes.resize(ed::GetSelectedObjectCount());
    int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), selectedNodes.size());
    selectedNodes.resize(nodeCount);

    if (nodeCount == 0) return;

    copy_buffer.clear();

    copy_buffer.gameDataFilePath = this->gameDataFilePath; // Store game data file path

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
    copyBufferSize = copy_buffer.nodes.size() << 32 |
                     copy_buffer.ports.size() << 16 |
                     copy_buffer.connections.size();
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

static int drawnNodeCount = 0;
void FactoryNodeEditor::DrawHeader() {
    auto &io = ImGui::GetIO();
    ImGui::Text("FPS: %.2f (%.2gms)", io.Framerate, io.Framerate ? 1000.0f / io.Framerate : 0.0f);
    ImGui::Text("Nodes: %d, Connections: %d, Ports: %d", graph->getNodes().size(), graph->getConnections().size(), graph->getPorts().size());
    ImGui::Text("Copy Buffer Size: Nodes: %d, Ports: %d, Connections: % d",
                copyBufferSize >> 32, (copyBufferSize >> 16) & 0xFFFF, copyBufferSize & 0xFFFF);
    ImGui::Text("Selection size: %d", ed::GetSelectedObjectCount());

    // Debug info for quadtree
    if (nodeQuadtree) {
        auto bounds = nodeQuadtree->getBox();
        ImGui::Text("Quadtree bounds: (%.1f, %.1f) size: (%.1f, %.1f)",
                   bounds.getTopLeft().x, bounds.getTopLeft().y,
                   bounds.getSize().x, bounds.getSize().y);

        // Show current view bounds
        ImVec2 viewMin = windowPos;
        ImVec2 viewMax = viewMin + windowSize;
        ImVec2 canvasMin = ed::ScreenToCanvas(viewMin);
        ImVec2 canvasMax = ed::ScreenToCanvas(viewMax);
        ImGui::Text("View bounds: (%.1f, %.1f) to (%.1f, %.1f)", canvasMin.x, canvasMin.y, canvasMax.x, canvasMax.y);
        ImGui::Text("Visible nodes: %d / %d", drawnNodeCount, graph->getNodes().size());
        ImGui::Text("Undo Stack: %zu, Redo Stack: %zu", undoRedoManager.getUndoStackSize(), undoRedoManager.getRedoStackSize());
    }
    ImGui::Text("File path: %s", projectFilePath.c_str());

    ImGui::Separator();
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

void FactoryNodeEditor::HandleFirstFrame() {
    if (first_frame) {
        ProjectIO::LoadProject(projectFilePath, *graph);
        solver->solve(*graph);
        quadtreeNeedsRebuild = true;
        first_frame = false; // Reset after first frame
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

    drawnNodeCount = static_cast<int>(nodesToRegister.size());
    // Draw only visible nodes
    for (const auto& nodeData : nodesToRegister) {
        auto node = graph->getNode(nodeData);
        if (!node) continue;

        ed::NodeId nodeId = IdUtils::ToNodeId(node->id);
        ed::BeginNode(nodeId);
        ImGui::Text("%s", node->name.c_str());
        ImGui::BeginGroup(); // Group inputs/outputs
        int max_port_count = node->input_ports.size() > node->output_ports.size() ? node->input_ports.size() : node->output_ports.size();
        for (int i = 0; i < max_port_count; ++i) {
            if (i < node->input_ports.size()) {
                Port *p = graph->getPort(node->input_ports[i]);
                if (!p) continue; // Skip invalid ports
                ed::PinId pinId = IdUtils::ToPinId(p->id);
                ed::BeginPin(pinId, ed::PinKind::Input);
                ImGui::Text("<%.2f> %s",p->rate ,graph->getGameData().resources.at(p->resource_id).name.c_str()); // Display resource name
                ed::EndPin();
            } else {
                ImGui::Text(" "); // Empty space for alignment
            }
            ImGui::SameLine();
            if (i < node->output_ports.size()) {
                Port *p = graph->getPort(node->output_ports[i]);
                if (!p) continue; // Skip invalid ports
                ed::PinId pinId = IdUtils::ToPinId(p->id);
                ed::BeginPin(pinId, ed::PinKind::Output);
                ImGui::Text("%s <%.2f>",p->rate, graph->getGameData().resources.at(p->resource_id).name.c_str()); // Display resource name
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
    for (const auto &c: graph->getConnections()) {
        ed::Link(IdUtils::ToLinkId(c.id), IdUtils::ToPinId(c.from_port), IdUtils::ToPinId(c.to_port));
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
            int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), selectedNodes.size());
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
            ImGui::Text("Type: %s", toString(node->type));
            ImGui::Text("Machine ID: %d", node->machine_id);
            ImGui::Text("Selected Recipe ID: %d", node->selected_recipe_id);
            ImGui::Text("Power usage: %.2f MW", node->power_usage);
            ImGui::Text("Machine count: %.2f", node->machine_count);
            // Display input ports
            ImGui::Text("Input Ports:");
            for (int portId : node->input_ports) {
                auto port = graph->getPort(portId);
                if (port) {
                    ImGui::BulletText("Port ID: %d, Resource ID: %d, Rate: %.2f", port->id, port->resource_id, port->rate);
                }
            }
            // Display output ports
            ImGui::Text("Output Ports:");
            for (int portId : node->output_ports) {
                auto port = graph->getPort(portId);
                if (port) {
                    ImGui::BulletText("Port ID: %d, Resource ID: %d, Rate: %.2f", port->id, port->resource_id, port->rate);
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
            ImGui::Text("Resource ID: %d", port->resource_id);
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
                solver->solve(*graph);
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
            ImGui::Text("Resource ID: %d", connection->resource_id);
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
            auto resourceFilter = graph->getGameData().resources.at(graph->getPort(selected_port_id)->resource_id);
            bool fromInput = graph->getPort(selected_port_id)->isInput;
            for (const auto& recipe : graph->getGameData().recipes) {
                const auto& ports = fromInput ? recipe.output_ports : recipe.input_ports;
                for (const auto& port : ports) {
                    if (port.resource_id == resourceFilter.id) {
                        if (ImGui::Selectable(recipe.name.c_str())) {
                            executeCommand(std::make_unique<AddNodeCommand>(recipe.name, NodeType::PROCESSOR, recipe.id, selected_port_id, ed::ScreenToCanvas(m_storedPopupPosition)));
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
            }
        } else {
            for (const auto& recipe : graph->getGameData().recipes) {
                if (ImGui::Selectable(recipe.name.c_str())) {
                    executeCommand(std::make_unique<AddNodeCommand>(recipe.name, NodeType::PROCESSOR, recipe.id, -1, m_storedPopupPosition));
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }
    ed::Resume();
}