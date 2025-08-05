//
// Created by hagoel on 8/1/25.
//

#include "FactoryGraph.h"

#include <iostream>
#include <unordered_set>

int FactoryGraph::addNode(const std::string &name, NodeType type, int recipe_id) {
    int id = next_node_id++;
    nodes.emplace_back(name, type, id);
    setNodeRecipe(id, recipe_id);
    return id;
}

bool FactoryGraph::setNodeRecipe(int node_id, int recipe_id) {
    Node *node = getNode(node_id);
    if (!node) {
        std::cerr << "Node with ID " << node_id << " does not exist." << std::endl;
        return false; // Node does not exist
    }
    if (recipe_id > game_data.recipes.size()) {
        return false;
    }
    const Recipe &recipe = game_data.recipes[recipe_id];
    node->selected_recipe_id = recipe_id;
    node->machine_id = recipe.category_id;
    node->output_ports.resize(recipe.getOutputPortCount());
    node->input_ports.resize(recipe.getInputPortCount());
    for (int i = 0; i < recipe.getInputPortCount(); ++i) {
        int port_id = addPort(recipe.getInputPortResourceId(i));
        node->input_ports[i] = port_id;
    }
    for (int i = 0; i < recipe.getOutputPortCount(); ++i) {
        int port_id = addPort(recipe.getOutputPortResourceId(i));
        node->output_ports[i] = port_id;
    }
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


bool FactoryGraph::isValidConnection(int from_port, int to_port) {
    Port *from_port_ptr = getPort(from_port);
    Port *to_port_ptr = getPort(to_port);
    if (!from_port_ptr || !to_port_ptr) {
        std::cerr << "Invalid port IDs: from_port=" << from_port << ", to_port=" << to_port << std::endl;
        return false; // Invalid port IDs
    }
    if (from_port_ptr->resource_id != to_port_ptr->resource_id) {
        auto resource_names = game_data.resources;
        std::cerr << "Resource mismatch: " << resource_names[from_port_ptr->resource_id].name
                  << " but expected resource is: " << resource_names[to_port_ptr->resource_id].name;
        return false; // Resource IDs do not match
    }
    return true; // Valid connection
}

bool FactoryGraph::addConnection(int from_port, int to_port) {
    if (!isValidConnection(from_port, to_port)) {
        std::cerr << "Invalid connection from port " << from_port << " to " << to_port << "." << std::endl;
        return false; // Invalid connection
    }
    connections.emplace_back(from_port, to_port, getPort(from_port)->resource_id);
    return true; // Connection added successfully
}

const std::vector<Connection> &FactoryGraph::getConnections() const {
    return connections;
}


int FactoryGraph::addPort(int resource_id) {
    int port_id = next_port_id++;
    ports.emplace_back(port_id, resource_id);
    return port_id; // Return the ID of the newly created port
}

Port *FactoryGraph::getPort(int id) {
    if (id < 0 || id >= static_cast<int>(ports.size())) {
        return nullptr; // Invalid ID
    }
    return &ports[id];
}

const std::vector<Port> &FactoryGraph::getPorts() const {
    return ports;
}

bool FactoryGraph::setPortDemand(int port_id, double demand) {
    Port *port = getPort(port_id);
    if (!port) {
        std::cerr << "Port with ID " << port_id << " does not exist." << std::endl;
        return false;
    }

    port->user_constraint = demand;
    return true;
}

void FactoryGraph::clear() {
    nodes.clear();
    ports.clear();
    connections.clear();
    next_port_id = 0; // Reset the port ID counter
    next_node_id = 0; // Reset the node ID counter
}


void FactoryGraph::printGraph() {
    for (const auto &node: nodes) {
        std::cout << "Node ID: " << node.id << ", Name: " << node.name
                << ", Type: " << toString(node.type)
                << ", Machine ID: " << node.machine_id
                << ", Selected Recipe ID: " << node.selected_recipe_id
                << ", Machine Count: " << node.machine_count
                << ", Power Usage: " << node.power_usage << " MW" << std::endl;
        // Machine machine = game_data.machines[node.machine_id];
        // std::cout << "Machine Base Power Usage: " << machine.base_power_usage
        //         << " MW, Max Somersloop Slots: " << machine.max_somersloop_slots
        //         << ", Category ID: " << machine.category_id << std::endl;
        Recipe recipe = game_data.recipes[node.selected_recipe_id];
        std::cout << "Recipe ID: " << recipe.id << ", Name: " << recipe.name
                << " Category ID: " << recipe.category_id
                << ", Time: " << recipe.time << " seconds" << std::endl;
        for (const auto &input_port: node.input_ports) {
            Port *port = getPort(input_port);
            if (port) {
                std::cout << "Input Port ID: " << port->id
                        << ", Resource ID: " << port->resource_id
                        << ", Rate: " << port->rate << std::endl;
            }
        }
        for (const auto &output_port: node.output_ports) {
            Port *port = getPort(output_port);
            if (port) {
                std::cout << "Output Port ID: " << port->id
                        << ", Resource ID: " << port->resource_id
                        << ", Rate: " << port->rate << std::endl;
            }
        }
        std::cout << "----------------------------------------" << std::endl;
    }
}
