#include "FactoryGraph.h"
#include "GameDataManager.h"
#include <iostream>
#include <unordered_set>
#include <utility>
#include <cstdint>

uint64_t FactoryGraph::addNode(const std::string &name, std::string recipe_key) {
    uint64_t id = next_node_id++;
    size_t index = nodes.size();
    nodes.emplace_back(name, id);
    addNodeToIndex(id, index);
    setNodeRecipe(id, std::move(recipe_key));
    return id;
}

void FactoryGraph::restoreNode(const Node &node, const std::vector<Port> &ports) {
    nodes.push_back(node);
    size_t index = nodes.size() - 1;
    addNodeToIndex(node.id, index);
    for (const auto& port : ports) {
        this->ports.push_back(port);
        addPortToIndex(port.id, this->ports.size() - 1);
    }
}

bool FactoryGraph::removeNode(uint64_t node_id) {
    auto map_it = node_id_to_index_.find(node_id);
    if (map_it == node_id_to_index_.end()) {
        return false;
    }

    size_t index = map_it->second;
    Node& node = nodes[index];

    // Remove all associated ports (these functions already handle their own swap-and-pop)
    for (uint64_t port_id : node.input_ports) {
        removePort(port_id);
    }
    for (uint64_t port_id : node.output_ports) {
        removePort(port_id);
    }

    // If it's not the last node, swap with the last
    size_t last_index = nodes.size() - 1;
    if (index != last_index) {
        std::swap(nodes[index], nodes[last_index]);

        // Update index of swapped node
        uint64_t swapped_id = nodes[index].id;
        node_id_to_index_[swapped_id] = index;
    }

    // Remove from index map
    removeNodeFromIndex(node_id);

    // Pop from vector
    nodes.pop_back();

    return true;
}


Node *FactoryGraph::getNode(uint64_t id) {
    auto it = node_id_to_index_.find(id);
    return (it != node_id_to_index_.end()) ? &nodes[it->second] : nullptr;
}

const Node *FactoryGraph::getNode(uint64_t id) const {
    auto it = node_id_to_index_.find(id);
    return (it != node_id_to_index_.end()) ? &nodes[it->second] : nullptr;
}

uint64_t FactoryGraph::addPort(const std::string& resource_key, uint64_t node_id) {
    uint64_t port_id = next_port_id++;
    size_t index = ports.size();
    ports.emplace_back(port_id, node_id, resource_key);
    addPortToIndex(port_id, index);
    return port_id;
}

bool FactoryGraph::removePort(uint64_t port_id) {
    auto map_it = port_id_to_index_.find(port_id);
    if (map_it == port_id_to_index_.end()) {
        return false;
    }

    size_t index = map_it->second;

    uint64_t node_id = ports[index].node_id;

    // Remove all connections involving this port
    // We'll use removeConnection() so it keeps indices consistent
    auto range = connectionsByPort.equal_range(port_id);
    std::vector<uint64_t> connections_to_remove;
    for (auto it = range.first; it != range.second; ++it) {
        connections_to_remove.push_back(it->second);
    }
    for (uint64_t conn_id : connections_to_remove) {
        const auto& conn = connections[connection_id_to_index_[conn_id]];
        removeConnection(conn.from_port, conn.to_port);
    }

    Node* parent_node = getNode(node_id);

    if (parent_node) {
        auto& in_ports = parent_node->input_ports;
        auto in_it = std::find(in_ports.begin(), in_ports.end(), port_id);

        if (in_it != in_ports.end()) {
            in_ports.erase(in_it);
        } else {
            auto& out_ports = parent_node->output_ports;
            auto out_it = std::find(out_ports.begin(), out_ports.end(), port_id);
            if (out_it != out_ports.end()) {
                out_ports.erase(out_it);
            }
        }
    }

    // If it's not the last port, swap with the last
    size_t last_index = ports.size() - 1;
    if (index != last_index) {
        std::swap(ports[index], ports[last_index]);

        // Update index of swapped port
        uint64_t swapped_id = ports[index].id;
        port_id_to_index_[swapped_id] = index;
    }

    // Remove from index map
    removePortFromIndex(port_id);

    // Remove from vector
    ports.pop_back();

    return true;
}


Port *FactoryGraph::getPort(uint64_t id) {
    auto it = port_id_to_index_.find(id);
    return (it != port_id_to_index_.end()) ? &ports[it->second] : nullptr;
}

const Port *FactoryGraph::getPort(uint64_t id) const {
    auto it = port_id_to_index_.find(id);
    return (it != port_id_to_index_.end()) ? &ports[it->second] : nullptr;
}

