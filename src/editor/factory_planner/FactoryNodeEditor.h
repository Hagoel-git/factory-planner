#pragma once
#include "imgui.h"
#include "imgui_node_editor.h"
#include "services/UndoRedoSystem.h"
#include "pvigier/Quadtree.h"
#include "core/graph/FactorySolver.h"
#include "core/graph/FactoryGraph.h"

struct CopyBuffer;
namespace ed = ax::NodeEditor;

enum class SaveAsMode {
    KeepCurrentFile,
    SwitchToNewFile,
};

// Structure to hold node data for quadtree
struct NodeQuadtreeData {
    uint64_t nodeId;
    ImVec2 position;
    ImVec2 size;

    NodeQuadtreeData(uint64_t id, const ImVec2& pos, const ImVec2& sz)
        : nodeId(id), position(pos), size(sz) {}
};

// Functor to get bounding box for quadtree
struct GetNodeBox {
    quadtree::Box<float> operator()(const NodeQuadtreeData& nodeData) const {
        return quadtree::Box<float>{
            quadtree::Vector2<float>(nodeData.position.x, nodeData.position.y),
            quadtree::Vector2<float>(nodeData.size.x, nodeData.size.y)
        };
    }
};

struct DebugInfo {
    std::filesystem::path filePath;
    std::filesystem::path gameDataPath;
    int totalNodes = 0;
    int totalConnections = 0;
    int totalPorts = 0;
    int visibleNodes = 0;
    int selectionSize = 0;
    double lastTotalSolveDurationMs = 0;
    double lastSetupSolveDurationMs = 0;
    double lastSolverDurationMs = 0;
    double lastUpdateFactoryDurationMs = 0;
    FactorySolver::SolverResultStatus lastSolverResult = FactorySolver::SolverResultStatus::ERROR;
    std::string lastSolverStatus;
    float quadtreeBoundsMin[2] = {0, 0};
    float quadtreeBoundsMax[2] = {0, 0};
    float viewBoundsMin[2] = {0, 0};
    float viewBoundsMax[2] = {0, 0};
    size_t undoStackSize = 0;
    size_t redoStackSize = 0;
};

class FactoryNodeEditor {
public:

    FactoryNodeEditor(const GameData& game_data, const std::string& projectFilePath, std::string  title );

    ~FactoryNodeEditor();

    void Draw();

    bool Save();
    bool SaveAs(const std::string& newFilePath, SaveAsMode mode = SaveAsMode::KeepCurrentFile);

    void cut(CopyBuffer &copyBuffer);

    void copy(CopyBuffer &copy_buffer);
    void paste(const CopyBuffer &copy_buffer, bool mapExternalConnections = false);

    void selectAll();

    void undo() {
        if (!undoRedoManager.canUndo()) return;

        const Command *command = undoRedoManager.getCommandToUndo();
        if (!command) return;

        CommandFlags flags = command->GetFlags();

        undoRedoManager.undo(*graph);

        if (flags.needsSolve) {
            FactorySolver::SolverResult result = solver->solve(*graph);
            debugInfo.lastTotalSolveDurationMs = result.total_solve_time_ms;
            debugInfo.lastSetupSolveDurationMs = result.setup_time_ms;
            debugInfo.lastSolverDurationMs = result.solve_time_ms;
            debugInfo.lastUpdateFactoryDurationMs = result.update_factory_time_ms;
            debugInfo.lastSolverResult = result.status;
        }
        quadtreeNeedsRebuild = flags.needsRebuild;
    }
    void redo() {
        if (!undoRedoManager.canRedo()) return;

        const Command *command = undoRedoManager.getCommandToRedo();
        if (!command) return;

        CommandFlags flags = command->GetFlags();

        undoRedoManager.redo(*graph);

        if (flags.needsSolve) {
            FactorySolver::SolverResult result = solver->solve(*graph);
            debugInfo.lastTotalSolveDurationMs = result.total_solve_time_ms;
            debugInfo.lastSetupSolveDurationMs = result.setup_time_ms;
            debugInfo.lastSolverDurationMs = result.solve_time_ms;
            debugInfo.lastUpdateFactoryDurationMs = result.update_factory_time_ms;
            debugInfo.lastSolverResult = result.status;
        }
        quadtreeNeedsRebuild = flags.needsRebuild;
    }
    bool canUndo() const { return undoRedoManager.canUndo(); }
    bool canRedo() const { return undoRedoManager.canRedo(); }

