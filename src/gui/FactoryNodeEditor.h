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
    FactoryGraph& graph;
    FactorySolver& solver;
    ed::EditorContext* context = nullptr;
    ed::NodeId m_contextNodeId;
    ed::PinId m_contextPinId;
    ed::LinkId m_contextLinkId;

    FactoryNodeEditor(FactoryGraph& g, FactorySolver& s);

    ~FactoryNodeEditor();

    void Draw();
private:
    int selected_port_id = 0;
};
