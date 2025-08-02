//
// Created by hagoel on 8/1/25.
//

#include "FactoryGraph.h"

#include <iostream>
#include <unordered_set>

int FactoryGraph::addNode(const std::string &name, NodeType type, int key_id) {
    int id = next_node_id++;
    nodes.emplace_back(name, type, id, key_id);
    return id;
}

Node *FactoryGraph::getNode(int id) {
    if (id < 0 || id >= static_cast<int>(nodes.size())) {
        return nullptr; // Invalid ID
    }
    return &nodes[id];
}

const std::vector<Node> &FactoryGraph::getNodes() const {
    return nodes;
}

void FactoryGraph::addConnection(int from_node_id, int to_node_id, int resource_id, double max_flow) {
    connections.emplace_back(from_node_id, to_node_id, resource_id);
}

const std::vector<Connection> &FactoryGraph::getConnections() const {
    return connections;
}

void FactoryGraph::clear() {
    nodes.clear();
    connections.clear();
    next_node_id = 0; // Reset the node ID counter
}

bool FactoryGraph::loadGameData(const std::string &jsonFile) {
    try {
        game_data = GameDataLoader::loadGameData(jsonFile);
        return true;
    } catch (const std::exception &e) {
        std::cerr << "Error loading game data: " << e.what() << std::endl;
        return false;
    }
}

std::vector<int> FactoryGraph::findFinalNodes() {
    std::unordered_set<int> nodes_with_outgoing_connections;
    for (const auto &conn : connections) {
        nodes_with_outgoing_connections.insert(conn.from);
    }

    std::vector<int> final_nodes;
    for (const auto &node : nodes) {
        if (nodes_with_outgoing_connections.find(node.id) == nodes_with_outgoing_connections.end()) {
            final_nodes.push_back(node.id);
        }
    }
    return final_nodes;
}

void FactoryGraph::printGraph() const {
    for (const auto &node: nodes) {
        std::cout << "Node ID: " << node.id << ", Name: " << node.name
                << ", Type: " << toString(node.type) << ", Key ID: " << node.key_id << std::endl;
    }
    for (const auto &conn: connections) {
        std::cout << "Connection from Node ID: " << conn.from
                << " to Node ID: " << conn.to
                << ", Resource ID: " << conn.resource_id
                << ", Max Flow: " << conn.max_flow
                << ", Actual Flow: " << conn.flow_rate
                << ", Is Bottleneck: " << (conn.is_bottleneck ? "Yes" : "No") << std::endl;
    }
}
