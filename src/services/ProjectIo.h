//
// Created by hagoel on 8/17/25.
//

#ifndef PROJECTIO_H
#define PROJECTIO_H


#include <filesystem>
#include <string>
#include "imgui-node-editor/imgui_node_editor.h"

class FactoryGraph;

class ProjectIO {
public:
    static bool saveProject(const std::string &path, const FactoryGraph &factoryGraph, ax::NodeEditor::EditorContext* context = nullptr);
    static bool loadProject(const std::string &path, FactoryGraph &factoryGraph);

};



#endif //PROJECTIO_H
