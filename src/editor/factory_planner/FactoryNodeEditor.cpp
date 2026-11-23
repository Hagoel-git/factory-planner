#define IMGUI_DEFINE_MATH_OPERATORS
#include "FactoryNodeEditor.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_node_editor.h"
#include "imgui_node_editor_internal.h"

#include <unordered_map>
#include <string>
#include <utility>
#include <cinttypes>
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>
#include <unordered_set>

#include "common/IdUtils.h"
#include "common/CopyBuffer.h"
#include "common/FilesystemUtils.h"
#include "services/IconCache.h"
#include "services/NotificationManager.h"
#include "services/ProjectIo.h"
#include "services/SettingsManager.h"
namespace ed = ax::NodeEditor;

namespace {
constexpr float kMaxCanvasCoord = 500000000.0f;
constexpr float kMaxNodeDimension = 10000.0f;

std::string sanitizeScalarFormat(std::string_view fmt, bool isInt, bool isS64) {
    if (fmt.empty()) {
        return isInt ? (isS64 ? "%lld" : "%d") : "%.3g";
    }

    int specCount = 0;
    std::string normalized;
    normalized.reserve(fmt.size() + 4);

    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') {
            normalized.push_back(fmt[i]);
            continue;
        }
        if (i + 1 < fmt.size() && fmt[i + 1] == '%') {
            normalized += "%%";
            ++i;
            continue;
        }

        // Single format conversion: %[flags][width][.prec][length]spec
        size_t start = i;
        ++i;
        while (i < fmt.size() &&
               (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#' || fmt[i] == '0')) {
            ++i;
        }
        while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i]))) {
            ++i;
        }
        if (i < fmt.size() && fmt[i] == '.') {
            ++i;
            while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i]))) {
                ++i;
            }
        }
        size_t lenStart = i;
        while (i < fmt.size() && fmt[i] == 'l') {
            ++i;
        }
        size_t lenCount = i - lenStart;
        if (i >= fmt.size() || lenCount > 2) {
            return isInt ? (isS64 ? "%lld" : "%d") : "%.3g";
        }

        char conv = fmt[i];
        ++specCount;
        if (specCount > 1) {
            return isInt ? (isS64 ? "%lld" : "%d") : "%.3g";
        }

        if (isInt) {
            if (conv != 'd' && conv != 'i') {
                return isS64 ? "%lld" : "%d";
            }
            normalized.append(fmt.substr(start, lenStart - start));
            if (isS64) {
                normalized += "ll";
            }
            normalized.push_back(conv);
        } else {
            if (conv != 'f' && conv != 'F' && conv != 'g' && conv != 'G' && conv != 'e' && conv != 'E') {
                return "%.3g";
            }
            if (lenCount > 1) {
                return "%.3g";
            }
            normalized.append(fmt.substr(start, i - start + 1));
        }
    }

    if (specCount != 1) {
        return isInt ? (isS64 ? "%lld" : "%d") : "%.3g";
    }
    return normalized;
}

std::string formatDoubleScalar(std::string_view fmt, double val) {
    std::string safeFmt = sanitizeScalarFormat(fmt, false, false);
    char buf[128];
    snprintf(buf, sizeof(buf), safeFmt.c_str(), val);
    return std::string(buf);
}

std::string formatMetricValueString(const gamedata::MetricDef& mdef, double canonicalVal, const FactoryGraph& graph) {
    double dispVal = mdef.is_rate ? graph.toDisplayRate(canonicalVal) : canonicalVal;
    std::string out = formatDoubleScalar(mdef.format.empty() ? "%.3g" : mdef.format, dispVal);
    std::string unitStr = mdef.unit;
    if (mdef.is_rate) {
        unitStr += gamedata::rateUnitSuffix(graph.getRateUnit());
    }
    if (!unitStr.empty()) {
        if (unitStr != "%" && unitStr.front() != '/') {
            out += ' ';
        }
        out += unitStr;
    }
    return out;
}

bool metricHasDisplayLocation(const gamedata::MetricDef& mdef, gamedata::MetricDisplayLocation loc) {
    return std::find(mdef.display.begin(), mdef.display.end(), loc) != mdef.display.end();
}

const gamedata::Profile* findNodeProfile(const GameData& gameData, const Node& node) {
    const Machine* machine = gameData.findMachine(node.machine_key);
    if (machine) {
        if (const auto* prof = gameData.findProfile(machine->profile)) {
            return prof;
        }
    }
    return gameData.findProfile("default");
}

std::vector<const gamedata::ParameterDef*> collectProfileParamDefs(const GameData& gameData, const Node& node) {
    std::vector<const gamedata::ParameterDef*> result;
    const gamedata::Profile* profile = findNodeProfile(gameData, node);
    if (!profile) {
        return result;
    }
    result.reserve(profile->params.size());
    for (const auto& pid : profile->params) {
        if (const auto* pdef = gameData.findParameter(pid)) {
            result.push_back(pdef);
        }
    }
    return result;
}

gamedata::ParamValue defaultFallbackParamValue(const gamedata::ParameterDef& pdef) {
    if (pdef.has_default) {
        return pdef.default_value;
    }
    switch (pdef.type) {
        case gamedata::ParameterType::Number:
        case gamedata::ParameterType::Int:
            return gamedata::ParamValue::number(0.0);
        case gamedata::ParameterType::Bool:
            return gamedata::ParamValue::boolean(false);
        case gamedata::ParameterType::Enum:
        case gamedata::ParameterType::ResourceChoice:
            return gamedata::ParamValue::null();
        case gamedata::ParameterType::Slots:
            return gamedata::ParamValue::slots({});
    }
    return gamedata::ParamValue::null();
}

gamedata::ParamValue resolveEffectiveParamValue(
    const Node& node,
    const gamedata::NodeEval* eval,
    const gamedata::ParameterDef& pdef) {
    if (eval) {
        if (auto it = eval->resolved_params.find(pdef.id); it != eval->resolved_params.end()) {
            return it->second;
        }
    }
    if (auto it = node.params.find(pdef.id); it != node.params.end()) {
        return it->second;
    }
    return defaultFallbackParamValue(pdef);
}

gamedata::ParamValue resolveEffectiveGlobalValue(
    const FactoryGraph& graph,
    const gamedata::ParameterDef& gdef) {
    const auto& resolved = graph.getResolvedGlobals();
    if (auto it = resolved.resolved_values.find(gdef.id); it != resolved.resolved_values.end()) {
        return it->second;
    }
    const auto& stored = graph.getGlobals();
    if (auto it = stored.find(gdef.id); it != stored.end()) {
        return it->second;
    }
    return defaultFallbackParamValue(gdef);
}

gamedata::UiWidget normalizeWidgetForType(
    gamedata::ParameterType type,
    gamedata::UiWidget requested,
    bool hasBoundedRange) {
    switch (type) {
        case gamedata::ParameterType::Number:
            if (requested == gamedata::UiWidget::Slider) {
                return hasBoundedRange ? gamedata::UiWidget::Slider : gamedata::UiWidget::Drag;
            }
            if (requested == gamedata::UiWidget::Drag ||
                requested == gamedata::UiWidget::Input ||
                requested == gamedata::UiWidget::Stepper) {
                return requested;
            }
            return gamedata::UiWidget::Drag;
        case gamedata::ParameterType::Int:
            if (requested == gamedata::UiWidget::Slider) {
                return hasBoundedRange ? gamedata::UiWidget::Slider : gamedata::UiWidget::Stepper;
            }
            if (requested == gamedata::UiWidget::Stepper ||
                requested == gamedata::UiWidget::Drag ||
                requested == gamedata::UiWidget::Input) {
                return requested;
            }
            return gamedata::UiWidget::Stepper;
        case gamedata::ParameterType::Bool:
            return gamedata::UiWidget::Checkbox;
        case gamedata::ParameterType::Enum:
        case gamedata::ParameterType::ResourceChoice:
            return gamedata::UiWidget::Combo;
        case gamedata::ParameterType::Slots:
            return gamedata::UiWidget::SlotRow;
    }
    return gamedata::defaultWidgetForParameterType(type);
}

std::string getOptionDisplayLabel(const gamedata::OptionSet* os, std::string_view key) {
    if (os) {
        if (const auto* opt = os->findOption(key)) {
            if (!opt->label.empty()) return opt->label;
        }
    }
    return std::string(key);
}

std::string getResourceDisplayName(const GameData& gameData, std::string_view resId) {
    if (const Resource* res = gameData.findResource(resId)) {
        if (!res->name.empty()) return res->name;
    }
    return std::string(resId);
}

const char* evalStatusLabel(gamedata::EvalStatus st) {
    switch (st) {
        case gamedata::EvalStatus::OK: return "OK";
        case gamedata::EvalStatus::WARNING: return "WARNING";
        case gamedata::EvalStatus::BLOCKED: return "BLOCKED";
        case gamedata::EvalStatus::ERROR: return "ERROR";
    }
    return "OK";
}

ImVec4 evalStatusColor(gamedata::EvalStatus st) {
    switch (st) {
        case gamedata::EvalStatus::OK: return ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
        case gamedata::EvalStatus::WARNING: return ImVec4(0.95f, 0.72f, 0.20f, 1.0f);
        case gamedata::EvalStatus::BLOCKED: return ImVec4(0.95f, 0.45f, 0.20f, 1.0f);
        case gamedata::EvalStatus::ERROR: return ImVec4(0.95f, 0.28f, 0.28f, 1.0f);
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}
}

FactoryNodeEditor::FactoryNodeEditor(const GameData& game_data, const std::filesystem::path &projectFilePath, std::string title)
    : m_editorName(std::move(title)), m_projectFilePath(projectFilePath), m_selectedPortId(static_cast<uint64_t>(-1)), m_contextNodeId(0), m_contextPinId(0),
      m_contextLinkId(0), m_undoRedoManager(SettingsManager::instance().getSettings().maxUndoHistory) {
    try {
        VLOG(1) << "Initializing FactoryNodeEditor for project: " << projectFilePath;
        m_graph = std::make_unique<FactoryGraph>(game_data);
        m_solver = std::make_unique<FactorySolver>();

        m_nextAutosaveTime = std::chrono::steady_clock::now() +
                           std::chrono::minutes(std::max(1, SettingsManager::instance().getSettings().autoSaveIntervalMinutes));

        ed::Config cfg = ed::Config();
        cfg.DisableInternalFitView = true;
        cfg.AutoSaveEnabled = false;
        cfg.SettingsFile = nullptr;
        cfg.SaveSettings = nullptr;
        cfg.LoadSettings = nullptr;
        cfg.SaveNodeSettings = nullptr;
        cfg.LoadNodeSettings = nullptr;
        cfg.UserPointer = nullptr;
        cfg.NavigateButtonIndex = SettingsManager::instance().getSettings().navigationButtonIndex;
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
            if (!ProjectIO::loadProject(projectFilePath, *m_graph)) {
                m_loadedSuccessfully = false;
            }
        }

        // Register package with IconCache and build RecipeSearchIndex after final GameData is loaded.
        m_iconPackageId = IconCache::instance().registerPackage(m_graph->getGameData());
        {
            const auto indexStart = std::chrono::steady_clock::now();
            m_recipeSearchIndex = gamedata::RecipeSearchIndex::build(m_graph->getGameData());
            const double indexMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - indexStart).count();
            LOG(INFO) << "Built RecipeSearchIndex for " << m_recipeSearchIndex.recipeCount()
                      << " recipes in " << indexMs << " ms.";
        }

        // Initialize quadtree dynamically based on graph nodes or default minimum size
        rebuildQuadtree();

        FactorySolver::SolverResult result = m_solver->solve(*m_graph);
        m_debugInfo.lastTotalSolveDurationMs = result.total_solve_time_ms;
        m_debugInfo.lastSetupSolveDurationMs = result.setup_time_ms;
        m_debugInfo.lastSolverDurationMs = result.solve_time_ms;
        m_debugInfo.lastUpdateFactoryDurationMs = result.update_factory_time_ms;
        m_debugInfo.lastSolverResult = result.status;
        m_debugInfo.lastSolverStatus = m_solver->getLastSolverStatus();

        m_quadtreeNeedsRebuild = true;
        if (m_loadedSuccessfully) {
            LOG(INFO) << "FactoryNodeEditor initialized successfully for project: " << projectFilePath;
        } else {
            LOG(WARNING) << "FactoryNodeEditor failed to load project: " << projectFilePath;
        }
    } catch (const std::exception &e) {
        m_loadedSuccessfully = false;
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
    m_contextNodeId = 0;
    m_contextPinId = 0;
    m_contextLinkId = 0;
    m_quadtreeNeedsRebuild = false;
    LOG(INFO) << "FactoryNodeEditor destroyed for project: " << m_projectFilePath;
}

void FactoryNodeEditor::draw() {
    if (!m_context) return;

    m_isFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    if (!m_autosaveSuspended && std::chrono::steady_clock::now() > m_nextAutosaveTime && SettingsManager::instance().getSettings().autoSaveEnabled) {
        if (m_isDirty && !m_projectFilePath.empty()) {
            LOG(INFO) << "Auto-saved project: " << m_projectFilePath;
            save();
        }
        m_nextAutosaveTime = std::chrono::steady_clock::now() +
                           std::chrono::minutes(std::max(1, SettingsManager::instance().getSettings().autoSaveIntervalMinutes));
    }

    ed::SetCurrentEditor(m_context);

    updateDebugInfo();

    // Begin the node editor canvas (and leave room for collapsible side panel when wide enough)
    ImVec2 avail = ImGui::GetContentRegionAvail();
    bool canShowSideBar = avail.x > 360.0f;
    float sideWidth = !canShowSideBar ? 0.0f : (m_showSidePanel ? std::min(265.0f, std::max(210.0f, avail.x * 0.26f)) : 30.0f);
    float spacingX = canShowSideBar ? ImGui::GetStyle().ItemSpacing.x : 0.0f;
    ImVec2 canvasSize(canShowSideBar ? std::max(100.0f, avail.x - sideWidth - spacingX) : std::max(4.0f, avail.x),
                      std::max(4.0f, avail.y));

    m_windowPos = ImGui::GetCursorScreenPos();
    m_windowSize = canvasSize;

    ed::Begin(m_editorName.c_str(), canvasSize);

    // Check if any node is placed or dragged outside current quadtree bounds
    if (!m_quadtreeNeedsRebuild && m_nodeQuadtree) {
        auto bounds = m_nodeQuadtree->getBox();
        for (const auto &node : m_graph->getNodes()) {
            ed::NodeId nodeId = IdUtils::toNodeId(node.id);
            ImVec2 nodePos = ed::GetNodePosition(nodeId);
            if (!std::isfinite(nodePos.x) || !std::isfinite(nodePos.y) ||
                std::abs(nodePos.x) >= kMaxCanvasCoord || std::abs(nodePos.y) >= kMaxCanvasCoord) {
                m_quadtreeNeedsRebuild = true;
                break;
            }
            ImVec2 nodeSize = ed::GetNodeSize(nodeId);
            if (!std::isfinite(nodeSize.x) || !std::isfinite(nodeSize.y) ||
                nodeSize.x <= 0.0f || nodeSize.y <= 0.0f ||
                nodeSize.x > kMaxNodeDimension || nodeSize.y > kMaxNodeDimension) {
                nodeSize = ImVec2(300.0f, 100.0f);
            }
            quadtree::Box<float> nodeBox(quadtree::Vector2<float>(nodePos.x, nodePos.y),
                                         quadtree::Vector2<float>(nodeSize.x, nodeSize.y));
            if (!bounds.contains(nodeBox)) {
                m_quadtreeNeedsRebuild = true;
                break;
            }
        }
    }

    // Rebuild quadtree if needed
    if (m_quadtreeNeedsRebuild || !m_nodeQuadtree) {
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

    std::vector<uint64_t> selectedNodeIds;
    if (int selCount = ed::GetSelectedObjectCount(); selCount > 0) {
        std::vector<ed::NodeId> selEd(static_cast<size_t>(selCount));
        int numNodes = ed::GetSelectedNodes(selEd.data(), selCount);
        selectedNodeIds.reserve(static_cast<size_t>(std::max(0, numNodes)));
        for (int i = 0; i < numNodes; ++i) {
            selectedNodeIds.push_back(IdUtils::fromNodeId(selEd[i]));
        }
    }

    ed::End(); // End node editor

    ed::SetCurrentEditor(nullptr);

    if (canShowSideBar) {
        ImGui::SameLine();
        drawSidePanel(ImVec2(sideWidth, canvasSize.y), selectedNodeIds);
    } else if (m_activeParamEdit.active && m_activeParamEdit.isGlobal) {
        commitActiveParamEdit();
    }
}

bool FactoryNodeEditor::save() {
    commitPendingEdits();
    if (!m_loadedSuccessfully || m_projectFilePath.empty()) {
        LOG(WARNING) << "Save refused: editor was not loaded successfully or path is empty: " << m_projectFilePath;
        return false;
    }
    if (ProjectIO::saveProject(m_projectFilePath, *m_graph, this->m_context)) {
        m_isDirty = false;
        return true;
    }
    return false;
}

bool FactoryNodeEditor::saveAs(const std::filesystem::path &newFilePath, SaveAsMode mode) {
    if (newFilePath.empty()) {
        LOG(ERROR) << "SaveAs failed: path is empty";
        return false;
    }
    std::filesystem::path resolvedPath = ensureProjectExtension(newFilePath);
    if (!m_loadedSuccessfully && resolvedPath == m_projectFilePath) {
        LOG(WARNING) << "SaveAs refused: cannot overwrite corrupted project file: " << resolvedPath;
        return false;
    }
    commitPendingEdits();
    if (ProjectIO::saveProject(resolvedPath, *m_graph, this->m_context)) {
        if (mode == SaveAsMode::SwitchToNewFile) {
            m_projectFilePath = resolvedPath;
            m_editorName = resolvedPath.stem().string();
            m_isDirty = false;
            m_loadedSuccessfully = true;
        }
        LOG(INFO) << "Project saved as: " << resolvedPath;
        return true;
    }
    LOG(ERROR) << "Failed to save project as: " << resolvedPath;
    return false;
}

void FactoryNodeEditor::copy(CopyBuffer &copy_buffer) {
    std::vector<ed::NodeId> selectedNodes;
    selectedNodes.resize(ed::GetSelectedObjectCount());
    int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));
    selectedNodes.resize(nodeCount);

    if (nodeCount == 0) return;

    commitPendingEdits();
    copy_buffer.clear();

    copy_buffer.gameId = m_graph->getGameData().game.id;
    copy_buffer.gameName = m_graph->getGameData().game.name;
    copy_buffer.sourceEditor = this;

    for (const auto &nodeId : selectedNodes) {
        uint64_t nodeIdInt = IdUtils::fromNodeId(nodeId);
        auto node = m_graph->getNode(nodeIdInt);
        if (!node) continue; // Skip invalid nodes

        copy_buffer.nodes[nodeIdInt] = *node; // Copy node
        ImVec2 pos = ed::GetNodePosition(nodeId);
        if (!std::isfinite(pos.x) || !std::isfinite(pos.y) ||
            std::abs(pos.x) >= kMaxCanvasCoord || std::abs(pos.y) >= kMaxCanvasCoord) {
            pos = ImVec2(0.0f, 0.0f);
        }
        copy_buffer.nodePositions[nodeIdInt] = pos; // Store position

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
    ed::ClearSelection();
    VLOG(1) << "Cut " << copyBuffer.nodes.size() << " nodes to clipboard. At " << m_editorName;
}

