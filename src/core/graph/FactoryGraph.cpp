//
// Created by hagoel on 8/1/25.
//

#include <nlohmann/json.hpp>
#include "FactoryGraph.h"
#include <iostream>
#include <unordered_set>

int FactoryGraph::addNode(const std::string &name, NodeType type, std::string recipe_key) {
    int id = next_node_id++;
    size_t index = nodes.size();
    nodes.emplace_back(name, type, id);
    addNodeToIndex(id, index);
    setNodeRecipe(id, recipe_key);
    return id;
}

void FactoryGraph::restoreNode(const Node &node, const std::vector<Port> &ports) {
    nodes.push_back(node);
    size_t index = nodes.size() - 1;
    addNodeToIndex(node.id, index);
    for (auto port : ports) {
        this->ports.push_back(port);
        addPortToIndex(port.id, this->ports.size() - 1);
    }
}

bool FactoryGraph::removeNode(int node_id) {
    auto map_it = node_id_to_index_.find(node_id);
    if (map_it == node_id_to_index_.end()) {
        return false;
    }

    size_t index = map_it->second;
    Node& node = nodes[index];

    // Remove all associated ports (these functions already handle their own swap-and-pop)
    for (int port_id : node.input_ports) {
        removePort(port_id);
    }
    for (int port_id : node.output_ports) {
        removePort(port_id);
    }

    // If it's not the last node, swap with the last
    size_t last_index = nodes.size() - 1;
    if (index != last_index) {
        std::swap(nodes[index], nodes[last_index]);

        // Update index of swapped node
        int swapped_id = nodes[index].id;
        node_id_to_index_[swapped_id] = index;
    }

    // Remove from index map
    removeNodeFromIndex(node_id);

    // Pop from vector
    nodes.pop_back();

    return true;
}


Node *FactoryGraph::getNode(int id) {
    auto it = node_id_to_index_.find(id);
    return (it != node_id_to_index_.end()) ? &nodes[it->second] : nullptr;
}

int FactoryGraph::addPort(std::string resource_key, int node_id, bool isInput) {
    int port_id = next_port_id++;
    size_t index = ports.size();
    ports.emplace_back(port_id, node_id, resource_key, isInput);
    addPortToIndex(port_id, index);
    return port_id;
}

bool FactoryGraph::removePort(int port_id) {
    auto map_it = port_id_to_index_.find(port_id);
    if (map_it == port_id_to_index_.end()) {
        return false;
    }

    size_t index = map_it->second;

    // Remove all connections involving this port
    // We'll use removeConnection() so it keeps indices consistent
    auto range = connectionsByPort.equal_range(port_id);
    std::vector<int> connections_to_remove;
    for (auto it = range.first; it != range.second; ++it) {
        connections_to_remove.push_back(it->second);
    }
    for (int conn_id : connections_to_remove) {
        const auto& conn = connections[connection_id_to_index_[conn_id]];
        removeConnection(conn.from_port, conn.to_port);
    }

    // If it's not the last port, swap with the last
    size_t last_index = ports.size() - 1;
    if (index != last_index) {
        std::swap(ports[index], ports[last_index]);

        // Update index of swapped port
        int swapped_id = ports[index].id;
        port_id_to_index_[swapped_id] = index;
    }

    // Remove from index map
    removePortFromIndex(port_id);

    // Remove from vector
    ports.pop_back();

    return true;
}


Port *FactoryGraph::getPort(int id) {
    auto it = port_id_to_index_.find(id);
    return (it != port_id_to_index_.end()) ? &ports[it->second] : nullptr;
}

int FactoryGraph::addConnection(int from_port, int to_port) {
    if (!isValidConnection(from_port, to_port)) {
        return -1;
    }
    int id = next_connection_id++;
    size_t index = connections.size();
    connections.emplace_back(id, from_port, to_port, getPort(from_port)->resource_key);
    addConnectionToIndex(id, index);
    connectionsByPort.insert({from_port, id});
    connectionsByPort.insert({to_port, id});
    return id;
}

