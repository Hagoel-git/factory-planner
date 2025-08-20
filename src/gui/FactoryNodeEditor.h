#pragma once
#include "imgui.h"
#include "imgui_node_editor.h"
#include "../utils/UndoRedoSystem.h"
#include "pvigier/Quadtree.h"
namespace ed = ax::NodeEditor;

#include "../core/FactorySolver.h"
#include "../core/FactoryGraph.h"

// Structure to hold node data for quadtree
struct NodeQuadtreeData {
    int nodeId;
    ImVec2 position;
    ImVec2 size;

    NodeQuadtreeData(int id, const ImVec2& pos, const ImVec2& sz)
        : nodeId(id), position(pos), size(sz) {}
};

// Functor to get bounding box for quadtree
struct GetNodeBox {
    quadtree::Box<float> operator()(const NodeQuadtreeData& nodeData) const {
        return quadtree::Box<float>(
            quadtree::Vector2<float>(nodeData.position.x, nodeData.position.y),
            quadtree::Vector2<float>(nodeData.size.x, nodeData.size.y)
        );
    }
};

class FactoryNodeEditor {
public:

    FactoryNodeEditor(const std::string& gameDataFilePath, const std::string& projectFilePath, const std::string& title );

    ~FactoryNodeEditor();

    bool Initialize();
    bool Close();
    void Draw();
    bool Save();

    void undo() {
        if (!undoRedoManager.canUndo()) return;
        undoRedoManager.undo(*graph);
        solver->solve(*graph);
        quadtreeNeedsRebuild = true; // Mark for rebuild after undo
    }
    void redo() {
        if (!undoRedoManager.canRedo()) return;
        undoRedoManager.redo(*graph);
        solver->solve(*graph);
        quadtreeNeedsRebuild = true; // Mark for rebuild after redo
    }
    bool canUndo() const { return undoRedoManager.canUndo(); }
    bool canRedo() const { return undoRedoManager.canRedo(); }

    ed::EditorContext* GetContext() { return context; }

    const std::string& GetName() const { return name; }
    void SetName(const std::string& newName) { name = newName; }
private:
    std::unique_ptr<FactoryGraph> graph;
    std::unique_ptr<FactorySolver> solver;
    UndoRedoManager undoRedoManager;
    ed::EditorContext* context = nullptr;
    ed::NodeId m_contextNodeId;
    ed::PinId m_contextPinId;
    ed::LinkId m_contextLinkId;

    ImVec2 m_storedPopupPosition;
    ImVec2 windowPos;
    ImVec2 windowSize;
    std::vector<int> copyBuffer; // Buffer for copied nodes
    std::string name;
    std::string projectFilePath;
    std::string gameDataFilePath;
    int selected_port_id = 0;

    // Quadtree for spatial optimization
    std::unique_ptr<quadtree::Quadtree<NodeQuadtreeData, GetNodeBox>> nodeQuadtree;
    bool quadtreeNeedsRebuild = true;
    bool first_frame = true;

    void executeCommand(std::unique_ptr<Command> command) {
        if (!command) return;
        // check if it is a composite command and if it has no sub-commands, then ignore it
        if (auto compositeCommand = dynamic_cast<CompositeCommand*>(command.get())) {
            if (compositeCommand->isEmpty()) {
                return;
            }
        }
        undoRedoManager.executeCommand(std::move(command), *graph);
        solver->solve(*graph);
        quadtreeNeedsRebuild = true; // Mark for rebuild after command execution
    }

    void DrawHeader();
    void DrawToolbar();

    void HandleFirstFrame();

    void DrawNodes();
    void DrawConnections();
    void RebuildQuadtree();
    std::vector<NodeQuadtreeData> GetVisibleNodes(const ImVec2& viewMin, const ImVec2& viewMax);

    void HandleUserInteractions();
    void HandleKeyboardShortcuts();
    void HandleContextMenus();
    void HandlePopups();
};