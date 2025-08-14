//
// Created by hagoel on 8/1/25.
//

#include "FactoryGraph.h"
#include <iostream>
#include <unordered_set>

int FactoryGraph::addNode(const std::string &name, NodeType type, int recipe_id) {
    int id = next_node_id++;
    size_t index = nodes.size();
    nodes.emplace_back(name, type, id);
    addNodeToIndex(id, index);
    setNodeRecipe(id, recipe_id);
    return id;
}

int FactoryGraph::removeNode(int node_id) {
    std::cout << "Removing node with ID: " << node_id << std::endl;

    auto map_it = node_id_to_index_.find(node_id);
    if (map_it == node_id_to_index_.end()) {
        std::cerr << "Node with ID " << node_id << " does not exist." << std::endl;
        return false;
    }

    size_t index = map_it->second;
    Node& node = nodes[index];

    // Remove all associated ports
    for (int port_id : node.input_ports) {
        removePort(port_id);
    }
    for (int port_id : node.output_ports) {
        removePort(port_id);
    }

    // Update indices for elements after the removed one
    for (auto& [id, idx] : node_id_to_index_) {
        if (idx > index) {
            idx--;
        }
    }

    // Remove from hash map and vector
    removeNodeFromIndex(node_id);
    nodes.erase(nodes.begin() + index);
    return true;
}

Node *FactoryGraph::getNode(int id) {
    auto it = node_id_to_index_.find(id);
    return (it != node_id_to_index_.end()) ? &nodes[it->second] : nullptr;
}

int FactoryGraph::addPort(int resource_id, bool isInput) {
    int port_id = next_port_id++;
    size_t index = ports.size();
    ports.emplace_back(port_id, resource_id, isInput);
    addPortToIndex(port_id, index);
    return port_id;
}

bool FactoryGraph::removePort(int port_id) {
    std::cout << "Removing port with ID: " << port_id << std::endl;

    auto map_it = port_id_to_index_.find(port_id);
    if (map_it == port_id_to_index_.end()) {
        return false;
    }

    size_t index = map_it->second;

    // Remove all connections involving this port
    connections.erase(
        std::remove_if(connections.begin(), connections.end(),
            [port_id](const Connection& conn) {
                return conn.from_port == port_id || conn.to_port == port_id;
            }),
        connections.end()
    );

    // Rebuild connection index after removal
    connection_id_to_index_.clear();
    for (size_t i = 0; i < connections.size(); ++i) {
        addConnectionToIndex(connections[i].id, i);
    }

    // Update indices for elements after the removed one
    for (auto& [id, idx] : port_id_to_index_) {
        if (idx > index) {
            idx--;
        }
    }

    // Remove from hash map and vector
    removePortFromIndex(port_id);
    ports.erase(ports.begin() + index);
    return true;
}

Port *FactoryGraph::getPort(int id) {
    auto it = port_id_to_index_.find(id);
    return (it != port_id_to_index_.end()) ? &ports[it->second] : nullptr;
}

bool FactoryGraph::addConnection(int from_port, int to_port) {
    if (!isValidConnection(from_port, to_port)) {
        std::cerr << "Invalid connection from port " << from_port << " to " << to_port << "." << std::endl;
        return false;
    }
    int id = next_connection_id++;
    size_t index = connections.size();
    connections.emplace_back(id, from_port, to_port, getPort(from_port)->resource_id);
    addConnectionToIndex(id, index);
    return true;
}

bool FactoryGraph::removeConnection(int from_port, int to_port) {
    std::cout << "Removing connection from port " << from_port << " to port " << to_port << std::endl;

    for (auto it = connections.begin(); it != connections.end(); ++it) {
        if (it->from_port == from_port && it->to_port == to_port) {
            int connection_id = it->id;
            size_t index = std::distance(connections.begin(), it);

            // Update indices for elements after the removed one
            for (auto& [id, idx] : connection_id_to_index_) {
                if (idx > index) {
                    idx--;
                }
            }

            removeConnectionFromIndex(connection_id);
            connections.erase(it);
            return true;
        }
    }
    return false;
}

Connection *FactoryGraph::getConnection(int id) {
    auto it = connection_id_to_index_.find(id);
    return (it != connection_id_to_index_.end()) ? &connections[it->second] : nullptr;
}

void FactoryGraph::clear() {
    nodes.clear();
    ports.clear();
    connections.clear();
    node_id_to_index_.clear();
    port_id_to_index_.clear();
    connection_id_to_index_.clear();
    next_port_id = 0;
    next_node_id = 0;
    next_connection_id = 0;
}

// Keep the rest of the methods unchanged
bool FactoryGraph::setNodeRecipe(int node_id, int recipe_id) {
    Node *node = getNode(node_id);
    if (!node) {
        std::cerr << "Node with ID " << node_id << " does not exist." << std::endl;
        return false;
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
        int port_id = addPort(recipe.getInputPortResourceId(i), true);
        node->input_ports[i] = port_id;
    }
    for (int i = 0; i < recipe.getOutputPortCount(); ++i) {
        int port_id = addPort(recipe.getOutputPortResourceId(i), false);
        node->output_ports[i] = port_id;
    }
    return true;
}

const std::vector<Node> &FactoryGraph::getNodes() const {
    return nodes;
}

bool FactoryGraph::isValidConnection(int from_port, int to_port) {
    Port *from_port_ptr = getPort(from_port);
    Port *to_port_ptr = getPort(to_port);
    if (!from_port_ptr || !to_port_ptr) {
        std::cerr << "Invalid port IDs: from_port=" << from_port << ", to_port=" << to_port << std::endl;
        return false;
    }
    if (from_port_ptr->resource_id != to_port_ptr->resource_id) {
        auto resource_names = game_data.resources;
        std::cerr << "Resource mismatch: " << resource_names[from_port_ptr->resource_id].name
                  << " but expected resource is: " << resource_names[to_port_ptr->resource_id].name;
        return false;
    }
    if (from_port_ptr->isInput || !to_port_ptr->isInput) {
        std::cerr << "Invalid connection: from_port " << from_port << " is an input port or to_port " << to_port << " is not an input port." << std::endl;
        return false;
    }
    return true;
}

bool FactoryGraph::connectionExists(int from_port, int to_port) {
    return std::any_of(connections.begin(), connections.end(),
        [from_port, to_port](const Connection& conn) {
            return conn.from_port == from_port && conn.to_port == to_port;
        });
}

const std::vector<Connection> &FactoryGraph::getConnections() const {
    return connections;
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

void FactoryGraph::printGraph() {
    for (const auto &node: nodes) {
        std::cout << "Node ID: " << node.id << ", Name: " << node.name
                << ", Type: " << toString(node.type)
                << ", Machine ID: " << node.machine_id
                << ", Selected Recipe ID: " << node.selected_recipe_id
                << ", Machine Count: " << node.machine_count
                << ", Power Usage: " << node.power_usage << " MW" << std::endl;
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