void FactoryGraph::restoreConnection(const Connection &connection) {
    connections.push_back(connection);
    size_t index = connections.size() - 1;
    addConnectionToIndex(connection.id, index);
    connectionsByPort.insert({connection.from_port, connection.id});
    connectionsByPort.insert({connection.to_port, connection.id});
}

bool FactoryGraph::removeConnection(int from_port, int to_port) {
    for (size_t index = 0; index < connections.size(); ++index) {
        if (connections[index].from_port == from_port &&
            connections[index].to_port == to_port) {

            int connection_id = connections[index].id;

            // Remove from connectionsByPort before erasing
            auto range_from = connectionsByPort.equal_range(from_port);
            for (auto multi_it = range_from.first; multi_it != range_from.second; ++multi_it) {
                if (multi_it->second == connection_id) {
                    connectionsByPort.erase(multi_it);
                    break;
                }
            }
            auto range_to = connectionsByPort.equal_range(to_port);
            for (auto multi_it = range_to.first; multi_it != range_to.second; ++multi_it) {
                if (multi_it->second == connection_id) {
                    connectionsByPort.erase(multi_it);
                    break;
                }
            }

            // If it's not the last element, swap with the last
            size_t last_index = connections.size() - 1;
            if (index != last_index) {
                std::swap(connections[index], connections[last_index]);

                // Update index map for the swapped element
                int swapped_id = connections[index].id;
                connection_id_to_index_[swapped_id] = index;
            }

            // Remove from index map
            removeConnectionFromIndex(connection_id);

            // Actually remove the element
            connections.pop_back();

            return true;
            }
    }
    return false;
}

Connection *FactoryGraph::getConnection(int id) {
    auto it = connection_id_to_index_.find(id);
    return (it != connection_id_to_index_.end()) ? &connections[it->second] : nullptr;
}

Connection *FactoryGraph::getConnection(int from_port, int to_port) {
    auto range = connectionsByPort.equal_range(from_port);
    for (auto it = range.first; it != range.second; ++it) {
        int conn_id = it->second;
        auto idx_it = connection_id_to_index_.find(conn_id);
        if (idx_it == connection_id_to_index_.end()) continue;
        const Connection &c = connections[idx_it->second];
        if (c.to_port == to_port) {
            return getConnection(conn_id);
        }
    }
    return nullptr;
}

