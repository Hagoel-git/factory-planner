#include "FactoryGraph.h"
#include "core/data/GameDataManager.h"

#include <iostream>
#include <unordered_set>
#include <utility>
#include <cstdint>
#include <absl/log/log.h>

uint64_t FactoryGraph::addNode(const std::string &name, std::string recipe_key) {
    uint64_t id = next_node_id++;
    size_t index = nodes.size();
    nodes.emplace_back(name, id);
    addNodeToIndex(id, index);
    setNodeRecipe(id, std::move(recipe_key));
    VLOG(3) << "Added node: " << name << " (ID: " << id << ")";
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
    VLOG(3) << "Restored node: " << node.name << " (ID: " << node.id << ")";
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
    VLOG(3) << "Removed node ID: " << node_id;
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
    VLOG(3) << "Added port ID: " << port_id << " to node ID: " << node_id << " with resource: " << resource_key;
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

    VLOG(3) << "Removed port ID: " << port_id;
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
    VLOG(3) << "Added connection ID: " << id << " from port ID: " << from_port << " to port ID: " << to_port;
    return id;
}

void FactoryGraph::restoreConnection(const Connection &connection) {
    connections.push_back(connection);
    size_t index = connections.size() - 1;
    addConnectionToIndex(connection.id, index);
    connectionsByPort.insert({connection.from_port, connection.id});
    connectionsByPort.insert({connection.to_port, connection.id});
    VLOG(3) << "Restored connection ID: " << connection.id << " from port ID: " << connection.from_port << " to port ID: " << connection.to_port;
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
            VLOG(3) << "Removed connection ID: " << connection_id << " from port ID: " << from_port << " to port ID: " << to_port;
            return true;
            }
    }
    VLOG(3) << "No connection found from port ID: " << from_port << " to port ID: " << to_port << " to remove.";
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
    VLOG(3) << "Cleared FactoryGraph.";
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
        node_json["machine"] = node.machine_key;
        if (node.clock_speed != 100.0) {
            node_json["clock_speed"] = node.clock_speed;
        }
        if (node.production_multiplier != 100.0) {
            node_json["production_multiplier"] = node.production_multiplier;
        }

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

    j["preferred_machines"] = preferred_machines;

    // Serialize ID counters
    j["next_node_id"] = next_node_id;
    j["next_connection_id"] = next_connection_id;
    j["game_data_uuid"] = game_data.uuid;
    VLOG(3) << "Serialized FactoryGraph.";
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
    if (j.contains("preferred_machines") && j["preferred_machines"].is_array()) {
        preferred_machines = j["preferred_machines"].get<std::vector<std::string>>();
    }

    next_port_id = 0;

    const std::string PREFIX_RES = "res@";
    const std::string PREFIX_MAC = "mac@";
    const std::string PREFIX_REC = "rec@";

    std::unordered_map<uint64_t, uint64_t> oldToNewPortIdMap;

    // Deserialize nodes
    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& node_json : j["nodes"]) {
            uint64_t nodeId = node_json.value("id", -1);
            std::string recipeKey = node_json.value("selected_recipe_key", "");

            if (game_data.recipes.find(recipeKey) == game_data.recipes.end()) {
                auto it = game_data.id_aliases.find(PREFIX_REC + recipeKey);
                if (it != game_data.id_aliases.end()) {
                    recipeKey = it->second;
                } else {
                    LOG(ERROR) << "Unknown recipe key: " << recipeKey;
                    continue;
                }
            }

            if (nodeId == -1 || recipeKey.empty()) {
                LOG(ERROR) << "Invalid node data in saved graph.";
                continue;
            }

            auto recipeIt = game_data.recipes.find(recipeKey);
            if (recipeIt == game_data.recipes.end()) {
                LOG(ERROR) << "Recipe key '" << recipeKey << "' not found in game data.";
                continue;
            }

            const Recipe& recipe = recipeIt->second;

            double clockSpeed = node_json.value("clock_speed", 100.0);
            double productionMultiplier = node_json.value("production_multiplier", 100.0);

            Node node;
            node.id = nodeId;
            node.selected_recipe_key = recipeKey;
            node.name = recipe.name;
            node.clock_speed = clockSpeed;
            node.production_multiplier = productionMultiplier;

            std::string machKey = node_json.value("machine", "");

            if (!machKey.empty() && game_data.machines.find(machKey) == game_data.machines.end()) {
                auto it = game_data.id_aliases.find(PREFIX_MAC + machKey);
                if (it != game_data.id_aliases.end()) {
                    machKey = it->second;
                }
            }

            if (!recipe.produced_in_machines_keys.empty()) {
                if (machKey.empty()) {
                    node.machine_key = resolvePreferredMachine(recipe.produced_in_machines_keys);
                } else {
                    node.machine_key = machKey;
                }
            } else {
                LOG(WARNING) << "Recipe '" << recipeKey << "' has no associated machines.";
                node.machine_key = "";
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

                if (savedOutputPorts.is_array() && i < savedOutputPorts.size() && savedOutputPorts[i].is_number()) {
                    uint64_t oldPortId = savedOutputPorts[i].get<uint64_t>();
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
                LOG(ERROR) << "Invalid connection data in saved graph.";
                continue;
            }

            if (oldToNewPortIdMap.count(oldFromPort) && oldToNewPortIdMap.count(oldToPort)) {
                uint64_t newFromPort = oldToNewPortIdMap[oldFromPort];
                uint64_t newToPort = oldToNewPortIdMap[oldToPort];

                Connection conn;
                conn.id = conn_json.value("id", -1);
                if (conn.id == -1) {
                    LOG(ERROR) << "Invalid connection id in saved graph.";
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
    VLOG(3) << "Deserialized FactoryGraph.";
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

    if (!recipe.produced_in_machines_keys.empty()) {
        node->machine_key = resolvePreferredMachine(recipe.produced_in_machines_keys);
    } else {
        node->machine_key = "";
    }

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

    VLOG(3) << "Set recipe '" << recipe_key << "' for node ID: " << node_id;

    // if (recipe.input_ports.size() == 1 && recipe.input_ports.at(0).resource_key == "nothing") {
    //     //node->type = NodeType::PRODUCER;
    // } else if (recipe.output_ports.size() == 1 && recipe.output_ports.at(0).resource_key == "nothing") {
    //     //node->type = NodeType::CONSUMER;
    // } else {
    //     //node->type = NodeType::PROCESSOR;
    // }
    return true;
}

bool FactoryGraph::setNodeClockSpeed(uint64_t node_id, double speed) {
    Node *node = getNode(node_id);
    if (!node) {
        return false;
    }
    node->clock_speed = speed;
    VLOG(3) << "Set clock speed to " << speed << "% for node ID: " << node_id;
    return true;
}

bool FactoryGraph::setProductionMultiplier(uint64_t node_id, double multiplier) {
    Node *node = getNode(node_id);
    if (!node) {
        return false;
    }
    node->production_multiplier = multiplier;
    VLOG(3) << "Set production multiplier to " << multiplier << "% for node ID: " << node_id;
    return true;
}

bool FactoryGraph::changeMachine(uint64_t node_id, const std::string &machine_key) {
    Node *node = getNode(node_id);
    if (!node) {
        return false;
    }

    Recipe recipe = game_data.recipes.at(node->selected_recipe_key);
    auto it = std::find(recipe.produced_in_machines_keys.begin(), recipe.produced_in_machines_keys.end(), machine_key);
    if (it == recipe.produced_in_machines_keys.end()) {
        return false;
    }

    node->machine_key = machine_key;
    VLOG(3) << "Changed machine to '" << machine_key << "' for node ID: " << node_id;
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

void FactoryGraph::setMachinePreferred(const std::string &machineKey, bool preferred) {
    auto it = std::remove(preferred_machines.begin(), preferred_machines.end(), machineKey);
    preferred_machines.erase(it, preferred_machines.end());

    if (preferred) {
        preferred_machines.insert(preferred_machines.begin(), machineKey);
        VLOG(3) << "Machine set as preferred (High Priority): " << machineKey;
    }
}

bool FactoryGraph::isMachinePreferred(const std::string &machineKey) const {
    return std::find(preferred_machines.begin(), preferred_machines.end(), machineKey) != preferred_machines.end();
}

std::string FactoryGraph::resolvePreferredMachine(const std::vector<std::string> &allowedMachines) const {
    if (allowedMachines.empty()) return "";

    for (const auto& prefKey : preferred_machines) {
        for (const auto& allowed : allowedMachines) {
            if (prefKey == allowed) {
                return prefKey;
            }
        }
    }

    return allowedMachines[0];
}
