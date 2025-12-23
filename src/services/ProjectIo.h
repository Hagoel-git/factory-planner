//
// Created by hagoel on 8/17/25.
//

#ifndef PROJECTIO_H
#define PROJECTIO_H


#include <filesystem>
#include <nlohmann/json.hpp>
#include "imgui-node-editor/imgui_node_editor.h"

using json = nlohmann::json;

namespace ed = ax::NodeEditor;
class FactoryGraph;

class ProjectIO {
public:
    static bool saveProject(const std::string &path, const FactoryGraph &factoryGraph, ed::EditorContext* context = nullptr);
    static bool loadProject(const std::string &path, FactoryGraph &factoryGraph);

};



#endif //PROJECTIO_H