std::vector<Connection *> FactoryGraph::getConnectionsForPort(int id) {
    std::vector<Connection*> result;
    auto range = connectionsByPort.equal_range(id);
    for (auto it = range.first; it != range.second; ++it) {
        result.push_back(getConnection(it->second));
    }
    return result;
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


nlohmann::json FactoryGraph::serialize() const {
    nlohmann::json j;

    // Serialize nodes
    j["nodes"] = nlohmann::json::array();
    for (const auto& node : nodes) {
        nlohmann::json node_json;
        node_json["id"] = node.id;
        node_json["name"] = node.name;
        node_json["type"] = node.type;
        node_json["machine_key"] = node.machine_key;
        node_json["selected_recipe_key"] = node.selected_recipe_key;
        node_json["input_ports"] = node.input_ports;
        node_json["output_ports"] = node.output_ports;
        j["nodes"].push_back(node_json);
    }

    // Serialize ports
    j["ports"] = nlohmann::json::array();
    for (const auto& port : ports) {
        nlohmann::json port_json;
        port_json["id"] = port.id;
        port_json["node_id"] = port.node_id;
        port_json["resource_key"] = port.resource_key;
        port_json["isInput"] = port.isInput;
        port_json["user_constraint"] = port.user_constraint;
        j["ports"].push_back(port_json);
    }

    // Serialize connections
    j["connections"] = nlohmann::json::array();
    for (const auto& connection : connections) {
        nlohmann::json conn_json;
        conn_json["id"] = connection.id;
        conn_json["from_port"] = connection.from_port;
        conn_json["to_port"] = connection.to_port;
        conn_json["resource_key"] = connection.resource_key;
        j["connections"].push_back(conn_json);
    }

    // Serialize ID counters
    j["next_node_id"] = next_node_id;
    j["next_port_id"] = next_port_id;
    j["next_connection_id"] = next_connection_id;
    j["game_data_path"] = game_data.gameDataFilePath;

    return j;
}

void FactoryGraph::deserialize(const nlohmann::json& j) {
    // Clear existing data
    clear();

    // Deserialize nodes
    if (j.contains("nodes")) {
        for (const auto& node_json : j["nodes"]) {
            Node node;
            node.id = node_json["id"];
            node.name = node_json["name"];
            node.type = static_cast<NodeType>(node_json["type"]);
            node.machine_key = node_json["machine_key"];
            node.selected_recipe_key = node_json["selected_recipe_key"];
            node.input_ports = node_json["input_ports"].get<std::vector<int>>();
            node.output_ports = node_json["output_ports"].get<std::vector<int>>();

            nodes.push_back(node);
            addNodeToIndex(node.id, nodes.size() - 1);
        }
    }

    // Deserialize ports
    if (j.contains("ports")) {
        for (const auto& port_json : j["ports"]) {
            Port port(port_json["id"], port_json["node_id"], port_json["resource_key"], port_json["isInput"]);
            port.user_constraint = port_json["user_constraint"];

            ports.push_back(port);
            addPortToIndex(port.id, ports.size() - 1);
        }
    }

    // Deserialize connections
    if (j.contains("connections")) {
        for (const auto& conn_json : j["connections"]) {
            Connection connection(conn_json["id"], conn_json["from_port"],
                                conn_json["to_port"], conn_json["resource_key"]);

            connections.push_back(connection);
            addConnectionToIndex(connection.id, connections.size() - 1);
            connectionsByPort.insert({connection.from_port, connection.id});
            connectionsByPort.insert({connection.to_port, connection.id});
        }
    }

    // Deserialize ID counters
    if (j.contains("next_node_id")) {
        next_node_id = j["next_node_id"];
    }
    if (j.contains("next_port_id")) {
        next_port_id = j["next_port_id"];
    }
    if (j.contains("next_connection_id")) {
        next_connection_id = j["next_connection_id"];
    }
    if (j.contains("game_data_path")) {
        std::string path = j["game_data_path"];
        GameDataManager gdm;
        std::string load_error;
        gdm.loadFromFile(path, load_error);
        if (!load_error.empty()) {
            std::cerr << "Error loading game data from " << path << ": " << std::endl << load_error << std::endl;
        }
        game_data = gdm.current();
    }
}

bool FactoryGraph::setNodeRecipe(int node_id, std::string recipe_key) {
    Node *node = getNode(node_id);
    if (!node) {
        return false;
    }
    if (game_data.recipes.count(recipe_key) == 0) {
        return false;
    }
    const Recipe &recipe = game_data.recipes.at(recipe_key);
    node->selected_recipe_key = recipe_key;
    node->machine_key = recipe.produced_in_machines_keys.empty() ? "" : recipe.produced_in_machines_keys[0]; // todo: allow selection of machine
    node->output_ports.resize(recipe.output_ports.size());
    node->input_ports.resize(recipe.input_ports.size());
    for (int i = 0; i < recipe.input_ports.size(); ++i) {
        int port_id = addPort(recipe.input_ports.at(i).resource_key, node_id, true);
        node->input_ports[i] = port_id;
    }
    for (int i = 0; i < recipe.output_ports.size(); ++i) {
        int port_id = addPort(recipe.output_ports.at(i).resource_key, node_id, false);
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
        return false;
    }
    if (from_port_ptr->resource_key != to_port_ptr->resource_key) {
        auto resource_names = game_data.resources;
        return false;
    }
    if (from_port_ptr->isInput || !to_port_ptr->isInput) {
        return false;
    }
    return true;
}

bool FactoryGraph::connectionExists(int from_port, int to_port) {
    auto range = connectionsByPort.equal_range(from_port);
    for (auto it = range.first; it != range.second; ++it) {
        int conn_id = it->second;
        auto idx_it = connection_id_to_index_.find(conn_id);
        if (idx_it == connection_id_to_index_.end()) continue;
        const Connection &c = connections[idx_it->second];
        if (c.to_port == to_port) return true;
    }
    return false;
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
        return false;
    }
    port->user_constraint = demand;
    return true;
}