//
// Created by hagoel on 8/6/25.
//

#ifndef FACTORYNODEEDITOR_H
#define FACTORYNODEEDITOR_H
#include "imgui.h"
#include "imgui_node_editor.h"
#include "../core/FactoryGraph.h"
#include "../core/FactorySolver.h"

namespace ed = ax::NodeEditor;

class FactoryNodeEditor {
public:
    bool Initialize();
    void Shutdown();

    void SetGraph(FactoryGraph* graph);
    void SetSolver(FactorySolver* solver);

    void Show();

    FactoryNodeEditor();
    ~FactoryNodeEditor();

private:
    ed::EditorContext *m_ctx = nullptr;
    FactoryGraph *m_factoryGraph = nullptr;
    FactorySolver *m_factorySolver = nullptr;

    void ShowEditor();


    void RenderNodes();
    void RenderConnections();
    void HandleInteractions();

    void HandleDoubleClick();
    void HandleConnectionDrag();
    void HandleNodeCreation(ImVec2 pos);
    void HandleNodeDeletion();

};


#endif //FACTORYNODEEDITOR_H