uint64_t FactoryGraph::addConnection(uint64_t from_port, uint64_t to_port) {
    if (!isValidConnection(from_port, to_port)) {
        return -1;
    }
    uint64_t id = next_connection_id++;
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

bool FactoryGraph::removeConnection(uint64_t from_port, uint64_t to_port) {
    for (size_t index = 0; index < connections.size(); ++index) {
        if (connections[index].from_port == from_port &&
            connections[index].to_port == to_port) {

            uint64_t connection_id = connections[index].id;

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
                uint64_t swapped_id = connections[index].id;
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

Connection *FactoryGraph::getConnection(uint64_t id) {
    auto it = connection_id_to_index_.find(id);
    return (it != connection_id_to_index_.end()) ? &connections[it->second] : nullptr;
}

const Connection *FactoryGraph::getConnection(uint64_t id) const {
    auto it = connection_id_to_index_.find(id);
    return (it != connection_id_to_index_.end()) ? &connections[it->second] : nullptr;
}

Connection *FactoryGraph::getConnection(uint64_t from_port, uint64_t to_port) {
    auto range = connectionsByPort.equal_range(from_port);
    for (auto it = range.first; it != range.second; ++it) {
        uint64_t conn_id = it->second;
        auto idx_it = connection_id_to_index_.find(conn_id);
        if (idx_it == connection_id_to_index_.end()) continue;
        const Connection &c = connections[idx_it->second];
        if (c.to_port == to_port) {
            return getConnection(conn_id);
        }
    }
    return nullptr;
}

std::vector<Connection *> FactoryGraph::getConnectionsForPort(uint64_t id) {
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
        node_json["selected_recipe_key"] = node.selected_recipe_key;
        node_json["input_ports"] = node.input_ports;
        node_json["output_ports"] = node.output_ports;

        // Save constraints in a simple map of { "port_id": constraint_value }
        nlohmann::json port_constraints = nlohmann::json::object();
        for (uint64_t port_id : node.input_ports) {
            const Port* port = getPort(port_id);
            if (port && port->user_constraint != -1.0) { // Only save non-default values
                port_constraints[std::to_string(port_id)] = port->user_constraint;
            }
        }
        for (uint64_t port_id : node.output_ports) {
            const Port* port = getPort(port_id);
            if (port && port->user_constraint != -1.0) {
                port_constraints[std::to_string(port_id)] = port->user_constraint;
            }
        }
        node_json["port_constraints"] = port_constraints;
        j["nodes"].push_back(node_json);
    }

    // Serialize connections
    j["connections"] = nlohmann::json::array();
    for (const auto& connection : connections) {
        nlohmann::json conn_json;
        conn_json["id"] = connection.id;
        conn_json["from_port"] = connection.from_port;
        conn_json["to_port"] = connection.to_port;
        j["connections"].push_back(conn_json);
    }

    // Serialize ID counters
    j["next_node_id"] = next_node_id;
    j["next_connection_id"] = next_connection_id;
    j["game_data_filename"] = game_data.gameDataFilePath.filename();
    j["game_name"] = game_data.gameName;

    return j;
}

void FactoryGraph::deserialize(const nlohmann::json& j, const GameData &game_data) {
    // Clear existing data
    clear();

    this->game_data = game_data;

    if (j.contains("next_node_id") && j["next_node_id"].is_number()) {
        next_node_id = j.value("next_node_id", 0);
    }
    if (j.contains("next_connection_id") && j["next_connection_id"].is_number()) {
        next_connection_id = j.value("next_connection_id", 0);
    }

    next_port_id = 0;

    std::unordered_map<uint64_t, uint64_t> oldToNewPortIdMap;

    // Deserialize nodes
    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& node_json : j["nodes"]) {
            uint64_t nodeId = node_json.value("id", -1);
            std::string recipeKey = node_json.value("selected_recipe_key", "");

            if (nodeId == -1 || recipeKey.empty()) {
                std::cerr << "Invalid node id" << std::endl;
                continue;
            }

            auto recipeIt = game_data.recipes.find(recipeKey);
            if (recipeIt == game_data.recipes.end()) {
                std::cerr << "Invalid recipe key" << std::endl;
                continue;
            }

            const Recipe& recipe = recipeIt->second;

            Node node;
            node.id = nodeId;
            node.selected_recipe_key = recipeKey;
            node.name = recipe.name;

            if (!recipe.produced_in_machines_keys.empty()) {
                node.machine_key = recipe.produced_in_machines_keys[0]; // todo: make proper machine save/load
            } else {
                std::cerr << "Warning: Recipe '" << recipeKey << "' has no associated machines." << std::endl;
            }

            const auto& portConstraints = node_json.value("port_constraints", nlohmann::json::object());
            const auto& savedInputPorts = node_json.value("input_ports", nlohmann::json::array());
            for (size_t i = 0; i < recipe.input_ports.size(); ++i) {
                const auto& recipePort = recipe.input_ports[i];
                Port p;
                p.id = next_port_id++;
                p.resource_key = recipePort.resource_key;
                p.node_id = nodeId;

                if (savedInputPorts.is_array() && i < savedInputPorts.size() && savedInputPorts[i].is_number()) {
                    uint64_t oldPortId = savedInputPorts[i].get<uint64_t>();
                    oldToNewPortIdMap[oldPortId] = p.id;

                    if (portConstraints.contains(std::to_string(oldPortId))) {
                        p.user_constraint = portConstraints[std::to_string(oldPortId)].get<double>();
                    }
                }

                node.input_ports.push_back(p.id);
                ports.push_back(p);
                addPortToIndex(p.id, ports.size() - 1);
            }

            const auto& savedOutputPorts = node_json.value("output_ports", nlohmann::json::array());
            for (size_t i = 0; i < recipe.output_ports.size(); ++i) {
                const auto& recipePort = recipe.output_ports[i];
                Port p;
                p.id = next_port_id++;
                p.resource_key = recipePort.resource_key;
                p.node_id = nodeId;

                if (savedOutputPorts.is_array() && i < savedOutputPorts.size() && savedOutputPorts[i].is_number()) {                    uint64_t oldPortId = savedOutputPorts[i].get<uint64_t>();
                    oldToNewPortIdMap[oldPortId] = p.id;

                    if (portConstraints.contains(std::to_string(oldPortId))) {
                        p.user_constraint = portConstraints[std::to_string(oldPortId)].get<double>();
                    }
                }

                node.output_ports.push_back(p.id);
                ports.push_back(p);
                addPortToIndex(p.id, ports.size() - 1);
            }

            nodes.push_back(node);
            addNodeToIndex(node.id, nodes.size() - 1);
        }
    }

    // Deserialize connections
    if (j.contains("connections") && j["connections"].is_array()) {
        for (const auto& conn_json : j["connections"]) {
            uint64_t oldFromPort = conn_json.value("from_port", -1);
            uint64_t oldToPort = conn_json.value("to_port", -1);

            if (oldFromPort == -1 || oldToPort == -1) {
                std::cerr << "Invalid connection loaded" << std::endl;
                continue;
            }

            if (oldToNewPortIdMap.count(oldFromPort) && oldToNewPortIdMap.count(oldToPort)) {
                uint64_t newFromPort = oldToNewPortIdMap[oldFromPort];
                uint64_t newToPort = oldToNewPortIdMap[oldToPort];

                Connection conn;
                conn.id = conn_json.value("id", -1);
                if (conn.id == -1) {
                    std::cerr << "Invalid connection id" << std::endl;
                    continue;
                }
                conn.from_port = newFromPort;
                conn.to_port = newToPort;
                connections.push_back(conn);
                addConnectionToIndex(conn.id, connections.size() - 1);
                connectionsByPort.insert({conn.from_port, conn.id});
                connectionsByPort.insert({conn.to_port, conn.id});
            }
        }
    }
}

bool FactoryGraph::setNodeRecipe(uint64_t node_id, const std::string& recipe_key) {
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
        uint64_t port_id = addPort(recipe.input_ports.at(i).resource_key, node_id);
        node->input_ports[i] = port_id;
    }
    for (int i = 0; i < recipe.output_ports.size(); ++i) {
        uint64_t port_id = addPort(recipe.output_ports.at(i).resource_key, node_id);
        node->output_ports[i] = port_id;
    }

    // if (recipe.input_ports.size() == 1 && recipe.input_ports.at(0).resource_key == "nothing") {
    //     //node->type = NodeType::PRODUCER;
    // } else if (recipe.output_ports.size() == 1 && recipe.output_ports.at(0).resource_key == "nothing") {
    //     //node->type = NodeType::CONSUMER;
    // } else {
    //     //node->type = NodeType::PROCESSOR;
    // }
    return true;
}

