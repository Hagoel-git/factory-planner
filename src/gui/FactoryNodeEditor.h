#pragma once
#include "imgui.h"
#include "imgui_node_editor.h"
#include "../core/FactorySolver.h"
namespace ed = ax::NodeEditor;

#include "../core/FactoryGraph.h"
#include "../core/Node.h"
#include "../core/Port.h"
#include "../core/Recipe.h"

class FactoryNodeEditor {
public:

    FactoryNodeEditor(const std::string& dataFilePath = "../../data/satisfactory.json",const std::string& title = "Factory Node Editor");

    ~FactoryNodeEditor();

    void Draw();
    const std::string& GetName() const { return name; }
    void SetName(const std::string& newName) { name = newName; }
private:
    std::unique_ptr<FactoryGraph> graph;
    std::unique_ptr<FactorySolver> solver;
    ed::EditorContext* context = nullptr;
    ed::NodeId m_contextNodeId;
    ed::PinId m_contextPinId;
    ed::LinkId m_contextLinkId;

    ImVec2 m_storedPopupPosition;
    std::vector<int> copyBuffer; // Buffer for copied nodes
    std::string name;
    std::string configFile;
    int selected_port_id = 0;

    void DrawHeader();
    void DrawToolbar();

    void HandleFirstFrame();

    void DrawNodes();
    void DrawConnections();

    void HandleUserInteractions();
    void HandleKeyboardShortcuts();
    void HandleContextMenus();
    void HandlePopups();
};
