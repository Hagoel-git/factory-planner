//
// Created by hagoel on 8/1/25.
//

#include "FactoryGraph.h"

#include <iostream>

int FactoryGraph::addNode(const std::string& name, NodeType type, int key_id) {
    int id = next_node_id++;
    nodes.emplace_back(name, type, id, key_id);
    return id;
}

Node* FactoryGraph::getNode(int id) {
    if (id < 0 || id >= static_cast<int>(nodes.size())) {
        return nullptr; // Invalid ID
    }
    return &nodes[id];
}

const std::vector<Node>& FactoryGraph::getNodes() const {
    return nodes;
}

void FactoryGraph::addConnection(int from_node_id, int to_node_id, int resource_id, double max_flow) {
    connections.emplace_back(from_node_id, to_node_id, resource_id);
}

const std::vector<Connection>& FactoryGraph::getConnections() const {
    return connections;
}

void FactoryGraph::clear() {
    nodes.clear();
    connections.clear();
    next_node_id = 0; // Reset the node ID counter
}

void FactoryGraph::printGraph() const {
    for (const auto& node : nodes) {
        std::cout << "Node ID: " << node.id << ", Name: " << node.name
                  << ", Type: " << toString(node.type) << ", Key ID: " << node.key_id << std::endl;
    }
    for (const auto& conn : connections) {
        std::cout << "Connection from Node ID: " << conn.from_node_id
                  << " to Node ID: " << conn.to_node_id
                  << ", Resource ID: " << conn.resource_id
                  << ", Max Flow: " << conn.max_flow
                  << ", Actual Flow: " << conn.actual_flow
                  << ", Is Bottleneck: " << (conn.is_bottleneck ? "Yes" : "No") << std::endl;
    }
}