const std::vector<Node> &FactoryGraph::getNodes() const {
    return nodes;
}

bool FactoryGraph::isValidConnection(uint64_t from_port, uint64_t to_port) {
    Port *from_port_ptr = getPort(from_port);
    Port *to_port_ptr = getPort(to_port);
    if (!from_port_ptr || !to_port_ptr) {
        return false;
    }
    if (from_port_ptr->resource_key != to_port_ptr->resource_key) {
        auto resource_names = game_data.resources;
        return false;
    }
    if (isInputPort(from_port_ptr->id) || !isInputPort(to_port_ptr->id)) {
        return false;
    }
    return true;
}

bool FactoryGraph::connectionExists(uint64_t from_port, uint64_t to_port) {
    auto range = connectionsByPort.equal_range(from_port);
    for (auto it = range.first; it != range.second; ++it) {
        uint64_t conn_id = it->second;
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

bool FactoryGraph::setPortConstraint(uint64_t port_id, double demand) {
    Port *port = getPort(port_id);
    if (!port) {
        return false;
    }
    port->user_constraint = demand;
    return true;
}

bool FactoryGraph::isInputPort(uint64_t port_id) const {
    const Port *port = getPort(port_id);
    const Node *node = port ? getNode(port->node_id) : nullptr;
    return port && node && std::find(node->input_ports.begin(), node->input_ports.end(), port_id) != node->input_ports.end();
}