    ed::EditorContext* GetContext() { return context; }

    const DebugInfo& GetDebugInfo() const { return debugInfo; }

    const std::string& GetName() const { return name; }
    const std::filesystem::path& GetProjectFilePath() const { return projectFilePath; }
    const std::filesystem::path& GetGameDataFilePath() const { return graph->getGameData().gameDataFilePath; }
    void SetName(const std::string& newName) { name = newName; }
private:
    std::unique_ptr<FactoryGraph> graph;
    std::unique_ptr<FactorySolver> solver;
    UndoRedoManager undoRedoManager;
    ed::EditorContext* context = nullptr;

    DebugInfo debugInfo;

    ImTextureID textureID;

    bool wasDragging = false;
    bool draggingNodes = false;
    ed::NodeId draggedNodeId;
    ImVec2 draggedNodeOriginalPos = ImVec2(0, 0);
    ImVec2 draggedNodeNewPos = ImVec2(0, 0);

    std::chrono::time_point<std::chrono::steady_clock> nextAutosaveTime;

    ed::NodeId m_contextNodeId;

    ed::PinId m_contextPinId;
    char m_contextPinConstraintBuf[64] = {0}; // Buffer for pin constraint input
    double m_contextPinOriginalConstraint = 0.0;
    double m_contextPinCurrentConstraint = 0.0;
    bool m_contextPinConstraintChanged = false;
    bool m_isContextMenuInitialized = false;

    ed::LinkId m_contextLinkId;

    ImVec2 m_storedPopupPosition;
    ImVec2 windowPos;
    ImVec2 windowSize;
    std::string name;
    std::filesystem::path projectFilePath;
    uint64_t selected_port_id = 0;

    // Quadtree for spatial optimization
    std::unique_ptr<quadtree::Quadtree<NodeQuadtreeData, GetNodeBox>> nodeQuadtree;
    bool quadtreeNeedsRebuild = true;

    void executeCommand(std::unique_ptr<Command> command) {
        if (!command) return;
        // check if it is a composite command and if it has no sub-commands, then ignore it
        if (auto compositeCommand = dynamic_cast<CompositeCommand*>(command.get())) {
            if (compositeCommand->isEmpty()) {
                return;
            }
        }
        CommandFlags flags = command->GetFlags();
        undoRedoManager.executeCommand(std::move(command), *graph);
        if (flags.needsSolve) {
            FactorySolver::SolverResult result = solver->solve(*graph);
            debugInfo.lastTotalSolveDurationMs = result.total_solve_time_ms;
            debugInfo.lastSetupSolveDurationMs = result.setup_time_ms;
            debugInfo.lastSolverDurationMs = result.solve_time_ms;
            debugInfo.lastUpdateFactoryDurationMs = result.update_factory_time_ms;
            debugInfo.lastSolverResult = result.status;
        }
        quadtreeNeedsRebuild = flags.needsRebuild;
    }

    void DrawHeader();

    void UpdateDebugInfo();

    void DrawToolbar();

    void DrawNodes();
    void DrawConnections();
    void RebuildQuadtree();
    std::vector<NodeQuadtreeData> GetVisibleNodes(const ImVec2& viewMin, const ImVec2& viewMax);

    void HandleUserInteractions();
    void HandleContextMenus();
    void HandlePopups();
};