void FactoryNodeEditor::paste(const CopyBuffer &copy_buffer, bool mapExternalConnections) {
    if (copy_buffer.isEmpty()) return;

    const bool gameIdMismatch = !copy_buffer.gameId.empty() &&
                                !m_graph->getGameData().game.id.empty() &&
                                copy_buffer.gameId != m_graph->getGameData().game.id;
    const bool gameNameMismatch = copy_buffer.gameId.empty() &&
                                  copy_buffer.gameName != m_graph->getGameData().game.name;
    if (gameIdMismatch || gameNameMismatch) {
        NotificationManager::instance().addNotification(
            "Paste Failed",
            "Cannot paste: Game mismatch ('" + copy_buffer.gameName + "' vs '" + m_graph->getGameData().game.name + "')",
            NotificationType::Warning
        );
        return;
    }

    if (copy_buffer.sourceEditor != this) {
        mapExternalConnections = false;
    }

    CopyBuffer filteredBuffer;
    filteredBuffer.gameId = copy_buffer.gameId;
    filteredBuffer.gameName = copy_buffer.gameName;
    filteredBuffer.sourceEditor = copy_buffer.sourceEditor;

    const auto& gameData = m_graph->getGameData();
    std::unordered_set<uint64_t> validNodeIds;
    int skippedNodes = 0;

    for (const auto& [id, node] : copy_buffer.nodes) {
        const Recipe* recipePtr = gameData.findRecipe(node.selected_recipe_key);

        if (recipePtr) {
            const Recipe& recipe = *recipePtr;
            bool machineValid = !node.machine_key.empty() &&
                                std::find(recipe.compatible_machine_ids.begin(),
                                          recipe.compatible_machine_ids.end(),
                                          node.machine_key) != recipe.compatible_machine_ids.end();

            filteredBuffer.nodes[id] = node;
            if (!machineValid) {
                if (!recipe.compatible_machine_ids.empty()) {
                    filteredBuffer.nodes[id].machine_key = m_graph->resolvePreferredMachine(recipe.compatible_machine_ids);
                } else {
                    filteredBuffer.nodes[id].machine_key = "";
                }
            }
            filteredBuffer.nodePositions[id] = copy_buffer.nodePositions.count(id)
                                                   ? copy_buffer.nodePositions.at(id)
                                                   : ImVec2(0.0f, 0.0f);
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
            NotificationManager::instance().addNotification("Paste Failed", "All " + std::to_string(skippedNodes) + " copied nodes contained invalid recipes/machines for this game version.", NotificationType::Warning);
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
    ed::SetCurrentEditor(m_context);
    m_flowAnimationUntil = glfwGetTime() + static_cast<double>(ed::GetStyle().FlowDuration);
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

    ed::SetCurrentEditor(m_context);
    m_navigationAnimationUntil = glfwGetTime() + static_cast<double>(ed::GetStyle().ScrollDuration);

    if (!allNodes.empty()) {
        ImRect contentBounds;
        bool isFirstNode = true;

        for (const auto& node : allNodes) {
            ed::NodeId nodeId = IdUtils::toNodeId(node.id);
            ImVec2 nodePos = ed::GetNodePosition(nodeId);
            if (!std::isfinite(nodePos.x) || !std::isfinite(nodePos.y) ||
                std::abs(nodePos.x) >= kMaxCanvasCoord || std::abs(nodePos.y) >= kMaxCanvasCoord) {
                continue;
            }
            ImVec2 nodeSize = ed::GetNodeSize(nodeId);

            // A node that has never been drawn will have a size of (0,0).
            // We'll use a default fallback size to ensure it's included in the bounds.
            if (!std::isfinite(nodeSize.x) || !std::isfinite(nodeSize.y) ||
                nodeSize.x <= 0.0f || nodeSize.y <= 0.0f ||
                nodeSize.x > kMaxNodeDimension || nodeSize.y > kMaxNodeDimension) {
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

        if (!isFirstNode) {
            // Add some padding so nodes aren't right at the edge of the view
            const float padding = 100.0f;
            contentBounds.Min.x -= padding;
            contentBounds.Min.y -= padding;
            contentBounds.Max.x += padding;
            contentBounds.Max.y += padding;

            ed::NavigateTo(contentBounds);
        }
    } else {
        ed::NavigateToContent();
    }
}

void FactoryNodeEditor::undo() {
    commitPendingEdits();
    m_isContextMenuInitialized = false;
    if (!m_undoRedoManager.canUndo()) return;

    const Command *command = m_undoRedoManager.getCommandToUndo();
    if (!command) return;

    CommandFlags flags = command->getFlags();

    m_undoRedoManager.undo(*m_graph);
    m_isDirty = true;
    VLOG(1) << "Undo command '" << command->getDescription() << "' executed in editor: " << m_editorName;

    if (flags.needsSolve) {
        FactorySolver::SolverResult result = m_solver->solve(*m_graph);
        m_debugInfo.lastTotalSolveDurationMs = result.total_solve_time_ms;
        m_debugInfo.lastSetupSolveDurationMs = result.setup_time_ms;
        m_debugInfo.lastSolverDurationMs = result.solve_time_ms;
        m_debugInfo.lastUpdateFactoryDurationMs = result.update_factory_time_ms;
        m_debugInfo.lastSolverResult = result.status;
        m_debugInfo.lastSolverStatus = m_solver->getLastSolverStatus();
    }
    m_quadtreeNeedsRebuild |= flags.needsRebuild;
}

void FactoryNodeEditor::redo() {
    commitPendingEdits();
    m_isContextMenuInitialized = false;
    if (!m_undoRedoManager.canRedo()) return;

    const Command *command = m_undoRedoManager.getCommandToRedo();
    if (!command) return;

    CommandFlags flags = command->getFlags();

    m_undoRedoManager.redo(*m_graph);
    m_isDirty = true;
    VLOG(1) << "Redo command '" << command->getDescription() << "' executed in editor: " << m_editorName;

    if (flags.needsSolve) {
        FactorySolver::SolverResult result = m_solver->solve(*m_graph);
        m_debugInfo.lastTotalSolveDurationMs = result.total_solve_time_ms;
        m_debugInfo.lastSetupSolveDurationMs = result.setup_time_ms;
        m_debugInfo.lastSolverDurationMs = result.solve_time_ms;
        m_debugInfo.lastUpdateFactoryDurationMs = result.update_factory_time_ms;
        m_debugInfo.lastSolverResult = result.status;
        m_debugInfo.lastSolverStatus = m_solver->getLastSolverStatus();
    }
    m_quadtreeNeedsRebuild |= flags.needsRebuild;
}


void FactoryNodeEditor::executeCommand(std::unique_ptr<Command> command) {
    if (!command) return;
    commitActiveParamEdit();
    // check if it is a composite command and if it has no sub-commands, then ignore it
    if (auto compositeCommand = dynamic_cast<CompositeCommand*>(command.get())) {
        if (compositeCommand->isEmpty()) {
            return;
        }
    }
    CommandFlags flags = command->getFlags();
    m_undoRedoManager.executeCommand(std::move(command), *m_graph);
    m_isDirty = true;
    if (flags.needsSolve) {
        FactorySolver::SolverResult result = m_solver->solve(*m_graph);
        m_debugInfo.lastTotalSolveDurationMs = result.total_solve_time_ms;
        m_debugInfo.lastSetupSolveDurationMs = result.setup_time_ms;
        m_debugInfo.lastSolverDurationMs = result.solve_time_ms;
        m_debugInfo.lastUpdateFactoryDurationMs = result.update_factory_time_ms;
        m_debugInfo.lastSolverResult = result.status;
        m_debugInfo.lastSolverStatus = m_solver->getLastSolverStatus();
    }
    m_quadtreeNeedsRebuild |= flags.needsRebuild;
}

void FactoryNodeEditor::updateDebugInfo() {
    m_debugInfo.filePath = m_projectFilePath;
    m_debugInfo.gameDataPath = m_graph->getGameData().manifest_path;
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
    float maxAbsCoord = 0.0f;

    std::vector<NodeQuadtreeData> items;
    items.reserve(m_graph->getNodes().size());

    // Add all nodes to quadtree with finite coordinate checks
    for (const auto &node : m_graph->getNodes()) {
        ed::NodeId nodeId = IdUtils::toNodeId(node.id);
        ImVec2 nodePos = ed::GetNodePosition(nodeId);
        if (!std::isfinite(nodePos.x) || !std::isfinite(nodePos.y) ||
            std::abs(nodePos.x) >= kMaxCanvasCoord || std::abs(nodePos.y) >= kMaxCanvasCoord) {
            nodePos = ImVec2(0.0f, 0.0f);
            ed::SetNodePosition(nodeId, nodePos);
        }
        ImVec2 nodeSize = ed::GetNodeSize(nodeId);
        if (!std::isfinite(nodeSize.x) || !std::isfinite(nodeSize.y) ||
            nodeSize.x <= 0.0f || nodeSize.y <= 0.0f ||
            nodeSize.x > kMaxNodeDimension || nodeSize.y > kMaxNodeDimension) {
            nodeSize = ImVec2(300.0f, 100.0f); // Default size if not provided
        }
        maxAbsCoord = std::max({maxAbsCoord,
                                std::abs(nodePos.x),
                                std::abs(nodePos.y),
                                std::abs(nodePos.x + nodeSize.x),
                                std::abs(nodePos.y + nodeSize.y)});
        items.emplace_back(node.id, nodePos, nodeSize);
    }

    const float margin = 2048.0f;
    float halfWorldSize = 131072.0f; // 2^17 minimum half-size (minimum 262,144 units total)
    while (halfWorldSize <= maxAbsCoord + margin && halfWorldSize < (1.0f * (1ULL << 30))) {
        halfWorldSize *= 2.0f;
    }
    float worldSize = halfWorldSize * 2.0f;

    quadtree::Box<float> worldBounds(
        quadtree::Vector2<float>(-halfWorldSize, -halfWorldSize),
        quadtree::Vector2<float>(worldSize, worldSize)
    );
    m_nodeQuadtree = std::make_unique<quadtree::Quadtree<NodeQuadtreeData, GetNodeBox>>(worldBounds);

    for (const auto &item : items) {
        m_nodeQuadtree->add(item);
    }
}

std::vector<NodeQuadtreeData> FactoryNodeEditor::getVisibleNodes(const ImVec2& viewMin, const ImVec2& viewMax) {
    if (!m_nodeQuadtree) {
        return {};
    }

    if (!std::isfinite(viewMin.x) || !std::isfinite(viewMin.y) ||
        !std::isfinite(viewMax.x) || !std::isfinite(viewMax.y)) {
        return {};
    }

    // Add some padding to the view to catch nodes that are partially visible
    const float padding = 100.0f;
    const auto qtBox = m_nodeQuadtree->getBox();

    // Clamp the query box to the quadtree bounds to avoid floating-point overflow on extreme zooms
    float minX = std::max(viewMin.x - padding, qtBox.left);
    float minY = std::max(viewMin.y - padding, qtBox.top);
    float maxX = std::min(viewMax.x + padding, qtBox.getRight());
    float maxY = std::min(viewMax.y + padding, qtBox.getBottom());

    if (maxX <= minX || maxY <= minY) {
        return {};
    }

    quadtree::Box<float> viewBox(minX, minY, maxX - minX, maxY - minY);
    return m_nodeQuadtree->query(viewBox);
}

gamedata::EvaluatedParamUi FactoryNodeEditor::resolveEffectiveParamUi(
    const GameData& gameData,
    const gamedata::NodeEval* eval,
    const gamedata::ParameterDef& pdef) {
    if (eval) {
        if (auto it = eval->param_ui.find(pdef.id); it != eval->param_ui.end()) {
            return it->second;
        }
    }
    gamedata::EvaluatedParamUi fallback;
    bool hasEvalVisible = false;
    if (eval) {
        if (auto vit = eval->param_visible.find(pdef.id); vit != eval->param_visible.end()) {
            fallback.visible = vit->second;
            hasEvalVisible = true;
        }
    }
    if (!hasEvalVisible && pdef.visible_if.is_present && pdef.visible_if.is_literal) {
        fallback.visible = pdef.visible_if.literal_value;
    }
    if (pdef.min.is_present && pdef.min.is_literal) {
        fallback.has_min = true;
        fallback.min_val = pdef.min.literal_value;
    }
    if (pdef.max.is_present && pdef.max.is_literal) {
        fallback.has_max = true;
        fallback.max_val = pdef.max.literal_value;
    }
    if (pdef.type == gamedata::ParameterType::Int) {
        if (fallback.has_min) fallback.min_val = std::ceil(fallback.min_val - 1e-9);
        if (fallback.has_max) fallback.max_val = std::floor(fallback.max_val + 1e-9);
    }
    if (fallback.has_min && fallback.has_max && fallback.min_val > fallback.max_val) {
        fallback.has_min = false;
        fallback.has_max = false;
        fallback.min_val = 0.0;
        fallback.max_val = 0.0;
    }
    if (pdef.type == gamedata::ParameterType::Slots && pdef.count.is_literal) {
        int64_t c = std::llround(pdef.count.literal_value);
        fallback.slot_count = static_cast<size_t>(std::clamp<int64_t>(c, 0, 64));
    }
    const bool allowAllByLiteral = !pdef.allowed_if.is_present ||
        (pdef.allowed_if.is_literal && pdef.allowed_if.literal_value);
    if (allowAllByLiteral) {
        if (pdef.type == gamedata::ParameterType::Enum || pdef.type == gamedata::ParameterType::Slots) {
            if (const auto* os = gameData.findOptionSet(pdef.option_set)) {
                for (const auto& opt : os->options) {
                    fallback.allowed_options.push_back(opt.key);
                }
            }
        } else if (pdef.type == gamedata::ParameterType::ResourceChoice) {
            fallback.allowed_resources = pdef.candidate_resources;
        }
    }
    return fallback;
}

gamedata::EvaluatedParamUi FactoryNodeEditor::resolveEffectiveGlobalUi(
    const GameData& gameData,
    const gamedata::ResolvedGlobals& resolvedGlobals,
    const gamedata::ParameterDef& gdef) {
    if (auto it = resolvedGlobals.param_ui.find(gdef.id); it != resolvedGlobals.param_ui.end()) {
        return it->second;
    }
    gamedata::EvaluatedParamUi fallback = resolveEffectiveParamUi(gameData, nullptr, gdef);
    if (auto vit = resolvedGlobals.visible.find(gdef.id); vit != resolvedGlobals.visible.end()) {
        fallback.visible = vit->second;
    }
    return fallback;
}

bool FactoryNodeEditor::segmentIntersectsAABB(ImVec2 p1, ImVec2 p2, ImVec2 boxMin, ImVec2 boxMax) {
    if (!std::isfinite(p1.x) || !std::isfinite(p1.y) ||
        !std::isfinite(p2.x) || !std::isfinite(p2.y) ||
        !std::isfinite(boxMin.x) || !std::isfinite(boxMin.y) ||
        !std::isfinite(boxMax.x) || !std::isfinite(boxMax.y)) {
        return false;
    }

    if (boxMin.x > boxMax.x) std::swap(boxMin.x, boxMax.x);
    if (boxMin.y > boxMax.y) std::swap(boxMin.y, boxMax.y);

    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;

    float tMin = 0.0f;
    float tMax = 1.0f;

    // X axis slab
    if (std::abs(dx) < 1e-7f) {
        if (p1.x < boxMin.x || p1.x > boxMax.x) {
            return false;
        }
    } else {
        float invDx = 1.0f / dx;
        float t1 = (boxMin.x - p1.x) * invDx;
        float t2 = (boxMax.x - p1.x) * invDx;
        if (t1 > t2) std::swap(t1, t2);
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMin > tMax) return false;
    }

    // Y axis slab
    if (std::abs(dy) < 1e-7f) {
        if (p1.y < boxMin.y || p1.y > boxMax.y) {
            return false;
        }
    } else {
        float invDy = 1.0f / dy;
        float t1 = (boxMin.y - p1.y) * invDy;
        float t2 = (boxMax.y - p1.y) * invDy;
        if (t1 > t2) std::swap(t1, t2);
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMin > tMax) return false;
    }

    return true;
}

void FactoryNodeEditor::drawNodes() {
    auto* detailContext = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(m_context);

    ImVec2 screenMin = m_windowPos;
    ImVec2 screenMax = m_windowPos + m_windowSize;
    ImVec2 s2cMin = ed::ScreenToCanvas(screenMin);
    ImVec2 s2cMax = ed::ScreenToCanvas(screenMax);

    // Get the visible canvas area directly from node editor context if available
    ImVec2 canvasMin = s2cMin;
    ImVec2 canvasMax = s2cMax;
    if (detailContext) {
        ImRect vr = detailContext->GetViewRect();
        canvasMin = vr.Min;
        canvasMax = vr.Max;
    }
    if (canvasMin.x > canvasMax.x) std::swap(canvasMin.x, canvasMax.x);
    if (canvasMin.y > canvasMax.y) std::swap(canvasMin.y, canvasMax.y);

    // Get visible nodes from quadtree
    auto visibleNodes = getVisibleNodes(canvasMin, canvasMax);

    // Build set of visible node ids
    std::unordered_set<uint64_t> visibleNodeIds;
    visibleNodeIds.reserve(visibleNodes.size());
    for (const auto &nd : visibleNodes) visibleNodeIds.insert(nd.nodeId);

    // Keep dragged node and selected nodes visible so they don't disappear mid-drag
    if (m_draggingNodes) {
        if (m_draggedNodeId != ed::NodeId::Invalid) {
            visibleNodeIds.insert(IdUtils::fromNodeId(m_draggedNodeId));
        }
        std::vector<ed::NodeId> selectedNodes(ed::GetSelectedObjectCount());
        int selCount = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));
        for (int i = 0; i < selCount; ++i) {
            if (selectedNodes[i] != ed::NodeId::Invalid) {
                visibleNodeIds.insert(IdUtils::fromNodeId(selectedNodes[i]));
            }
        }
    }

    // Keep node hosting the active connection creation pin visible so connection drag isn't aborted
    if (detailContext) {
        auto& itemCreator = detailContext->GetItemCreator();
        if (itemCreator.m_IsActive) {
            ax::NodeEditor::Detail::Pin* activePin = itemCreator.m_DraggedPin ? itemCreator.m_DraggedPin : itemCreator.m_LinkStart;
            if (activePin) {
                if (activePin->m_Node) {
                    visibleNodeIds.insert(IdUtils::fromNodeId(activePin->m_Node->m_ID));
                }
                auto port = m_graph->getPort(IdUtils::fromPinId(activePin->m_ID));
                if (port) {
                    visibleNodeIds.insert(port->node_id);
                }
            }
        }
    }
    if (m_selectedPortId != static_cast<uint64_t>(-1)) {
        auto port = m_graph->getPort(m_selectedPortId);
        if (port) {
            visibleNodeIds.insert(port->node_id);
        }
    }
    // Keep node owning an active parameter edit alive so offscreen pan/zoom does not strand the edit.
    if (m_activeParamEdit.active && !m_activeParamEdit.isGlobal) {
        if (m_graph->getNode(m_activeParamEdit.nodeId)) {
            visibleNodeIds.insert(m_activeParamEdit.nodeId);
        } else {
            m_activeParamEdit = ActiveParamEdit{};
        }
    }

    std::unordered_set<uint64_t> nodesToRegister = visibleNodeIds;

    // Iterate only through the visible nodes to register their connection partners
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

    // For any connections crossing the viewport where neither node is on screen,
    // ensure both endpoints are registered so the wire is rendered by imgui-node-editor
    for (const auto& conn : m_graph->getConnections()) {
        auto fromPort = m_graph->getPort(conn.from_port);
        auto toPort = m_graph->getPort(conn.to_port);
        if (!fromPort || !toPort) continue;

        uint64_t nodeAId = fromPort->node_id;
        uint64_t nodeBId = toPort->node_id;

        if (nodesToRegister.contains(nodeAId) && nodesToRegister.contains(nodeBId)) {
            continue;
        }

        ed::NodeId edA = IdUtils::toNodeId(nodeAId);
        ed::NodeId edB = IdUtils::toNodeId(nodeBId);

        ImVec2 posA = ed::GetNodePosition(edA);
        ImVec2 posB = ed::GetNodePosition(edB);

        if (!std::isfinite(posA.x) || !std::isfinite(posA.y) ||
            !std::isfinite(posB.x) || !std::isfinite(posB.y)) {
            continue;
        }

        ImVec2 sizeA = ed::GetNodeSize(edA);
        if (!std::isfinite(sizeA.x) || !std::isfinite(sizeA.y) || sizeA.x <= 0.0f || sizeA.y <= 0.0f) {
            sizeA = ImVec2(300.0f, 100.0f);
        }
        ImVec2 sizeB = ed::GetNodeSize(edB);
        if (!std::isfinite(sizeB.x) || !std::isfinite(sizeB.y) || sizeB.x <= 0.0f || sizeB.y <= 0.0f) {
            sizeB = ImVec2(300.0f, 100.0f);
        }

        ImVec2 centerA(posA.x + sizeA.x * 0.5f, posA.y + sizeA.y * 0.5f);
        ImVec2 centerB(posB.x + sizeB.x * 0.5f, posB.y + sizeB.y * 0.5f);

        const float wirePadding = std::max({sizeA.x * 0.5f + 150.0f, sizeB.x * 0.5f + 150.0f, 300.0f});
        ImVec2 paddedBoxMin(canvasMin.x - wirePadding, canvasMin.y - wirePadding);
        ImVec2 paddedBoxMax(canvasMax.x + wirePadding, canvasMax.y + wirePadding);

        if (segmentIntersectsAABB(centerA, centerB, paddedBoxMin, paddedBoxMax)) {
            nodesToRegister.insert(nodeAId);
            nodesToRegister.insert(nodeBId);
        }
    }

    m_debugInfo.visibleNodes = static_cast<int>(nodesToRegister.size());

    std::string deferredTooltip;
    // Draw only visible nodes
    for (const auto& nodeData : nodesToRegister) {
        auto node = m_graph->getNode(nodeData);
        if (!node) continue;

        const auto& gameDataRef = m_graph->getGameData();
        const gamedata::NodeEval* nodeEval = m_graph->getNodeEval(node->id);
        const gamedata::NodeMetricsResult* nodeMetrics = m_solver->getNodeMetrics(node->id);
        const Recipe* recipePtr = gameDataRef.findRecipe(node->selected_recipe_key);
        const gamedata::Profile* profilePtr = findNodeProfile(gameDataRef, *node);

        gamedata::EvalStatus evalStatus = nodeEval ? nodeEval->status : gamedata::EvalStatus::OK;
        if (node->is_unresolved) {
            evalStatus = gamedata::EvalStatus::ERROR;
        }
        bool pushedStatusBorder = (evalStatus != gamedata::EvalStatus::OK);

        ed::NodeId nodeId = IdUtils::toNodeId(node->id);
        if (pushedStatusBorder) {
            ed::PushStyleColor(ed::StyleColor_NodeBorder, evalStatusColor(evalStatus));
        }
        ed::BeginNode(nodeId);
        std::string nodeDeferredTip;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2,0));

        // --- PRE-CALC: count visible ports and heights ----
        ImGuiStyle &style = ImGui::GetStyle();
        float font_h = ImGui::GetFontSize();                // text height
        float port_img_h = font_h;                          // the image height for pins
        float port_line_h = std::max(port_img_h, font_h);  // effective per-pin height
        float spacing_y = style.ItemSpacing.y;             // vertical spacing between items

        // Count only active visible ports
        const auto& gameData = m_graph->getGameData();
        int visible_inputs = 0, visible_outputs = 0;
        for (uint64_t portId : node->input_ports) {
            Port *p = m_graph->getPort(portId);
            if (p && p->active && !p->resource_key.empty() && gameData.findResource(p->resource_key) != nullptr) ++visible_inputs;
        }
        for (uint64_t portId : node->output_ports) {
            Port *p = m_graph->getPort(portId);
            if (p && p->active && !p->resource_key.empty() && gameData.findResource(p->resource_key) != nullptr) ++visible_outputs;
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
                if (!p || !p->active || p->resource_key.empty()) continue;

                const Resource* resPtr = gameData.findResource(p->resource_key);
                if (!resPtr) continue;
                const Resource& res = *resPtr;
                ed::BeginPin(IdUtils::toPinId(p->id), ed::PinKind::Input);
                ImVec2 iconPos = ImGui::GetCursorScreenPos();
                ImVec2 iconSize(port_img_h, port_img_h);
                if (m_graph->getConnectionsForPort(p->id).empty()) {
                    bool isDark = SettingsManager::instance().getSettings().themeName == "Dark";
                    ImU32 bgColor = isDark ? IM_COL32(190, 70, 70, 200)
                                           : IM_COL32(235, 180, 180, 255);

                    ImGui::GetWindowDrawList()->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize.x, iconPos.y + iconSize.y), bgColor, 0.0f);
                }
                ImGui::Image(
                    IconCache::instance().getResourceIcon(m_iconPackageId, res),
                    ImVec2(port_img_h, port_img_h));
                if (m_solver->isGoalBottlenecked(p->id)) {
                    ImGui::GetWindowDrawList()->AddRect(
                        iconPos, ImVec2(iconPos.x + iconSize.x, iconPos.y + iconSize.y),
                        IM_COL32(235, 80, 40, 255), 0.0f, 0, 2.0f);
                }
                ImGui::SameLine();
                if (ImGui::IsItemHovered()) {
                    deferredTooltip = buildPinTooltipText(*p, res, false);
                }
                const std::string rateText = formatPinRateText(*p);
                if (m_solver->isGoalBottlenecked(p->id)) {
                    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.2f, 1.0f), "%s", rateText.c_str());
                } else {
                    ImGui::Text("%s", rateText.c_str());
                }
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

        // --- MIDDLE (machine, body metrics, body parameters) ---
        bool hasBodyParams = false;
        if (profilePtr) {
            for (const auto& pid : profilePtr->params) {
                const auto* pdef = gameData.findParameter(pid);
                if (!pdef || pdef->ui.placement != gamedata::UiPlacement::Body) continue;
                if (resolveEffectiveParamUi(gameData, nodeEval, *pdef).visible) {
                    hasBodyParams = true;
                    break;
                }
            }
        }
        bool hasBodyMetrics = false;
        if (nodeMetrics && evalStatus != gamedata::EvalStatus::BLOCKED && evalStatus != gamedata::EvalStatus::ERROR) {
            for (const auto& mdef : gameData.metrics) {
                if (!metricHasDisplayLocation(mdef, gamedata::MetricDisplayLocation::Body)) continue;
                const auto* nm = nodeMetrics->findMetric(mdef.id);
                if (nm && nm->enabled) {
                    hasBodyMetrics = true;
                    break;
                }
            }
        }
        bool hasMiddleExtras = hasBodyParams || hasBodyMetrics ||
                               (evalStatus != gamedata::EvalStatus::OK) ||
                               (recipePtr && !recipePtr->setup.empty());

        ImGui::BeginGroup();
        if (!hasMiddleExtras && machine_offset > 0.0f) {
            ImGui::Dummy(ImVec2(0, machine_offset));
        }
        const float middleWidth = hasBodyParams ? std::max(machine_h, 128.0f) : machine_h;
        float groupStartX = ImGui::GetCursorPosX();

        const Machine* machPtr = gameData.findMachine(node->machine_key);
        if (machPtr) {
            if (middleWidth > machine_h) {
                ImGui::SetCursorPosX(groupStartX + (middleWidth - machine_h) * 0.5f);
            }
            ImGui::Image(
                IconCache::instance().getMachineIcon(m_iconPackageId, *machPtr),
                ImVec2(machine_h, machine_h));
            if (deferredTooltip.empty() && nodeDeferredTip.empty() && ImGui::IsItemHovered()) {
                nodeDeferredTip = buildNodeTooltipText(*node, nodeEval, nodeMetrics);
            }
        } else {
            if (middleWidth > 48.0f) {
                ImGui::SetCursorPosX(groupStartX + (middleWidth - 48.0f) * 0.5f);
            }
            ImGui::Dummy(ImVec2(48, machine_h));
        }

        const char* countText = "%.2f";
        char buf[32];
        snprintf(buf, sizeof(buf), countText, node->machine_count);
        float textWidth = ImGui::CalcTextSize(buf).x;
        ImGui::SetCursorPosX(groupStartX + (middleWidth - textWidth) * 0.5f);
        ImGui::Text("%s", buf);
        if (deferredTooltip.empty() && nodeDeferredTip.empty() && ImGui::IsItemHovered()) {
            nodeDeferredTip = buildNodeTooltipText(*node, nodeEval, nodeMetrics);
        }

        if (evalStatus != gamedata::EvalStatus::OK) {
            const char* statusStr = node->is_unresolved ? "[UNRESOLVED]" :
                                    (evalStatus == gamedata::EvalStatus::BLOCKED ? "[BLOCKED]" :
                                     (evalStatus == gamedata::EvalStatus::ERROR ? "[ERROR]" : "[WARN]"));
            float statusW = ImGui::CalcTextSize(statusStr).x;
            ImGui::SetCursorPosX(groupStartX + std::max(0.0f, (middleWidth - statusW) * 0.5f));
            ImGui::TextColored(evalStatusColor(evalStatus), "%s", statusStr);
            if (deferredTooltip.empty() && nodeDeferredTip.empty() && ImGui::IsItemHovered()) {
                nodeDeferredTip = buildNodeTooltipText(*node, nodeEval, nodeMetrics);
            }
        }

        if (recipePtr && !recipePtr->setup.empty()) {
            for (const auto& item : recipePtr->setup) {
                std::string setupStr = getResourceDisplayName(gameData, item.resource) +
                    " x" + formatDoubleScalar("%.4g", item.amount);
                ImGui::SetCursorPosX(groupStartX);
                ImGui::TextDisabled("%s", setupStr.c_str());
            }
        }

        if (hasBodyMetrics) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
            for (const auto& mdef : gameData.metrics) {
                if (!metricHasDisplayLocation(mdef, gamedata::MetricDisplayLocation::Body)) continue;
                const auto* nm = nodeMetrics->findMetric(mdef.id);
                if (!nm || !nm->enabled) continue;
                std::string mLabel = mdef.label.empty() ? mdef.id : mdef.label;
                ImGui::SetCursorPosX(groupStartX);
                if (nm->status == gamedata::EvalStatus::ERROR) {
                    ImGui::TextColored(evalStatusColor(gamedata::EvalStatus::ERROR),
                                       "%s: Error", mLabel.c_str());
                    if (deferredTooltip.empty() && nodeDeferredTip.empty() && ImGui::IsItemHovered()) {
                        nodeDeferredTip = !nm->error_message.empty()
                            ? nm->error_message
                            : buildNodeTooltipText(*node, nodeEval, nodeMetrics);
                    }
                } else {
                    std::string valStr = formatMetricValueString(mdef, nm->value, *m_graph);
                    ImGui::Text("%s: %s", mLabel.c_str(), valStr.c_str());
                    if (deferredTooltip.empty() && nodeDeferredTip.empty() && ImGui::IsItemHovered()) {
                        nodeDeferredTip = buildNodeTooltipText(*node, nodeEval, nodeMetrics);
                    }
                }
            }
            ImGui::PopStyleVar();
        }

        if (hasBodyParams) {
            char nidBuf[32];
            snprintf(nidBuf, sizeof(nidBuf), "##nbody_%" PRIu64, node->id);
            ImGui::PushID(nidBuf);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 3));
            ImGui::Dummy(ImVec2(0, 2));
            ImGui::SetCursorPosX(groupStartX);
            std::string* tipTarget = (deferredTooltip.empty() && nodeDeferredTip.empty()) ? &nodeDeferredTip : nullptr;
            drawNodeParameters(node->id, /*onlyBodyPlacement=*/true, /*inNodeBody=*/true,
                               middleWidth, tipTarget);
            ImGui::PopStyleVar(2);
            ImGui::PopID();

            // Parameter edits or live commands inside drawNodeParameters can re-evaluate and re-solve,
            // invalidating cached NodeEval / NodeMetricsResult pointers.
            node = m_graph->getNode(nodeData);
            nodeEval = m_graph->getNodeEval(nodeData);
            nodeMetrics = m_solver->getNodeMetrics(nodeData);
        }
        ImGui::EndGroup();

        ImGui::SameLine();

        // --- RIGHT (outputs) ---
        ImGui::BeginGroup();
        if (outputs_offset > 0.0f) ImGui::Dummy(ImVec2(0, outputs_offset));

        if (visible_outputs > 0) {
            ImGui::BeginGroup();
            for (uint64_t port : node->output_ports) {
                Port *p = m_graph->getPort(port);
                if (!p || !p->active || p->resource_key.empty()) continue;

                const Resource* resPtr = gameData.findResource(p->resource_key);
                if (!resPtr) continue;
                const Resource& res = *resPtr;
                ed::BeginPin(IdUtils::toPinId(p->id), ed::PinKind::Output);
                const std::string rateText = formatPinRateText(*p);
                if (m_solver->isGoalBottlenecked(p->id)) {
                    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.2f, 1.0f), "%s", rateText.c_str());
                } else {
                    ImGui::Text("%s", rateText.c_str());
                }
                ImGui::SameLine();
                bool has_excess = p->excess_rate > 1e-4;
                bool is_unconnected = m_graph->getConnectionsForPort(p->id).empty();
                ImVec2 iconPos = ImGui::GetCursorScreenPos();
                ImVec2 iconSize(port_img_h, port_img_h);
                if (has_excess || is_unconnected) {
                    bool isDark = SettingsManager::instance().getSettings().themeName == "Dark";
                    ImU32 bgColor;
                    if (has_excess) {
                        bgColor = isDark ? IM_COL32(210, 140, 50, 200) : IM_COL32(245, 190, 100, 255); // Orange
                    } else {
                        bgColor = isDark ? IM_COL32(70, 190, 70, 200) : IM_COL32(180, 235, 180, 255); // Green
                    }

                    ImGui::GetWindowDrawList()->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize.x, iconPos.y + iconSize.y), bgColor, 0.0f);
                }
                ImGui::Image(
                    IconCache::instance().getResourceIcon(m_iconPackageId, res),
                    ImVec2(port_img_h, port_img_h));
                if (m_solver->isGoalBottlenecked(p->id)) {
                    ImGui::GetWindowDrawList()->AddRect(
                        iconPos, ImVec2(iconPos.x + iconSize.x, iconPos.y + iconSize.y),
                        IM_COL32(235, 80, 40, 255), 0.0f, 0, 2.0f);
                }
                if (ImGui::IsItemHovered()) {
                    deferredTooltip = buildPinTooltipText(*p, res, has_excess);
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
        if (pushedStatusBorder) {
            ed::PopStyleColor();
        }
        if (deferredTooltip.empty()) {
            if (!nodeDeferredTip.empty()) {
                deferredTooltip = std::move(nodeDeferredTip);
            } else if (ed::GetHoveredNode() == nodeId && !m_activeParamEdit.active && !ImGui::IsAnyItemActive()) {
                deferredTooltip = buildNodeTooltipText(*node, nodeEval, nodeMetrics);
            }
        }
    }

    if (m_pendingBodyParamPopup.shouldOpen) {
        m_activeBodyParamPopup = m_pendingBodyParamPopup;
        m_pendingBodyParamPopup.shouldOpen = false;
        ed::Suspend();
        ImGui::SetNextWindowPos(m_activeBodyParamPopup.anchorPos, ImGuiCond_Appearing);
        ImGui::OpenPopup("##body_param_popup");
        ed::Resume();
    }
    drawDeferredBodyParamPopup();

    if (!deferredTooltip.empty()) {
        ed::Suspend();

        ImGui::SetNextWindowPos(ImGui::GetMousePos() + ImVec2(15, 15));
        if (SettingsManager::instance().getSettings().themeName == "Dark") {
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.1f, 0.1f, 0.1f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(1.0f, 1.0f, 1.0f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);

        ImGui::BeginTooltip();
        ImGui::TextUnformatted(deferredTooltip.c_str());
        ImGui::EndTooltip();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

        ed::Resume();
    }
}

