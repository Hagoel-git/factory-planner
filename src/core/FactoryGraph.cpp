//
// Created by hagoel on 8/1/25.
//

#include "FactoryGraph.h"

#include <iostream>
#include <unordered_set>

int FactoryGraph::addNode(const std::string &name, NodeType type, int key_id, int recipe_id) {
    int id = next_node_id++;
    nodes.emplace_back(name, type, id, key_id);
    setNodeRecipe(id, recipe_id);
    return id;
}

bool FactoryGraph::setNodeRecipe(int node_id, int recipe_id) {
    Node* node = getNode(node_id);
    if (!node) {
        std::cerr << "Node with ID " << node_id << " does not exist." << std::endl;
        return false; // Node does not exist
    }
    if (recipe_id > game_data.recipes.size()) {
        return false;
    }
    const Recipe& recipe = game_data.recipes[recipe_id];
    node->selected_recipe_id = recipe_id;
    node->output_rates.resize(recipe.getOutputPortCount());
    node->input_rates.resize(recipe.getInputPortCount());
    node->required_output_rates.resize(recipe.getOutputPortCount());
    node->required_input_rates.resize(recipe.getInputPortCount());
    return true;
}

bool FactoryGraph::setNodeDemand(int node_id, int output_port, double demand) {
    Node* node = getNode(node_id);
    if (!node) {
        std::cerr << "Node with ID " << node_id << " does not exist." << std::endl;
        return false;
    }

    int recipe_id = node->selected_recipe_id;
    const Recipe& recipe = game_data.recipes[recipe_id];

    if (output_port >= recipe.getOutputPortCount()) return false;
    node->required_output_rates[output_port] = demand;
    return true;
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

bool FactoryGraph::isValidConnection(int from_node_id, int to_node_id, int from_port, int to_port) {
    Node* from_node = getNode(from_node_id);
    Node* to_node = getNode(to_node_id);

    if (!from_node || !to_node) {
        std::cerr << "Invalid node ID(s) in connection." << std::endl;
        return false; // Invalid node ID
    }

    Recipe from_recipe = game_data.recipes[from_node->selected_recipe_id];
    Recipe to_recipe = game_data.recipes[to_node->selected_recipe_id];

    if (from_port >= from_recipe.getInputPortCount() || to_port >= to_recipe.getOutputPortCount()) {
        std::cerr << "Invalid port number(s) in connection." << std::endl;
        return false; // Invalid port number
    }

    // Check if the connection is valid based on resource compatibility
    return from_recipe.getOutputPortResourceId(from_port) == to_recipe.getInputPortResourceId(to_port);
}


bool FactoryGraph::addConnection(int from_node_id, int to_node_id, int from_port, int to_port) {
    if (!isValidConnection(from_node_id, to_node_id, from_port, to_port)) {
        std::cerr << "Invalid connection from Node ID " << from_node_id << " to Node ID " << to_node_id
                  << " on ports " << from_port << " to " << to_port << "." << std::endl;
        return false; // Invalid connection
    }
    Node* from_node = getNode(from_node_id);
    Recipe* from_recipe = &game_data.recipes[from_node->selected_recipe_id];
    connections.emplace_back(from_node_id, to_node_id, from_port, to_port, from_recipe->getOutputPortResourceId(from_port));
    return true; // Connection added successfully
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
        nodes_with_outgoing_connections.insert(conn.from_node);
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
        node.print();
        Recipe recipe = game_data.recipes[node.selected_recipe_id];
        std::cout << "Recipe ID: " << recipe.id << ", Name: " << recipe.name
                  << ", Time: " << recipe.time << " seconds" << std::endl;
        std::cout << "----------------------------------------" << std::endl;
    }
    for (const auto &conn: connections) {
        std::cout << "Connection from Node ID: " << conn.from_node
                << " to Node ID: " << conn.to_node
                << " from Port: " << conn.from_port
                << " to Port: " << conn.to_port
                << ", Resource ID: " << conn.resource_id
                << ", Is Bottleneck: " << (conn.is_bottleneck ? "Yes" : "No") << std::endl;
    }
}
