#ifndef COPYBUFFER_H
#define COPYBUFFER_H

#include <unordered_map>
#include <unordered_set>
#include <filesystem>

#include "core/graph/Node.h"
#include "core/graph/Port.h"
#include "core/graph/Connection.h"

struct ImVec2;
class FactoryNodeEditor;

struct CopyBuffer {
    std::unordered_map<uint64_t, Node> nodes;    // id->Node; copies of Node (value-copied)
    std::unordered_map<uint64_t, Port> ports;    // id->Port copies of Port (value-copied) for quick lookup
    std::unordered_set<Connection> connections;    // copies of Connection that touch selected nodes
    std::unordered_map<uint64_t, ImVec2> nodePositions; // nodeId -> canvas position (ed::GetNodePosition)

    std::string gameName;
    const FactoryNodeEditor* sourceEditor = nullptr;

    bool isEmpty() const {
        return nodes.empty();
    }

    void clear() {
        nodes.clear();
        ports.clear();
        connections.clear();
        nodePositions.clear();
        gameName.clear();
        sourceEditor = nullptr;
    }

};
#endif //COPYBUFFER_H