void FactoryNodeEditor::drawConnections() {
    for (const auto &c: m_graph->getConnections()) {
        ImVec4 color = SettingsManager::instance().getSettings().themeName == "Dark" ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f,  1.0f);
        if (c.state == WireState::INVALID) {
            color = ImVec4(0.95f, 0.25f, 0.25f, 1.0f);
        } else if (c.state == WireState::INACTIVE) {
            color.w = 0.45f;
        }
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
                m_storedPopupPosition = ImGui::GetMousePos();
                ed::Resume();
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
                    ImVec2 newPos = ed::GetNodePosition(nodeId);
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
        commitActiveParamEdit();
        ImGui::OpenPopup("Node Context Menu");
        m_isNodeContextMenuInitialized = false;
    }
    ed::PinId clickedPinId = 0;
    if (ed::ShowPinContextMenu(&clickedPinId)) {
        commitActivePinConstraintEdit();
        m_contextPinId = clickedPinId;
        ImGui::OpenPopup("Pin Context Menu");
        m_isContextMenuInitialized = false;
    }
    if (ed::ShowLinkContextMenu(&m_contextLinkId)) {
        ImGui::OpenPopup("Link Context Menu");
    }
    ed::Resume();

    if (ed::ShowBackgroundContextMenu()) {
        ed::Suspend();
        ImGui::OpenPopup("Create new node");
        m_storedPopupPosition = ImGui::GetMousePos();
        m_selectedPortId = static_cast<uint64_t>(-1);
        ed::Resume();
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
            m_isNodeContextMenuInitialized = true;
            if (const Machine* nodeMach = gameData.findMachine(node->machine_key)) {
                ImTextureID icon = IconCache::instance().getMachineIcon(m_iconPackageId, *nodeMach);
                ImGui::Image(icon, ImVec2(fontSize, fontSize));
                ImGui::SameLine();
            }
            ImGui::Text("%s", node->name.c_str());

            const gamedata::NodeEval* nodeEval = m_graph->getNodeEval(node->id);
            const gamedata::NodeMetricsResult* nodeMetrics = m_solver->getNodeMetrics(node->id);
            gamedata::EvalStatus evalStatus = nodeEval ? nodeEval->status : gamedata::EvalStatus::OK;
            if (node->is_unresolved) evalStatus = gamedata::EvalStatus::ERROR;
            if (evalStatus != gamedata::EvalStatus::OK) {
                ImGui::SameLine();
                ImGui::TextColored(evalStatusColor(evalStatus), "[%s]",
                                   node->is_unresolved ? "UNRESOLVED" : evalStatusLabel(evalStatus));
            }

            ImGui::Separator();

            if (nodeEval && !nodeEval->messages.empty()) {
                for (const auto& msg : nodeEval->messages) {
                    ImGui::PushTextWrapPos(240.0f);
                    ImGui::TextColored(evalStatusColor(msg.severity), "%s: %s",
                                       evalStatusLabel(msg.severity), msg.text.c_str());
                    ImGui::PopTextWrapPos();
                }
                ImGui::Separator();
            }
            if (nodeMetrics && !nodeMetrics->messages.empty()) {
                for (const auto& msg : nodeMetrics->messages) {
                    ImGui::PushTextWrapPos(240.0f);
                    ImGui::TextColored(evalStatusColor(msg.severity), "%s: %s",
                                       evalStatusLabel(msg.severity), msg.text.c_str());
                    ImGui::PopTextWrapPos();
                }
                ImGui::Separator();
            }

            std::vector<std::string> allowedMachines;
            if (const Recipe* nodeRec = gameData.findRecipe(node->selected_recipe_key)) {
                allowedMachines = nodeRec->compatible_machine_ids;
            }

            bool isCurrentMachineValid = std::find(allowedMachines.begin(), allowedMachines.end(), node->machine_key) != allowedMachines.end();

            if (allowedMachines.size() > 1 || (!allowedMachines.empty() && !isCurrentMachineValid)) {
                ImGui::TextDisabled("Change Machine:");

                std::string currentMachineName = node->machine_key;
                if (const Machine* currentMach = gameData.findMachine(node->machine_key)) {
                    currentMachineName = currentMach->name;
                } else if (node->machine_key.empty()) {
                    currentMachineName = "None";
                }

                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::BeginCombo("##machine_selector", currentMachineName.c_str())) {
                    std::vector<const Machine*> validMachines;
                    validMachines.reserve(allowedMachines.size());
                    int selectedIdx = -1;
                    for (const auto& mKey : allowedMachines) {
                        const Machine* machPtr = gameData.findMachine(mKey);
                        if (!machPtr) continue;
                        if (node->machine_key == mKey) {
                            selectedIdx = static_cast<int>(validMachines.size());
                        }
                        validMachines.push_back(machPtr);
                    }

                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(validMachines.size()));
                    if (selectedIdx >= 0) {
                        clipper.IncludeItemByIndex(selectedIdx);
                    }
                    while (clipper.Step()) {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                            const Machine& machine = *validMachines[static_cast<size_t>(i)];
                            const bool isSelected = (node->machine_key == machine.id);

                            ImGui::PushID(machine.id.c_str());

                            ImGui::Image(
                                IconCache::instance().getMachineIcon(m_iconPackageId, machine),
                                ImVec2(fontSize, fontSize));
                            ImGui::SameLine();

                            if (ImGui::Selectable(machine.name.c_str(), isSelected)) {
                                if (node->machine_key != machine.id) {
                                    executeCommand(std::make_unique<ChangeMachineCommand>(
                                        node->id, node->machine_key, machine.id
                                    ));
                                }
                            }

                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Speed: %.2fx", machine.speed);
                            }

                            if (isSelected) ImGui::SetItemDefaultFocus();
                            ImGui::PopID();
                        }
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
                const Machine* machPtr = gameData.findMachine(node->machine_key);
                ImGui::Text("%s", machPtr ? machPtr->name.c_str() : node->machine_key.c_str());
            }

            // Schema-defined node parameters (profile)
            ImGui::Separator();
            drawNodeParameters(node->id, /*onlyBodyPlacement=*/false, /*inNodeBody=*/false, 200.0f, nullptr);
            ImGui::Separator();

            if (ImGui::MenuItem("Delete Node")) {
                const uint64_t doomedId = node->id;
                executeCommand(std::make_unique<RemoveNodeCommand>(doomedId));
                node = nullptr;
            }

            node = m_graph->getNode(IdUtils::fromNodeId(m_contextNodeId));
            if (showDebug && node) {
                ImGui::Separator();
                ImGui::TextDisabled("Debug Info");
                ImGui::Text("Node ID: %" PRIu64, node->id);
                ImGui::Text("Recipe Key: %s", node->selected_recipe_key.c_str());
                ImGui::Text("Machine Key: %s", node->machine_key.c_str());
                ImGui::Text("Machine Count: %.4f", node->machine_count);
            }
        }
        ImGui::EndPopup();
    } else if (m_isNodeContextMenuInitialized) {
        commitActiveParamEdit();
        m_isNodeContextMenuInitialized = false;
    }
    if (ImGui::BeginPopup("Pin Context Menu")) {
        auto port = m_graph->getPort(IdUtils::fromPinId(m_contextPinId));
        if (port) {
            if (!m_isContextMenuInitialized) {
                m_contextPinOriginalConstraint = port->user_constraint;
                m_contextPinCurrentConstraint = port->user_constraint;
                if (port->user_constraint < 0.0) m_contextPinConstraintBuf[0] = '\0';
                else snprintf(m_contextPinConstraintBuf, sizeof(m_contextPinConstraintBuf), "%.12g",
                              m_graph->toResourceDisplayRate(port->resource_key, port->user_constraint));

                m_isContextMenuInitialized = true;
            }

            if (const Resource* res = gameData.findResource(port->resource_key)) {
                float imageSize = ImGui::GetFontSize();
                ImGui::Image(
                    IconCache::instance().getResourceIcon(m_iconPackageId, *res),
                    ImVec2(imageSize, imageSize));
                ImGui::SameLine();
                ImGui::Text("%s", res->name.c_str());
            } else {
                ImGui::Text("%s", port->resource_key.c_str());
            }

            ImGui::SameLine();
            ImGui::TextDisabled(m_graph->isInputPort(port->id) ? "(Input)" : "(Output)");

            ImGui::Separator();

            const std::string rateSuffix = m_graph->formatResourceRateSuffix(port->resource_key);
            const std::string spacedRateSuffix = m_graph->formatResourceRateSpacedSuffix(port->resource_key);
            ImGui::Text("Flow Rate Limit (%s):", rateSuffix.c_str());
            ImGui::PushItemWidth(-1);

            bool hugeGraph = m_graph->getNodes().size() >= 1000;

            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##rate", m_contextPinConstraintBuf, sizeof(m_contextPinConstraintBuf), ImGuiInputTextFlags_AutoSelectAll)) {
                double v = strtod(m_contextPinConstraintBuf, nullptr);
                if (m_contextPinConstraintBuf[0] == '\0') {
                    m_contextPinCurrentConstraint = -1.0;
                } else {
                    const double clampedDisplay = (!std::isfinite(v) || v < 0.0) ? 0.0 : v;
                    m_contextPinCurrentConstraint = m_graph->fromResourceDisplayRate(port->resource_key, clampedDisplay);
                }

                // Only solve LIVE if graph is small
                if (!hugeGraph) {
                    m_graph->setPortConstraint(port->id, m_contextPinCurrentConstraint);
                    (void)m_solver->solve(*m_graph);
                }
            }
            ImGui::PopItemWidth();
            const bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
            if (ImGui::IsItemDeactivatedAfterEdit() || (ImGui::IsItemFocused() && enterPressed)) {
                commitActivePinConstraintEdit();
                if (enterPressed) {
                    ImGui::CloseCurrentPopup();
                    m_isContextMenuInitialized = false;
                }
            }

            ImGui::Spacing();
            const ConstraintMode resolvedMode = m_solver->getResolvedConstraintMode(port->id);
            char autoLabel[64];
            if (port->constraint_mode == ConstraintMode::AUTO && resolvedMode != ConstraintMode::AUTO) {
                snprintf(autoLabel, sizeof(autoLabel), "Auto (%s)", constraintModeToString(resolvedMode));
            } else {
                snprintf(autoLabel, sizeof(autoLabel), "Auto");
            }

            ImGui::Text("Constraint Mode:");
            auto modeOption = [&](const char *label, const ConstraintMode mode) {
                const bool selected = (port->constraint_mode == mode);
                if (ImGui::RadioButton(label, selected) && !selected) {
                    if (m_contextPinCurrentConstraint != m_contextPinOriginalConstraint) {
                        executeCommand(std::make_unique<SetPortConstraintCommand>(
                            port->id, m_contextPinOriginalConstraint, m_contextPinCurrentConstraint,
                            port->constraint_mode, mode));
                        m_contextPinOriginalConstraint = m_contextPinCurrentConstraint;
                    } else {
                        executeCommand(std::make_unique<SetPortConstraintModeCommand>(
                            port->id, port->constraint_mode, mode));
                    }
                }
            };
            modeOption(autoLabel, ConstraintMode::AUTO);
            modeOption("Limit (Cap)", ConstraintMode::LIMIT);
            modeOption("Target (Pull)", ConstraintMode::TARGET);
            modeOption("Push (Force Downstream)", ConstraintMode::PUSH);

            ImGui::Separator();

            ImGui::TextDisabled("Current Actual Flow: %.2f%s",
                                m_graph->toResourceDisplayRate(port->resource_key, port->rate),
                                spacedRateSuffix.c_str());
            const double saturation = m_solver->getGoalSaturation(port->id);
            if (saturation >= 0.0) {
                if (m_solver->isGoalBottlenecked(port->id)) {
                    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.2f, 1.0f),
                                       "Goal Saturation: %.1f%% (Bottlenecked)", saturation * 100.0);
                } else {
                    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.0f),
                                       "Goal Saturation: %.1f%%", saturation * 100.0);
                }
            }
            if (!m_graph->isInputPort(port->id)) {
                if (port->excess_rate > 1e-4) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                                       "Excess (Unconsumed): %.2f%s",
                                       m_graph->toResourceDisplayRate(port->resource_key, port->excess_rate),
                                       spacedRateSuffix.c_str());
                } else {
                    ImGui::TextDisabled("Excess (Unconsumed): 0.00%s", spacedRateSuffix.c_str());
                }
            }


            if (showDebug) {
                ImGui::Separator();
                ImGui::TextDisabled("Debug Info");
                ImGui::Text("Port ID: %" PRIu64, port->id);
                ImGui::Text("Node ID: %" PRIu64, port->node_id);
                ImGui::Text("Constraint Val: %f", port->user_constraint);
                ImGui::Text("Constraint Mode: %s (Resolved: %s)",
                            constraintModeToString(port->constraint_mode),
                            constraintModeToString(resolvedMode));
            }
        }
        ImGui::EndPopup();
    } else if (m_isContextMenuInitialized) {
        commitActivePinConstraintEdit();
        m_isContextMenuInitialized = false;
    }

    if (ImGui::BeginPopup("Link Context Menu")) {
        auto connection = m_graph->getConnection(IdUtils::fromLinkId(m_contextLinkId));
        if (connection) {
            if (const Resource* res = gameData.findResource(connection->resource_key)) {
                ImGui::Image(
                    IconCache::instance().getResourceIcon(m_iconPackageId, *res),
                    ImVec2(24, 24));
                ImGui::SameLine();
                ImGui::Text("%s Flow", res->name.c_str());
            } else {
                ImGui::Text("%s Flow", connection->resource_key.c_str());
            }
            ImGui::Separator();

            const std::string linkRateText = formatLinkRateText(*connection);
            ImGui::TextUnformatted(linkRateText.c_str());

            ImGui::Separator();

            if (ImGui::MenuItem("Delete Link")) {
                const uint64_t fromPort = connection->from_port;
                const uint64_t toPort = connection->to_port;
                executeCommand(std::make_unique<RemoveConnectionCommand>(fromPort, toPort));
                connection = nullptr;
            }

            connection = m_graph->getConnection(IdUtils::fromLinkId(m_contextLinkId));
            if (showDebug && connection) {
                ImGui::Separator();
                ImGui::TextDisabled("Debug Info");
                ImGui::Text("Link ID: %" PRIu64, connection->id);
                ImGui::Text("From Port: %" PRIu64, connection->from_port);
                ImGui::Text("To Port: %" PRIu64, connection->to_port);
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Create new node")) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
            m_recipeSearchBuffer[0] = '\0';
            m_recipeSearchState.reset();
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##search", "Search Recipe...", m_recipeSearchBuffer, sizeof(m_recipeSearchBuffer));

        ImGui::Separator();

        const float iconSize = ImGui::GetFontSize();
        const float rowHeight = std::max(iconSize, ImGui::GetTextLineHeight());
        const float sideColWidth = 158.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::BeginTable("RecipeList", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg, ImVec2(640, 480))) {
            ImGui::TableSetupColumn("Inputs", ImGuiTableColumnFlags_WidthFixed, sideColWidth);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Outputs", ImGuiTableColumnFlags_WidthFixed, sideColWidth);

            std::string portResourceKey;
            bool portIsInput = false;
            bool hasContext = (m_selectedPortId != static_cast<uint64_t>(-1));
            if (hasContext) {
                auto p = m_graph->getPort(m_selectedPortId);
                if (p) {
                    portResourceKey = p->resource_key;
                    portIsInput = m_graph->isInputPort(m_selectedPortId);
                } else {
                    hasContext = false;
                }
            }

            const gamedata::RecipeSearchContextKind ctxKind = !hasContext
                ? gamedata::RecipeSearchContextKind::All
                : (portIsInput ? gamedata::RecipeSearchContextKind::ProducingResource
                               : gamedata::RecipeSearchContextKind::ConsumingResource);
            const bool filterChanged = m_recipeSearchState.update(
                m_recipeSearchIndex, m_recipeSearchBuffer, ctxKind, portResourceKey);
            if (filterChanged) {
                ImGui::SetScrollY(0.0f);
            }

            const auto& matchingIndices = m_recipeSearchState.results();
            m_lastRecipePickerDrawnRows = 0;

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(matchingIndices.size()), rowHeight);
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const uint32_t recipeIdx = matchingIndices[static_cast<size_t>(row)];
                    if (recipeIdx >= gameData.recipes.size()) continue;
                    const Recipe& recipe = gameData.recipes[recipeIdx];
                    ++m_lastRecipePickerDrawnRows;

                    ImGui::PushID(static_cast<int>(recipeIdx));
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);

                    ImGui::TableNextColumn();
                    for (const auto& p : recipe.in) {
                        std::string rKey = p.res.value_or("");
                        if (rKey.empty()) continue;
                        if (const Resource* res = gameData.findResource(rKey)) {
                            ImGui::Image(
                                IconCache::instance().getResourceIcon(m_iconPackageId, *res),
                                ImVec2(iconSize, iconSize));
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", res->name.c_str());
                            ImGui::SameLine();
                        }
                    }

                    ImGui::TableNextColumn();
                    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
                    if (ImGui::Selectable(recipe.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0.0f, rowHeight))) {
                        uint64_t fromPort = hasContext ? m_selectedPortId : static_cast<uint64_t>(-1);
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
                                    std::string rKey = p.res.value_or("");
                                    if (rKey.empty()) continue;

                                    // Track target index
                                    if (rKey == targetKey) target_idx = count;

                                    // Calculate width (Icon + Spacing + Rate + Spacing + Optional Text)
                                    float w = 24.0f + 30.0f + space_x;
                                    if (SettingsManager::instance().getSettings().showResourceNames) {
                                        if (const Resource* res = gameData.findResource(rKey)) {
                                            w += space_x + ImGui::CalcTextSize(res->name.c_str()).x;
                                        }
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

                            auto [in_h, in_w, in_idx]    = getColMetrics(recipe.in,  is_target_input ? portResourceKey : "");
                            auto [out_h, out_w, out_idx] = getColMetrics(recipe.out, !is_target_input ? portResourceKey : "");

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
                        executeCommand(std::make_unique<AddNodeCommand>(recipe.name, recipe.id, fromPort, nodePos));
                        m_selectedPortId = static_cast<uint64_t>(-1);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopStyleVar();

                    ImGui::TableNextColumn();
                    float contentWidth = 0.0f;
                    int count = 0;
                    for (const auto& p : recipe.out) {
                        std::string rKey = p.res.value_or("");
                        if (!rKey.empty() && gameData.findResource(rKey) != nullptr) count++;
                    }

                    if (count > 0) {
                        contentWidth = (count * iconSize) + ((count - 1) * ImGui::GetStyle().ItemSpacing.x);

                        float avail = ImGui::GetContentRegionAvail().x;
                        float off = avail - contentWidth;
                        if (off > 0.0f) {
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
                        }

                        bool first = true;
                        for (auto it = recipe.out.rbegin(); it != recipe.out.rend(); ++it) {
                            const auto& p = *it;
                            std::string rKey = p.res.value_or("");
                            if (rKey.empty()) continue;
                            if (const Resource* res = gameData.findResource(rKey)) {
                                if (!first) ImGui::SameLine();
                                ImGui::Image(
                                    IconCache::instance().getResourceIcon(m_iconPackageId, *res),
                                    ImVec2(iconSize, iconSize));
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", res->name.c_str());
                                first = false;
                            }
                        }
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    } else {
        auto* detailContext = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(m_context);
        bool isCreatingItem = false;
        if (detailContext && detailContext->GetItemCreator().m_IsActive) {
            isCreatingItem = true;
        }
        if (!isCreatingItem) {
            m_selectedPortId = static_cast<uint64_t>(-1);
        }
    }
    ed::Resume();
}

bool FactoryNodeEditor::hasPendingParamEdit() const {
    return m_activeParamEdit.active && m_activeParamEdit.dirty &&
           (!m_activeParamEdit.originalStoredValue.has_value() ||
            *m_activeParamEdit.originalStoredValue != m_activeParamEdit.currentValue);
}

bool FactoryNodeEditor::hasPendingPinConstraintEdit() const {
    return m_isContextMenuInitialized &&
           m_contextPinCurrentConstraint != m_contextPinOriginalConstraint;
}

void FactoryNodeEditor::commitActiveParamEdit() {
    if (!m_activeParamEdit.active) {
        return;
    }
    ActiveParamEdit pending = std::move(m_activeParamEdit);
    m_activeParamEdit = ActiveParamEdit{};

    if (!pending.dirty) {
        return;
    }

    const bool changedFromOriginal =
        !pending.originalStoredValue.has_value() ||
        (*pending.originalStoredValue != pending.currentValue);

    if (pending.isGlobal) {
        if (!m_graph->getGameData().findGlobal(pending.paramId)) {
            return;
        }
        if (changedFromOriginal) {
            executeCommand(std::make_unique<SetGlobalParamCommand>(
                pending.paramId,
                pending.originalStoredValue,
                pending.currentValue));
        } else if (m_graph->getNodes().size() >= 1000) {
            (void)m_solver->solve(*m_graph);
        }
        return;
    }

    Node* node = m_graph->getNode(pending.nodeId);
    if (!node) {
        return;
    }

    if (changedFromOriginal) {
        executeCommand(std::make_unique<ChangeNodeParamCommand>(
            pending.nodeId,
            pending.paramId,
            pending.originalStoredValue,
            pending.currentValue));
    } else if (m_graph->getNodes().size() >= 1000) {
        (void)m_solver->solve(*m_graph);
    }
}

void FactoryNodeEditor::commitActivePinConstraintEdit() {
    if (!hasPendingPinConstraintEdit()) {
        return;
    }
    const double original = m_contextPinOriginalConstraint;
    const double current = m_contextPinCurrentConstraint;
    m_contextPinOriginalConstraint = current;
    if (auto port = m_graph->getPort(IdUtils::fromPinId(m_contextPinId))) {
        executeCommand(std::make_unique<SetPortConstraintCommand>(
            port->id, original, current));
    }
}

void FactoryNodeEditor::commitPendingEdits() {
    commitActiveParamEdit();
    commitActivePinConstraintEdit();
}

void FactoryNodeEditor::applyLiveParamValue(
    uint64_t nodeId,
    const std::string& paramId,
    const gamedata::ParamValue& value) {
    if (!m_graph->setNodeParam(nodeId, paramId, value)) {
        return;
    }
    if (m_graph->getNodes().size() < 1000) {
        (void)m_solver->solve(*m_graph);
    }
}

void FactoryNodeEditor::applyLiveGlobalValue(
    const std::string& paramId,
    const gamedata::ParamValue& value) {
    if (!m_graph->setGlobalParam(paramId, value)) {
        return;
    }
    if (m_graph->getNodes().size() < 1000) {
        (void)m_solver->solve(*m_graph);
    }
}

void FactoryNodeEditor::executeParamChangeCommand(
    uint64_t nodeId,
    const std::string& paramId,
    const gamedata::ParamValue& newValue) {
    commitActiveParamEdit();
    Node* node = m_graph->getNode(nodeId);
    if (!node) {
        return;
    }
    std::optional<gamedata::ParamValue> oldRaw;
    if (auto it = node->params.find(paramId); it != node->params.end()) {
        oldRaw = it->second;
    }
    if (!oldRaw.has_value() || *oldRaw != newValue) {
        executeCommand(std::make_unique<ChangeNodeParamCommand>(
            nodeId, paramId, oldRaw, newValue));
    }
}

void FactoryNodeEditor::executeGlobalChangeCommand(
    const std::string& paramId,
    const gamedata::ParamValue& newValue) {
    commitActiveParamEdit();
    if (!m_graph->getGameData().findGlobal(paramId)) {
        return;
    }
    std::optional<gamedata::ParamValue> oldRaw;
    const auto& globals = m_graph->getGlobals();
    if (auto it = globals.find(paramId); it != globals.end()) {
        oldRaw = it->second;
    }
    if (!oldRaw.has_value() || *oldRaw != newValue) {
        executeCommand(std::make_unique<SetGlobalParamCommand>(
            paramId, oldRaw, newValue));
    }
}

void FactoryNodeEditor::resetProjectGlobals() {
    commitActiveParamEdit();
    const auto& gameData = m_graph->getGameData();
    const auto& storedGlobals = m_graph->getGlobals();
    auto cmd = std::make_unique<CompositeCommand>(
        "Reset Project Globals", CommandFlags{true, false});
    for (const auto& gdef : gameData.globals) {
        if (auto it = storedGlobals.find(gdef.id); it != storedGlobals.end()) {
            cmd->addCommand(SetGlobalParamCommand::createClear(gdef.id, it->second));
        }
    }
    executeCommand(std::move(cmd));
}

namespace {

void handleHoverParamTooltip(
    const gamedata::ParameterDef& pdef,
    std::string* hoveredTooltipOut,
    bool inNodeBody) {
    if (!ImGui::IsItemHovered() || pdef.tooltip.empty()) {
        return;
    }
    if (inNodeBody) {
        if (hoveredTooltipOut) {
            *hoveredTooltipOut = pdef.tooltip;
        }
    } else {
        ImGui::SetTooltip("%s", pdef.tooltip.c_str());
    }
}

template <typename OnSelectFn>
void renderChoiceListItems(
    const GameData& gameData,
    std::string_view iconPackageId,
    const gamedata::ParameterDef& pdef,
    const gamedata::EvaluatedParamUi& ui,
    const gamedata::ParamValue& currentVal,
    bool closePopupOnSelect,
    OnSelectFn&& onSelect) {
    std::optional<std::string> curKey = currentVal.isString()
        ? std::optional<std::string>(currentVal.asString())
        : std::nullopt;

    if (pdef.allow_none) {
        bool isNoneSelected = !curKey.has_value();
        if (ImGui::Selectable("(None)", isNoneSelected)) {
            if (!isNoneSelected) {
                onSelect(gamedata::ParamValue::null());
            }
            if (closePopupOnSelect) {
                ImGui::CloseCurrentPopup();
            }
        }
        if (isNoneSelected) {
            ImGui::SetItemDefaultFocus();
        }
    }

    if (pdef.type == gamedata::ParameterType::Enum) {
        const gamedata::OptionSet* os = gameData.findOptionSet(pdef.option_set);
        if (ui.allowed_options.empty() && !pdef.allow_none) {
            ImGui::TextDisabled("(No allowed options)");
        }
        int selectedIdx = -1;
        if (curKey.has_value()) {
            for (size_t i = 0; i < ui.allowed_options.size(); ++i) {
                if (ui.allowed_options[i] == *curKey) {
                    selectedIdx = static_cast<int>(i);
                    break;
                }
            }
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(ui.allowed_options.size()));
        if (selectedIdx >= 0) {
            clipper.IncludeItemByIndex(selectedIdx);
        }
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const std::string& key = ui.allowed_options[static_cast<size_t>(i)];
                bool selected = curKey.has_value() && (*curKey == key);
                std::string disp = getOptionDisplayLabel(os, key);
                ImGui::PushID(key.c_str());
                if (ImGui::Selectable(disp.c_str(), selected)) {
                    if (!selected) {
                        onSelect(gamedata::ParamValue::string(key));
                    }
                    if (closePopupOnSelect) {
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (os) {
                    if (const auto* optDef = os->findOption(key); optDef && !optDef->notes.empty() && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", optDef->notes.c_str());
                    }
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
        }
    } else if (pdef.type == gamedata::ParameterType::ResourceChoice) {
        if (ui.allowed_resources.empty() && !pdef.allow_none) {
            ImGui::TextDisabled("(No allowed resources)");
        }
        const float iconSz = ImGui::GetFontSize();
        int selectedIdx = -1;
        if (curKey.has_value()) {
            for (size_t i = 0; i < ui.allowed_resources.size(); ++i) {
                if (ui.allowed_resources[i] == *curKey) {
                    selectedIdx = static_cast<int>(i);
                    break;
                }
            }
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(ui.allowed_resources.size()));
        if (selectedIdx >= 0) {
            clipper.IncludeItemByIndex(selectedIdx);
        }
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const std::string& resId = ui.allowed_resources[static_cast<size_t>(i)];
                bool selected = curKey.has_value() && (*curKey == resId);
                const Resource* res = gameData.findResource(resId);
                std::string disp = getResourceDisplayName(gameData, resId);
                ImGui::PushID(resId.c_str());
                if (res) {
                    ImTextureID resTex = IconCache::instance().getResourceIcon(iconPackageId, *res);
                    if (resTex != 0) {
                        ImGui::Image(resTex, ImVec2(iconSz, iconSz));
                        ImGui::SameLine();
                    }
                }
                if (ImGui::Selectable(disp.c_str(), selected)) {
                    if (!selected) {
                        onSelect(gamedata::ParamValue::string(resId));
                    }
                    if (closePopupOnSelect) {
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
        }
    }
}

template <typename OnSelectFn>
void renderSlotListItems(
    const GameData& gameData,
    const gamedata::ParameterDef& pdef,
    const gamedata::EvaluatedParamUi& ui,
    const gamedata::ParamValue::SlotsArray& curSlots,
    size_t slotIdx,
    OnSelectFn&& onSelect) {
    if (slotIdx >= curSlots.size()) {
        return;
    }
    const gamedata::OptionSet* os = gameData.findOptionSet(pdef.option_set);
    const std::optional<std::string>& curKey = curSlots[slotIdx];

    if (pdef.allow_empty) {
        bool isEmptySelected = !curKey.has_value() || curKey->empty();
        if (ImGui::Selectable("(Empty)", isEmptySelected)) {
            if (!isEmptySelected) {
                gamedata::ParamValue::SlotsArray next = curSlots;
                next[slotIdx] = std::nullopt;
                onSelect(gamedata::ParamValue::slots(std::move(next)));
            }
            ImGui::CloseCurrentPopup();
        }
        if (isEmptySelected) {
            ImGui::SetItemDefaultFocus();
        }
    }

    if (ui.allowed_options.empty() && !pdef.allow_empty) {
        ImGui::TextDisabled("(No allowed options)");
    }
    int selectedIdx = -1;
    if (curKey.has_value()) {
        for (size_t i = 0; i < ui.allowed_options.size(); ++i) {
            if (ui.allowed_options[i] == *curKey) {
                selectedIdx = static_cast<int>(i);
                break;
            }
        }
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(ui.allowed_options.size()));
    if (selectedIdx >= 0) {
        clipper.IncludeItemByIndex(selectedIdx);
    }
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const std::string& key = ui.allowed_options[static_cast<size_t>(i)];
            bool selected = curKey.has_value() && (*curKey == key);
            std::string disp = getOptionDisplayLabel(os, key);
            ImGui::PushID(key.c_str());
            if (ImGui::Selectable(disp.c_str(), selected)) {
                if (!selected) {
                    gamedata::ParamValue::SlotsArray next = curSlots;
                    next[slotIdx] = key;
                    onSelect(gamedata::ParamValue::slots(std::move(next)));
                }
                ImGui::CloseCurrentPopup();
            }
            if (os) {
                if (const auto* optDef = os->findOption(key); optDef && !optDef->notes.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", optDef->notes.c_str());
                }
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
    }
}

} // namespace

void FactoryNodeEditor::drawNodeParameters(
    uint64_t nodeId,
    bool onlyBodyPlacement,
    bool inNodeBody,
    float itemWidth,
    std::string* hoveredTooltipOut) {
    Node* node = m_graph->getNode(nodeId);
    if (!node || node->is_unresolved) {
        return;
    }

    const auto& gameData = m_graph->getGameData();
    const gamedata::NodeEval* eval = m_graph->getNodeEval(nodeId);
    std::vector<const gamedata::ParameterDef*> allDefs = collectProfileParamDefs(gameData, *node);

    struct VisibleParamEntry {
        const gamedata::ParameterDef* def;
        gamedata::EvaluatedParamUi ui;
    };

    std::vector<VisibleParamEntry> visibleParams;
    for (const auto* pdef : allDefs) {
        if (onlyBodyPlacement && pdef->ui.placement != gamedata::UiPlacement::Body) {
            continue;
        }
        gamedata::EvaluatedParamUi ui = resolveEffectiveParamUi(gameData, eval, *pdef);
        if (!ui.visible) {
            continue;
        }
        visibleParams.push_back(VisibleParamEntry{pdef, std::move(ui)});
    }

    if (visibleParams.empty()) {
        if (!inNodeBody && !onlyBodyPlacement) {
            ImGui::TextDisabled("No visible parameters.");
        }
        return;
    }

    if (onlyBodyPlacement) {
        for (const auto& vp : visibleParams) {
            Node* curNode = m_graph->getNode(nodeId);
            if (!curNode) break;
            drawSingleParameterWidget(*curNode, *vp.def, vp.ui, inNodeBody, itemWidth, hoveredTooltipOut);
        }
        return;
    }

    // Group parameters by ui.group in first-appearance order
    std::vector<std::string> groupOrder;
    std::unordered_map<std::string, std::vector<size_t>> groupedIndices;
    for (size_t i = 0; i < visibleParams.size(); ++i) {
        const std::string& grp = visibleParams[i].def->ui.group;
        if (!groupedIndices.contains(grp)) {
            groupOrder.push_back(grp);
        }
        groupedIndices[grp].push_back(i);
    }

    for (size_t gi = 0; gi < groupOrder.size(); ++gi) {
        const std::string& grp = groupOrder[gi];
        if (!grp.empty()) {
            if (gi > 0) {
                ImGui::Spacing();
            }
            ImGui::TextDisabled("%s", grp.c_str());
            ImGui::Separator();
        }
        for (size_t idx : groupedIndices[grp]) {
            Node* curNode = m_graph->getNode(nodeId);
            if (!curNode) return;
            drawSingleParameterWidget(*curNode, *visibleParams[idx].def, visibleParams[idx].ui,
                                      inNodeBody, itemWidth, hoveredTooltipOut);
        }
    }
}

void FactoryNodeEditor::drawSingleParameterWidget(
    const Node& node,
    const gamedata::ParameterDef& pdef,
    const gamedata::EvaluatedParamUi& rawUi,
    bool inNodeBody,
    float itemWidth,
    std::string* hoveredTooltipOut) {
    drawParameterWidget(false, node.id, pdef, rawUi, inNodeBody, itemWidth, hoveredTooltipOut);
}

void FactoryNodeEditor::drawParameterWidget(
    bool isGlobal,
    uint64_t nodeId,
    const gamedata::ParameterDef& pdef,
    const gamedata::EvaluatedParamUi& rawUi,
    bool inNodeBody,
    float itemWidth,
    std::string* hoveredTooltipOut) {
    const Node* node = nullptr;
    const gamedata::NodeEval* eval = nullptr;
    if (!isGlobal) {
        node = m_graph->getNode(nodeId);
        if (!node) {
            return;
        }
        eval = m_graph->getNodeEval(nodeId);
    }

    ImGui::PushID(pdef.id.c_str());
    gamedata::EvaluatedParamUi ui = rawUi;
    if (ui.has_min && ui.has_max && ui.min_val > ui.max_val) {
        ui.has_min = false;
        ui.has_max = false;
        ui.min_val = 0.0;
        ui.max_val = 0.0;
    }
    const auto& gameData = m_graph->getGameData();
    gamedata::ParamValue currentVal = isGlobal
        ? resolveEffectiveGlobalValue(*m_graph, pdef)
        : resolveEffectiveParamValue(*node, eval, pdef);

    const bool hasBounded = ui.has_min && ui.has_max && (ui.min_val < ui.max_val);
    const gamedata::UiWidget widget = normalizeWidgetForType(pdef.type, pdef.ui.widget, hasBounded);
    const std::string baseLabel = pdef.label.empty() ? pdef.id : pdef.label;
    const std::string fullLabel = pdef.unit.empty() ? baseLabel : (baseLabel + " (" + pdef.unit + ")");

    auto lookupStoredOriginal = [&]() -> std::optional<gamedata::ParamValue> {
        if (isGlobal) {
            const auto& globals = m_graph->getGlobals();
            if (auto it = globals.find(pdef.id); it != globals.end()) {
                return it->second;
            }
            return std::nullopt;
        }
        if (const Node* n = m_graph->getNode(nodeId)) {
            if (auto it = n->params.find(pdef.id); it != n->params.end()) {
                return it->second;
            }
        }
        return std::nullopt;
    };

    auto isSameActiveEdit = [&]() -> bool {
        return m_activeParamEdit.active &&
               m_activeParamEdit.isGlobal == isGlobal &&
               (isGlobal || m_activeParamEdit.nodeId == nodeId) &&
               m_activeParamEdit.paramId == pdef.id;
    };

    auto applyLive = [&](const gamedata::ParamValue& v) {
        if (isGlobal) {
            applyLiveGlobalValue(pdef.id, v);
        } else {
            applyLiveParamValue(nodeId, pdef.id, v);
        }
    };

    auto commitDiscrete = [&](const gamedata::ParamValue& v) {
        if (isGlobal) {
            executeGlobalChangeCommand(pdef.id, v);
        } else {
            executeParamChangeCommand(nodeId, pdef.id, v);
        }
    };

    auto trackContinuousScalar = [&](const gamedata::ParamValue& editedVal, bool edited, bool allowEnter) {
        if (ImGui::IsItemActivated()) {
            if (m_activeParamEdit.active && !isSameActiveEdit()) {
                commitActiveParamEdit();
            }
            if (!m_activeParamEdit.active) {
                m_activeParamEdit.active = true;
                m_activeParamEdit.isGlobal = isGlobal;
                m_activeParamEdit.nodeId = isGlobal ? 0 : nodeId;
                m_activeParamEdit.paramId = pdef.id;
                m_activeParamEdit.originalStoredValue = lookupStoredOriginal();
                m_activeParamEdit.currentValue = currentVal;
                m_activeParamEdit.dirty = false;
            }
        }
        if (edited) {
            if (!isSameActiveEdit()) {
                if (m_activeParamEdit.active) {
                    commitActiveParamEdit();
                }
                m_activeParamEdit.active = true;
                m_activeParamEdit.isGlobal = isGlobal;
                m_activeParamEdit.nodeId = isGlobal ? 0 : nodeId;
                m_activeParamEdit.paramId = pdef.id;
                m_activeParamEdit.originalStoredValue = lookupStoredOriginal();
            }
            m_activeParamEdit.currentValue = editedVal;
            m_activeParamEdit.dirty = true;
            applyLive(editedVal);
        }
        const bool enterPressed = allowEnter && ImGui::IsItemFocused() &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
        if (ImGui::IsItemDeactivatedAfterEdit() || enterPressed) {
            commitActiveParamEdit();
        } else if (ImGui::IsItemDeactivated() && isSameActiveEdit() && !m_activeParamEdit.dirty) {
            m_activeParamEdit = ActiveParamEdit{};
        }
    };

    switch (pdef.type) {
        case gamedata::ParameterType::Number: {
            ImGui::TextUnformatted(inNodeBody ? baseLabel.c_str() : fullLabel.c_str());
            handleHoverParamTooltip(pdef, hoveredTooltipOut, inNodeBody);

            const double fallbackVal = (currentVal.isNumber() && std::isfinite(currentVal.asNumber()))
                ? expr::normalizeZero(currentVal.asNumber())
                : 0.0;
            double val = fallbackVal;
            if (ui.has_min && val < ui.min_val) val = ui.min_val;
            if (ui.has_max && val > ui.max_val) val = ui.max_val;
            const double clampedFallback = val;

            auto sanitizeEditedNumber = [&](double& v, bool& edited) {
                if (!edited) return;
                if (!std::isfinite(v)) {
                    v = clampedFallback;
                    edited = false;
                    return;
                }
                if (ui.has_min && v < ui.min_val) v = ui.min_val;
                if (ui.has_max && v > ui.max_val) v = ui.max_val;
                v = expr::normalizeZero(v);
            };

            std::string fmt = sanitizeScalarFormat(pdef.ui.format.empty() ? "%.4g" : pdef.ui.format, false, false);
            double stepVal = (pdef.has_step && pdef.step > 0.0) ? pdef.step : 1.0;
            float dragSpeed = 0.25f;
            if (hasBounded) {
                dragSpeed = std::clamp(static_cast<float>((ui.max_val - ui.min_val) * 0.004), 0.001f, 1.0f);
                if (pdef.has_step && pdef.step >= dragSpeed && pdef.step <= (ui.max_val - ui.min_val) * 0.25) {
                    dragSpeed = static_cast<float>(pdef.step);
                }
            } else if (pdef.has_step && pdef.step > 0.0) {
                dragSpeed = static_cast<float>(pdef.step);
            }

            const double* minPtr = (ui.has_min && ui.has_max) ? &ui.min_val : nullptr;
            const double* maxPtr = (ui.has_min && ui.has_max) ? &ui.max_val : nullptr;
            const ImGuiSliderFlags sliderFlags = (minPtr && maxPtr) ? ImGuiSliderFlags_AlwaysClamp : 0;

            if (widget == gamedata::UiWidget::Stepper) {
                const float frameH = ImGui::GetFrameHeight();
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                const float totalW = (itemWidth > 0.0f) ? itemWidth : ImGui::GetContentRegionAvail().x;
                const float midW = std::max(36.0f, totalW - (frameH + spacing) * 2.0f);
                const bool atMin = ui.has_min && val <= ui.min_val + 1e-12;
                const bool atMax = ui.has_max && val >= ui.max_val - 1e-12;

                ImGui::BeginDisabled(atMin);
                if (ImGui::Button("-##dec", ImVec2(frameH, frameH))) {
                    double next = val - stepVal;
                    if (ui.has_min && next < ui.min_val) next = ui.min_val;
                    if (ui.has_max && next > ui.max_val) next = ui.max_val;
                    commitDiscrete(gamedata::ParamValue::number(expr::normalizeZero(next)));
                }
                ImGui::EndDisabled();
                handleHoverParamTooltip(pdef, hoveredTooltipOut, inNodeBody);

                ImGui::SameLine();
                ImGui::SetNextItemWidth(midW);
                bool edited = ImGui::DragScalar("##val", ImGuiDataType_Double, &val, dragSpeed,
                                                minPtr, maxPtr, fmt.c_str(), sliderFlags);
                sanitizeEditedNumber(val, edited);
                trackContinuousScalar(gamedata::ParamValue::number(val), edited, true);
                handleHoverParamTooltip(pdef, hoveredTooltipOut, inNodeBody);

                ImGui::SameLine();
                ImGui::BeginDisabled(atMax);
                if (ImGui::Button("+##inc", ImVec2(frameH, frameH))) {
                    double next = val + stepVal;
                    if (ui.has_min && next < ui.min_val) next = ui.min_val;
                    if (ui.has_max && next > ui.max_val) next = ui.max_val;
                    commitDiscrete(gamedata::ParamValue::number(expr::normalizeZero(next)));
                }
                ImGui::EndDisabled();
                handleHoverParamTooltip(pdef, hoveredTooltipOut, inNodeBody);
            } else {
                ImGui::SetNextItemWidth(itemWidth);
                bool edited = false;
                if (widget == gamedata::UiWidget::Slider && hasBounded) {
                    edited = ImGui::SliderScalar("##val", ImGuiDataType_Double, &val, &ui.min_val, &ui.max_val,
                                                 fmt.c_str(), ImGuiSliderFlags_AlwaysClamp);
                } else if (widget == gamedata::UiWidget::Input) {
                    edited = ImGui::InputScalar("##val", ImGuiDataType_Double, &val, nullptr, nullptr, fmt.c_str(), 0);
                } else {
                    edited = ImGui::DragScalar("##val", ImGuiDataType_Double, &val, dragSpeed,
                                               minPtr, maxPtr, fmt.c_str(), sliderFlags);
                }
                sanitizeEditedNumber(val, edited);
                trackContinuousScalar(gamedata::ParamValue::number(val), edited, true);
                handleHoverParamTooltip(pdef, hoveredTooltipOut, inNodeBody);
            }
            break;
        }

        case gamedata::ParameterType::Int: {
            ImGui::TextUnformatted(inNodeBody ? baseLabel.c_str() : fullLabel.c_str());
            handleHoverParamTooltip(pdef, hoveredTooltipOut, inNodeBody);

            int64_t val64 = currentVal.isNumber() ? std::llround(currentVal.asNumber()) : 0;
            const int64_t min64 = ui.has_min ? std::llround(ui.min_val) : std::numeric_limits<int64_t>::min();
            const int64_t max64 = ui.has_max ? std::llround(ui.max_val) : std::numeric_limits<int64_t>::max();
            val64 = std::clamp(val64, min64, max64);
            const int64_t step64 = (pdef.has_step && pdef.step >= 1.0)
                ? std::max<int64_t>(1, std::llround(pdef.step))
                : 1;

            constexpr int64_t kI32Min = std::numeric_limits<int32_t>::min();
            constexpr int64_t kI32Max = std::numeric_limits<int32_t>::max();
            const bool useS32 = (val64 >= kI32Min && val64 <= kI32Max) &&
                                (!ui.has_min || min64 >= kI32Min) &&
                                (!ui.has_max || max64 <= kI32Max);
            std::string fmt = sanitizeScalarFormat(pdef.ui.format, true, !useS32);

            auto runIntScalarWidget = [&](ImGuiDataType dt, void* dataPtr, const void* pMin, const void* pMax,
                                          bool allowSlider, ImGuiSliderFlags sFlags) -> bool {
                if (widget == gamedata::UiWidget::Slider && allowSlider && pMin && pMax) {
                    return ImGui::SliderScalar("##val", dt, dataPtr, pMin, pMax, fmt.c_str(), sFlags);
                }
                if (widget == gamedata::UiWidget::Input) {
                    return ImGui::InputScalar("##val", dt, dataPtr, nullptr, nullptr, fmt.c_str(), 0);
                }
                return ImGui::DragScalar("##val", dt, dataPtr, 0.25f, pMin, pMax, fmt.c_str(), sFlags);
            };

            auto runTypedIntControl = [&](auto dummyTag, ImGuiDataType dt) {
                using T = decltype(dummyTag);
                T v = static_cast<T>(val64);
                T mn = static_cast<T>(min64);
                T mx = static_cast<T>(max64);
                const void* pMin = (ui.has_min && ui.has_max) ? &mn : nullptr;
                const void* pMax = (ui.has_min && ui.has_max) ? &mx : nullptr;
                const ImGuiSliderFlags sFlags = (pMin && pMax) ? ImGuiSliderFlags_AlwaysClamp : 0;

                if (widget == gamedata::UiWidget::Stepper) {
                    const float frameH = ImGui::GetFrameHeight();
                    const float spacing = ImGui::GetStyle().ItemSpacing.x;
                    const float totalW = (itemWidth > 0.0f) ? itemWidth : ImGui::GetContentRegionAvail().x;
                    const float midW = std::max(36.0f, totalW - (frameH + spacing) * 2.0f);
                    const bool atMin = ui.has_min && val64 <= min64;
                    const bool atMax = ui.has_max && val64 >= max64;

                    ImGui::BeginDisabled(atMin);
                    if (ImGui::Button("-##dec", ImVec2(frameH, frameH))) {
                        int64_t next = (val64 > min64 + step64) ? (val64 - step64) : min64;
                        next = std::clamp(next, min64, max64);
                        commitDiscrete(gamedata::ParamValue::number(static_cast<double>(next)));
                    }
                    ImGui::EndDisabled();
                    handleHoverParamTooltip(pdef, hoveredTooltipOut, inNodeBody);

                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(midW);
                    bool edited = ImGui::DragScalar("##val", dt, &v, 0.25f, pMin, pMax, fmt.c_str(), sFlags);
                    int64_t edited64 = std::clamp<int64_t>(static_cast<int64_t>(v), min64, max64);
                    trackContinuousScalar(gamedata::ParamValue::number(static_cast<double>(edited64)), edited, true);
                    handleHoverParamTooltip(pdef, hoveredTooltipOut, inNodeBody);

                    ImGui::SameLine();
                    ImGui::BeginDisabled(atMax);
                    if (ImGui::Button("+##inc", ImVec2(frameH, frameH))) {
                        int64_t next = (val64 < max64 - step64) ? (val64 + step64) : max64;
                        next = std::clamp(next, min64, max64);
                        commitDiscrete(gamedata::ParamValue::number(static_cast<double>(next)));
                    }
                    ImGui::EndDisabled();
                    handleHoverParamTooltip(pdef, hoveredTooltipOut, inNodeBody);
                } else {
                    ImGui::SetNextItemWidth(itemWidth);
                    bool edited = runIntScalarWidget(dt, &v, pMin, pMax, hasBounded, sFlags);
                    int64_t edited64 = std::clamp<int64_t>(static_cast<int64_t>(v), min64, max64);
                    trackContinuousScalar(gamedata::ParamValue::number(static_cast<double>(edited64)), edited, true);
                    handleHoverParamTooltip(pdef, hoveredTooltipOut, inNodeBody);
                }
            };

            if (useS32) {
                runTypedIntControl(int32_t{}, ImGuiDataType_S32);
            } else {
                runTypedIntControl(int64_t{}, ImGuiDataType_S64);
            }
            break;
        }

        case gamedata::ParameterType::Bool: {
            bool bVal = currentVal.isBool() ? currentVal.asBool() : false;
            const std::string& cbLabel = inNodeBody ? baseLabel : fullLabel;
            if (ImGui::Checkbox(cbLabel.c_str(), &bVal)) {
                commitDiscrete(gamedata::ParamValue::boolean(bVal));
            }
            handleHoverParamTooltip(pdef, hoveredTooltipOut, inNodeBody);
            break;
        }

        case gamedata::ParameterType::Enum:
        case gamedata::ParameterType::ResourceChoice: {
            ImGui::TextUnformatted(inNodeBody ? baseLabel.c_str() : fullLabel.c_str());
            handleHoverParamTooltip(pdef, hoveredTooltipOut, inNodeBody);

            std::optional<std::string> curKey = currentVal.isString()
                ? std::optional<std::string>(currentVal.asString())
                : std::nullopt;
            std::string preview;
            if (!curKey.has_value()) {
                preview = "(None)";
            } else if (pdef.type == gamedata::ParameterType::Enum) {
                preview = getOptionDisplayLabel(gameData.findOptionSet(pdef.option_set), *curKey);
            } else {
                preview = getResourceDisplayName(gameData, *curKey);
            }
            if (preview.empty()) {
                preview = "(None available)";
            }

            if (inNodeBody && !isGlobal) {
                std::string btnText = preview + "##choice";
                if (ImGui::Button(btnText.c_str(), ImVec2(itemWidth > 0.0f ? itemWidth : 120.0f, 0.0f))) {
                    ImVec2 bl(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y);
                    m_pendingBodyParamPopup = PendingParamPopup{
                        true, nodeId, pdef.id, -1, ed::CanvasToScreen(bl)};
                }
                handleHoverParamTooltip(pdef, hoveredTooltipOut, inNodeBody);
            } else {
                ImGui::SetNextItemWidth(itemWidth);
                if (ImGui::BeginCombo("##val", preview.c_str())) {
                    renderChoiceListItems(
                        gameData, m_iconPackageId, pdef, ui, currentVal, false,
                        [&](const gamedata::ParamValue& chosen) {
                            commitDiscrete(chosen);
                        });
                    ImGui::EndCombo();
                }
                handleHoverParamTooltip(pdef, hoveredTooltipOut, inNodeBody);
            }
            break;
        }

        case gamedata::ParameterType::Slots: {
            ImGui::TextUnformatted(inNodeBody ? baseLabel.c_str() : fullLabel.c_str());
            handleHoverParamTooltip(pdef, hoveredTooltipOut, inNodeBody);

            const size_t slotCount = ui.slot_count;
            if (slotCount == 0) {
                ImGui::TextDisabled("(0 slots)");
                break;
            }

            gamedata::ParamValue::SlotsArray curSlots;
            if (currentVal.isSlots()) {
                curSlots = currentVal.asSlots();
            }
            curSlots.resize(slotCount, std::nullopt);

            const gamedata::OptionSet* os = gameData.findOptionSet(pdef.option_set);
            const ImGuiStyle& style = ImGui::GetStyle();
            const float availW = (itemWidth > 0.0f) ? itemWidth : ImGui::GetContentRegionAvail().x;
            float lineUsedW = 0.0f;

            for (size_t i = 0; i < slotCount; ++i) {
                ImGui::PushID(static_cast<int>(i));
                std::string slotLabel = (curSlots[i].has_value() && !curSlots[i]->empty())
                    ? getOptionDisplayLabel(os, *curSlots[i])
                    : "Empty";
                std::string btnDisp = "[" + slotLabel + "]";
                float btnW = ImGui::CalcTextSize(btnDisp.c_str()).x + style.FramePadding.x * 2.0f;

                if (i > 0 && lineUsedW + style.ItemSpacing.x + btnW <= availW) {
                    ImGui::SameLine();
                    lineUsedW += style.ItemSpacing.x + btnW;
                } else {
                    lineUsedW = btnW;
                }

                std::string btnId = btnDisp + "##slot";
                if (ImGui::Button(btnId.c_str())) {
                    if (inNodeBody && !isGlobal) {
                        ImVec2 bl(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y);
                        m_pendingBodyParamPopup = PendingParamPopup{
                            true, nodeId, pdef.id, static_cast<int>(i), ed::CanvasToScreen(bl)};
                    } else {
                        ImGui::OpenPopup("##slot_picker");
                    }
                }
                handleHoverParamTooltip(pdef, hoveredTooltipOut, inNodeBody);

                if (!inNodeBody && ImGui::IsPopupOpen("##slot_picker")) {
                    ImGui::SetNextWindowSizeConstraints(ImVec2(180.0f, 0.0f), ImVec2(420.0f, 360.0f));
                }
                if (!inNodeBody && ImGui::BeginPopup("##slot_picker")) {
                    renderSlotListItems(
                        gameData, pdef, ui, curSlots, i,
                        [&](const gamedata::ParamValue& chosenSlots) {
                            commitDiscrete(chosenSlots);
                        });
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            break;
        }
    }

    ImGui::PopID();
}

void FactoryNodeEditor::drawDeferredBodyParamPopup() {
    ed::Suspend();
    if (ImGui::IsPopupOpen("##body_param_popup")) {
        ImGui::SetNextWindowSizeConstraints(ImVec2(180.0f, 0.0f), ImVec2(420.0f, 360.0f));
    }
    if (ImGui::BeginPopup("##body_param_popup")) {
        const auto& gameData = m_graph->getGameData();
        const Node* node = m_graph->getNode(m_activeBodyParamPopup.nodeId);
        const gamedata::ParameterDef* pdef = gameData.findParameter(m_activeBodyParamPopup.paramId);
        if (!node || !pdef) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            ed::Resume();
            return;
        }
        const gamedata::NodeEval* eval = m_graph->getNodeEval(node->id);
        gamedata::EvaluatedParamUi ui = resolveEffectiveParamUi(gameData, eval, *pdef);
        if (!ui.visible) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            ed::Resume();
            return;
        }

        gamedata::ParamValue currentVal = resolveEffectiveParamValue(*node, eval, *pdef);
        if (pdef->type == gamedata::ParameterType::Slots && m_activeBodyParamPopup.slotIndex >= 0) {
            size_t slotIdx = static_cast<size_t>(m_activeBodyParamPopup.slotIndex);
            gamedata::ParamValue::SlotsArray curSlots;
            if (currentVal.isSlots()) {
                curSlots = currentVal.asSlots();
            }
            curSlots.resize(ui.slot_count, std::nullopt);
            if (slotIdx < ui.slot_count) {
                renderSlotListItems(
                    gameData, *pdef, ui, curSlots, slotIdx,
                    [&](const gamedata::ParamValue& chosenSlots) {
                        executeParamChangeCommand(node->id, pdef->id, chosenSlots);
                    });
            } else {
                ImGui::CloseCurrentPopup();
            }
        } else if (pdef->type == gamedata::ParameterType::Enum ||
                   pdef->type == gamedata::ParameterType::ResourceChoice) {
            renderChoiceListItems(
                gameData, m_iconPackageId, *pdef, ui, currentVal, true,
                [&](const gamedata::ParamValue& chosen) {
                    executeParamChangeCommand(node->id, pdef->id, chosen);
                });
        }
        ImGui::EndPopup();
    }
    ed::Resume();
}

std::string FactoryNodeEditor::formatPinRateText(const Port& port) const {
    const double displayRate = m_graph->toResourceDisplayRate(port.resource_key, port.rate);
    const bool hasNumericConstraint = port.user_constraint >= 0.0;
    const bool hasUnratedPush = (port.user_constraint < 0.0 && port.constraint_mode == ConstraintMode::PUSH);
    char buf[64];
    if (hasNumericConstraint) {
        const ConstraintMode resolved = m_solver->getResolvedConstraintMode(port.id);
        const char tag = (resolved == ConstraintMode::LIMIT) ? 'L'
                       : (resolved == ConstraintMode::PUSH)  ? 'P' : 'T';
        if (m_solver->isGoalBottlenecked(port.id)) {
            snprintf(buf, sizeof(buf), "%.2f [!%c]", displayRate, tag);
        } else {
            snprintf(buf, sizeof(buf), "%.2f [%c]", displayRate, tag);
        }
    } else if (hasUnratedPush) {
        snprintf(buf, sizeof(buf), "%.2f [P]", displayRate);
    } else {
        snprintf(buf, sizeof(buf), "%.2f", displayRate);
    }
    return buf;
}

std::string FactoryNodeEditor::formatLinkRateText(const Connection& connection) const {
    const std::string spacedUnit = m_graph->formatResourceRateSpacedSuffix(connection.resource_key);
    char buf[128];
    snprintf(buf, sizeof(buf), "Current Rate: %.2f%s",
             m_graph->toResourceDisplayRate(connection.resource_key, connection.rate),
             spacedUnit.c_str());
    return buf;
}

std::string FactoryNodeEditor::buildPinTooltipText(
    const Port& port, const Resource& res, bool hasExcess) const {
    const std::string& resKey = !port.resource_key.empty() ? port.resource_key : res.id;
    const std::string spacedUnit = m_graph->formatResourceRateSpacedSuffix(resKey);
    std::string tip = res.name;
    if (hasExcess) {
        char ex_buf[128];
        snprintf(ex_buf, sizeof(ex_buf), "\nExcess: %.2f%s",
                 m_graph->toResourceDisplayRate(resKey, port.excess_rate), spacedUnit.c_str());
        tip += ex_buf;
    }
    const bool hasNumericConstraint = port.user_constraint >= 0.0;
    const bool hasUnratedPush = (port.user_constraint < 0.0 && port.constraint_mode == ConstraintMode::PUSH);
    if (hasNumericConstraint || hasUnratedPush || port.constraint_mode != ConstraintMode::AUTO) {
        const ConstraintMode resolved = m_solver->getResolvedConstraintMode(port.id);
        const char* modeLabel = constraintModeToString(resolved);
        char mode_buf[160];
        if (hasNumericConstraint) {
            const double displayConstraint = m_graph->toResourceDisplayRate(resKey, port.user_constraint);
            const double sat = m_solver->getGoalSaturation(port.id);
            if (sat >= 0.0) {
                const bool bottlenecked = m_solver->isGoalBottlenecked(port.id);
                snprintf(mode_buf, sizeof(mode_buf), "\n%s: %.2f%s (%.1f%%%s)",
                         modeLabel, displayConstraint, spacedUnit.c_str(),
                         sat * 100.0, bottlenecked ? " - Bottlenecked" : "");
            } else {
                snprintf(mode_buf, sizeof(mode_buf), "\n%s: %.2f%s",
                         modeLabel, displayConstraint, spacedUnit.c_str());
            }
        } else if (hasUnratedPush) {
            snprintf(mode_buf, sizeof(mode_buf), "\nMode: PUSH (Unrated - No Overflow)");
        } else {
            snprintf(mode_buf, sizeof(mode_buf), "\nMode: %s", modeLabel);
        }
        tip += mode_buf;
    }
    return tip;
}

std::string FactoryNodeEditor::buildNodeTooltipText(
    const Node& node,
    const gamedata::NodeEval* nodeEval,
    const gamedata::NodeMetricsResult* nodeMetrics) const {
    const auto& gameData = m_graph->getGameData();
    const Machine* mach = gameData.findMachine(node.machine_key);
    const Recipe* rec = gameData.findRecipe(node.selected_recipe_key);

    std::string tip = node.name;
    if (mach) {
        tip += "\nMachine: " + mach->name;
    } else if (!node.machine_key.empty()) {
        tip += "\nMachine: " + node.machine_key;
    }
    char mBuf[64];
    snprintf(mBuf, sizeof(mBuf), "\nMachines: %.2f", node.machine_count);
    tip += mBuf;

    gamedata::EvalStatus evalStatus = nodeEval ? nodeEval->status : gamedata::EvalStatus::OK;
    if (node.is_unresolved) {
        evalStatus = gamedata::EvalStatus::ERROR;
    }
    if (evalStatus != gamedata::EvalStatus::OK) {
        tip += "\nStatus: ";
        tip += node.is_unresolved ? "UNRESOLVED" : evalStatusLabel(evalStatus);
    }
    if (node.is_unresolved) {
        if (!node.selected_recipe_key.empty()) {
            tip += "\nMissing/unresolved recipe: " + node.selected_recipe_key;
        }
        if (!node.machine_key.empty() && !mach) {
            tip += "\nMissing/unresolved machine: " + node.machine_key;
        }
    }

    if (nodeEval) {
        for (const auto& msg : nodeEval->messages) {
            tip += "\n[";
            tip += evalStatusLabel(msg.severity);
            tip += "] ";
            tip += msg.text;
        }
    }
    if (nodeMetrics) {
        for (const auto& msg : nodeMetrics->messages) {
            tip += "\n[";
            tip += evalStatusLabel(msg.severity);
            tip += "] ";
            tip += msg.text;
        }
    }

    if (rec && !rec->setup.empty()) {
        tip += "\nSetup:";
        for (const auto& item : rec->setup) {
            tip += "\n  " + getResourceDisplayName(gameData, item.resource) +
                   " x" + formatDoubleScalar("%.4g", item.amount);
        }
    }

    if (nodeMetrics && evalStatus != gamedata::EvalStatus::BLOCKED && evalStatus != gamedata::EvalStatus::ERROR) {
        bool headerAdded = false;
        for (const auto& mdef : gameData.metrics) {
            if (!metricHasDisplayLocation(mdef, gamedata::MetricDisplayLocation::Tooltip)) continue;
            const auto* nm = nodeMetrics->findMetric(mdef.id);
            if (!nm || !nm->enabled) continue;
            if (!headerAdded) {
                tip += "\nMetrics:";
                headerAdded = true;
            }
            std::string mLabel = mdef.label.empty() ? mdef.id : mdef.label;
            tip += "\n  " + mLabel + ": ";
            if (nm->status == gamedata::EvalStatus::ERROR) {
                tip += "Error";
                if (!nm->error_message.empty()) {
                    tip += " (" + nm->error_message + ")";
                }
            } else {
                tip += formatMetricValueString(mdef, nm->value, *m_graph);
            }
        }
    }

    return tip;
}

void FactoryNodeEditor::drawGraphTotalsSection() {
    const auto& gameData = m_graph->getGameData();
    const auto& totals = m_solver->getTotals();

    bool anyShown = false;
    for (const auto& tot : totals) {
        const gamedata::MetricDef* mdef = gameData.findMetric(tot.metric_id);
        if (!mdef || !metricHasDisplayLocation(*mdef, gamedata::MetricDisplayLocation::Totals)) {
            continue;
        }
        anyShown = true;
        std::string mLabel = mdef->label.empty() ? mdef->id : mdef->label;
        ImGui::PushID(mdef->id.c_str());

        if (!tot.has_value) {
            ImGui::Text("%s:", mLabel.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("-");
        } else if (tot.split_sign) {
            ImGui::Text("%s:", mLabel.c_str());
            std::string posStr = formatMetricValueString(*mdef, tot.positive_sum, *m_graph);
            std::string negStr = formatMetricValueString(*mdef, tot.negative_sum, *m_graph);
            std::string netStr = formatMetricValueString(*mdef, tot.net_sum, *m_graph);
            ImGui::Indent(10.0f);
            ImGui::Text("Consumed (+): %s", posStr.c_str());
            ImGui::Text("Generated (-): %s", negStr.c_str());
            ImGui::Text("Net: %s", netStr.c_str());
            ImGui::Unindent(10.0f);
        } else {
            std::string valStr = formatMetricValueString(*mdef, tot.value, *m_graph);
            ImGui::Text("%s: %s", mLabel.c_str(), valStr.c_str());
        }

        if (ImGui::IsItemHovered()) {
            const char* aggStr = "Sum";
            if (tot.aggregate == gamedata::MetricAggregate::Max) aggStr = "Max";
            else if (tot.aggregate == gamedata::MetricAggregate::Min) aggStr = "Min";
            ImGui::SetTooltip("Aggregate: %s (%zu active nodes)", aggStr, tot.contributing_nodes);
        }
        ImGui::PopID();
    }

    if (!anyShown) {
        ImGui::TextDisabled("No totals metrics.");
    }
}

void FactoryNodeEditor::drawNodePropertiesSection(uint64_t nodeId) {
    const Node* node = m_graph->getNode(nodeId);
    if (!node) {
        ImGui::TextDisabled("Node not found.");
        return;
    }

    const auto& gameData = m_graph->getGameData();
    const Machine* mach = gameData.findMachine(node->machine_key);
    const Recipe* rec = gameData.findRecipe(node->selected_recipe_key);
    const gamedata::NodeEval* nodeEval = m_graph->getNodeEval(nodeId);
    const gamedata::NodeMetricsResult* nodeMetrics = m_solver->getNodeMetrics(nodeId);

    ImGui::PushID("##props_node");
    ImGui::Text("%s", node->name.c_str());
    if (mach) {
        ImGui::TextDisabled("Machine: %s", mach->name.c_str());
    } else if (!node->machine_key.empty()) {
        ImGui::TextDisabled("Machine: %s", node->machine_key.c_str());
    }
    ImGui::Text("Count: %.2f", node->machine_count);

    gamedata::EvalStatus evalStatus = nodeEval ? nodeEval->status : gamedata::EvalStatus::OK;
    if (node->is_unresolved) evalStatus = gamedata::EvalStatus::ERROR;
    if (evalStatus != gamedata::EvalStatus::OK) {
        ImGui::TextColored(evalStatusColor(evalStatus), "Status: %s",
                           node->is_unresolved ? "UNRESOLVED" : evalStatusLabel(evalStatus));
    }
    if (nodeEval && !nodeEval->messages.empty()) {
        for (const auto& msg : nodeEval->messages) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(evalStatusColor(msg.severity), "%s: %s",
                               evalStatusLabel(msg.severity), msg.text.c_str());
            ImGui::PopTextWrapPos();
        }
    }

    if (rec && !rec->setup.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Setup:");
        for (const auto& item : rec->setup) {
            std::string setupStr = getResourceDisplayName(gameData, item.resource) +
                " x" + formatDoubleScalar("%.4g", item.amount);
            ImGui::Text("  %s", setupStr.c_str());
        }
    }

    ImGui::Spacing();
    drawNodeParameters(nodeId, /*onlyBodyPlacement=*/false, /*inNodeBody=*/false, -FLT_MIN, nullptr);

    // Parameter edits inside drawNodeParameters can re-evaluate and re-solve, invalidating cached pointers.
    node = m_graph->getNode(nodeId);
    nodeEval = m_graph->getNodeEval(nodeId);
    nodeMetrics = m_solver->getNodeMetrics(nodeId);
    evalStatus = nodeEval ? nodeEval->status : gamedata::EvalStatus::OK;
    if (node && node->is_unresolved) evalStatus = gamedata::EvalStatus::ERROR;

    if (node && nodeMetrics && evalStatus != gamedata::EvalStatus::BLOCKED && evalStatus != gamedata::EvalStatus::ERROR) {
        bool anyMetrics = false;
        for (const auto& mdef : gameData.metrics) {
            const auto* nm = nodeMetrics->findMetric(mdef.id);
            if (!nm || !nm->enabled) continue;
            if (!anyMetrics) {
                ImGui::Spacing();
                ImGui::TextDisabled("Node Metrics");
                ImGui::Separator();
                anyMetrics = true;
            }
            std::string mLabel = mdef.label.empty() ? mdef.id : mdef.label;
            if (nm->status == gamedata::EvalStatus::ERROR) {
                ImGui::TextColored(evalStatusColor(gamedata::EvalStatus::ERROR),
                                   "%s: Error", mLabel.c_str());
                if (!nm->error_message.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", nm->error_message.c_str());
                }
            } else {
                std::string valStr = formatMetricValueString(mdef, nm->value, *m_graph);
                ImGui::Text("%s: %s", mLabel.c_str(), valStr.c_str());
            }
        }
    }

    ImGui::PopID();
}

void FactoryNodeEditor::drawProjectGlobalsSection() {
    const auto& gameData = m_graph->getGameData();
    if (gameData.globals.empty()) {
        return;
    }

    ImGui::PushID("##project_globals");

    struct GlobalEntry {
        const gamedata::ParameterDef* def;
        gamedata::EvaluatedParamUi ui;
    };

    std::vector<GlobalEntry> entries;
    {
        const auto& resolved = m_graph->getResolvedGlobals();
        for (const auto& gdef : gameData.globals) {
            gamedata::EvaluatedParamUi ui = resolveEffectiveGlobalUi(gameData, resolved, gdef);
            const auto stIt = resolved.status_by_global.find(gdef.id);
            const gamedata::EvalStatus status = (stIt != resolved.status_by_global.end())
                ? stIt->second
                : gamedata::EvalStatus::OK;
            const auto msgIt = resolved.messages_by_global.find(gdef.id);
            const bool hasMessages = (msgIt != resolved.messages_by_global.end() && !msgIt->second.empty());
            if (!ui.visible && status == gamedata::EvalStatus::OK && !hasMessages) {
                continue;
            }
            entries.push_back(GlobalEntry{&gdef, std::move(ui)});
        }
    }

    if (entries.empty()) {
        ImGui::TextDisabled("No visible project globals.");
    } else {
        // Group globals by ui.group in first-appearance order (matching drawNodeParameters)
        std::vector<std::string> groupOrder;
        std::unordered_map<std::string, std::vector<size_t>> groupedIndices;
        for (size_t i = 0; i < entries.size(); ++i) {
            const std::string& grp = entries[i].def->ui.group;
            if (!groupedIndices.contains(grp)) {
                groupOrder.push_back(grp);
            }
            groupedIndices[grp].push_back(i);
        }

        for (size_t gi = 0; gi < groupOrder.size(); ++gi) {
            const std::string& grp = groupOrder[gi];
            if (!grp.empty()) {
                if (gi > 0) {
                    ImGui::Spacing();
                }
                ImGui::TextDisabled("%s", grp.c_str());
                ImGui::Separator();
            }
            for (size_t idx : groupedIndices[grp]) {
                const gamedata::ParameterDef* gdef = entries[idx].def;
                // Re-fetch resolved globals fresh in case an earlier widget in this frame mutated m_resolvedGlobals.
                const auto& curResolved = m_graph->getResolvedGlobals();
                gamedata::EvaluatedParamUi curUi = resolveEffectiveGlobalUi(gameData, curResolved, *gdef);

                if (curUi.visible) {
                    drawParameterWidget(/*isGlobal=*/true, 0, *gdef, curUi,
                                        /*inNodeBody=*/false, -FLT_MIN, nullptr);
                } else {
                    const auto stIt = curResolved.status_by_global.find(gdef->id);
                    const gamedata::EvalStatus st = (stIt != curResolved.status_by_global.end())
                        ? stIt->second
                        : gamedata::EvalStatus::OK;
                    if (st != gamedata::EvalStatus::OK) {
                        const std::string& lbl = gdef->label.empty() ? gdef->id : gdef->label;
                        ImGui::TextColored(evalStatusColor(st), "%s [%s]",
                                           lbl.c_str(), evalStatusLabel(st));
                    }
                }

                // Re-fetch again after widget edits (setGlobalParam replaces m_resolvedGlobals).
                const auto& postResolved = m_graph->getResolvedGlobals();
                if (auto msgIt = postResolved.messages_by_global.find(gdef->id);
                    msgIt != postResolved.messages_by_global.end()) {
                    for (const auto& msg : msgIt->second) {
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextColored(evalStatusColor(msg.severity), "%s: %s",
                                           evalStatusLabel(msg.severity), msg.text.c_str());
                        ImGui::PopTextWrapPos();
                    }
                }
            }
        }
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(m_graph->getGlobals().empty());
    if (ImGui::SmallButton("Reset to Defaults##reset_globals")) {
        resetProjectGlobals();
    }
    ImGui::EndDisabled();

    ImGui::PopID();
}

void FactoryNodeEditor::drawSidePanel(
    const ImVec2& panelSize,
    const std::vector<uint64_t>& selectedNodeIds) {
    uint64_t nextSelId = (selectedNodeIds.size() == 1) ? selectedNodeIds[0] : static_cast<uint64_t>(-1);
    if (nextSelId != m_lastInspectedNodeId) {
        commitActiveParamEdit();
        m_lastInspectedNodeId = nextSelId;
    }

    ImGui::BeginChild("##EditorSidePanel", panelSize, ImGuiChildFlags_Borders);
    if (!m_showSidePanel) {
        if (ImGui::SmallButton("<##expand_side_panel")) {
            m_showSidePanel = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show Totals & Properties");
        }
        ImGui::EndChild();
        return;
    }

    if (ImGui::SmallButton(">##collapse_side_panel")) {
        commitActiveParamEdit();
        m_showSidePanel = false;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Collapse panel");
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("Summary & Inspector");

    ImGui::Separator();

    const auto& gameData = m_graph->getGameData();
    const gamedata::RateUnit curUnit = m_graph->getRateUnit();
    std::string curUnitStr = gamedata::rateUnitSuffix(curUnit);
    ImGui::TextUnformatted("Rate unit:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##rate_unit", curUnitStr.c_str())) {
        const gamedata::RateUnit candidates[] = {
            gamedata::RateUnit::PerTick,
            gamedata::RateUnit::PerSecond,
            gamedata::RateUnit::PerMinute,
            gamedata::RateUnit::PerHour
        };
        for (gamedata::RateUnit u : candidates) {
            if (u == gamedata::RateUnit::PerTick &&
                (!gameData.units.has_ticks_per_second || gameData.units.ticks_per_second <= 0.0)) {
                continue;
            }
            bool sel = (u == curUnit);
            std::string uDisp = gamedata::rateUnitSuffix(u);
            if (ImGui::Selectable(uDisp.c_str(), sel) && !sel) {
                executeCommand(std::make_unique<SetRateUnitCommand>(curUnit, u));
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Graph Totals", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawGraphTotalsSection();
    }

    if (!gameData.globals.empty()) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Project Globals", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawProjectGlobalsSection();
        } else if (m_activeParamEdit.active && m_activeParamEdit.isGlobal) {
            commitActiveParamEdit();
        }
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Node Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (selectedNodeIds.empty()) {
            ImGui::TextDisabled("Select a node to edit properties.");
        } else if (selectedNodeIds.size() > 1) {
            ImGui::TextDisabled("%zu nodes selected.", selectedNodeIds.size());
        } else {
            drawNodePropertiesSection(selectedNodeIds[0]);
        }
    }

    ImGui::EndChild();
}


