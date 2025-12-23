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

    void draw();

    bool save();
    bool saveAs(const std::string& newFilePath, SaveAsMode mode = SaveAsMode::KeepCurrentFile);

    void cut(CopyBuffer &copyBuffer);
    void copy(CopyBuffer &copy_buffer);
    void paste(const CopyBuffer &copy_buffer, bool mapExternalConnections = false);

    void selectAll();

    void showFlow();

    void fitView(bool force = false);

    bool isFocused() const { return m_isFocused; }

    void undo();
    void redo();
    bool canUndo() const { return m_undoRedoManager.canUndo(); }
    bool canRedo() const { return m_undoRedoManager.canRedo(); }

    ed::EditorContext* getContext() { return m_context; }

    const DebugInfo& getDebugInfo() const { return m_debugInfo; }

    const std::string& getName() const { return m_editorName; }
    const std::filesystem::path& getProjectFilePath() const { return m_projectFilePath; }
    void setName(const std::string& newName) { m_editorName = newName; }
private:
    std::unique_ptr<FactoryGraph> m_graph;
    std::unique_ptr<FactorySolver> m_solver;
    UndoRedoManager m_undoRedoManager;
    ed::EditorContext* m_context = nullptr;

    DebugInfo m_debugInfo;

    bool m_isFocused = false;
    bool m_wasDragging = false;
    bool m_draggingNodes = false;
    ed::NodeId m_draggedNodeId;
    ImVec2 m_draggedNodeOriginalPos = ImVec2(0, 0);
    ImVec2 m_draggedNodeNewPos = ImVec2(0, 0);

    std::chrono::time_point<std::chrono::steady_clock> m_nextAutosaveTime;

    ed::NodeId m_contextNodeId;

    ed::PinId m_contextPinId;
    char m_contextPinConstraintBuf[64] = {0}; // Buffer for pin constraint input
    double m_contextPinOriginalConstraint = 0.0;
    double m_contextPinCurrentConstraint = 0.0;
    bool m_contextPinConstraintChanged = false;
    bool m_isContextMenuInitialized = false;

    char m_recipeSearchBuffer[128] = "";
    bool m_useGenerate = false;

    bool m_showFitViewConfirmation = false;

    ed::LinkId m_contextLinkId;

    ImVec2 m_storedPopupPosition;
    ImVec2 m_windowPos;
    ImVec2 m_windowSize;
    std::string m_editorName;
    std::filesystem::path m_projectFilePath;
    uint64_t m_selectedPortId = 0;

    // Quadtree for spatial optimization
    std::unique_ptr<quadtree::Quadtree<NodeQuadtreeData, GetNodeBox>> m_nodeQuadtree;
    bool m_quadtreeNeedsRebuild = true;

    void executeCommand(std::unique_ptr<Command> command);

    void updateDebugInfo();

    void drawNodes();
    void drawConnections();
    void rebuildQuadtree();
    std::vector<NodeQuadtreeData> getVisibleNodes(const ImVec2& viewMin, const ImVec2& viewMax);

    void handleUserInteractions();
    void handleContextMenus();
    void handlePopups();
};