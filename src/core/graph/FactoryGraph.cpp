#include "FactoryGraph.h"
#include "core/data/GameDataManager.h"

#include <iostream>
#include <unordered_set>
#include <utility>
#include <cstdint>
#include <absl/log/log.h>

uint64_t FactoryGraph::addNode(const std::string &name, std::string recipe_key) {
    uint64_t id = m_nextNodeId++;
    size_t index = m_nodes.size();
    m_nodes.emplace_back(name, id);
    addNodeToIndex(id, index);
    setNodeRecipe(id, std::move(recipe_key));
    VLOG(3) << "Added node: " << name << " (ID: " << id << ")";
    return id;
}

void FactoryGraph::restoreNode(const Node &node, const std::vector<Port> &ports) {
    m_nodes.push_back(node);
    size_t index = m_nodes.size() - 1;
    addNodeToIndex(node.id, index);
    for (const auto& port : ports) {
        this->m_ports.push_back(port);
        addPortToIndex(port.id, this->m_ports.size() - 1);
    }
    VLOG(3) << "Restored node: " << node.name << " (ID: " << node.id << ")";
}

bool FactoryGraph::removeNode(uint64_t node_id) {
    auto map_it = m_nodeIdToIndex.find(node_id);
    if (map_it == m_nodeIdToIndex.end()) {
        return false;
    }

    size_t index = map_it->second;
    Node& node = m_nodes[index];

    // Remove all associated ports (these functions already handle their own swap-and-pop)
    for (uint64_t port_id : node.input_ports) {
        removePort(port_id);
    }
    for (uint64_t port_id : node.output_ports) {
        removePort(port_id);
    }

    // If it's not the last node, swap with the last
    size_t last_index = m_nodes.size() - 1;
    if (index != last_index) {
        std::swap(m_nodes[index], m_nodes[last_index]);

        // Update index of swapped node
        uint64_t swapped_id = m_nodes[index].id;
        m_nodeIdToIndex[swapped_id] = index;
    }

    // Remove from index map
    removeNodeFromIndex(node_id);

    // Pop from vector
    m_nodes.pop_back();
    VLOG(3) << "Removed node ID: " << node_id;
    return true;
}


Node *FactoryGraph::getNode(uint64_t id) {
    auto it = m_nodeIdToIndex.find(id);
    return (it != m_nodeIdToIndex.end()) ? &m_nodes[it->second] : nullptr;
}

const Node *FactoryGraph::getNode(uint64_t id) const {
    auto it = m_nodeIdToIndex.find(id);
    return (it != m_nodeIdToIndex.end()) ? &m_nodes[it->second] : nullptr;
}

uint64_t FactoryGraph::addPort(const std::string& resource_key, uint64_t node_id, ConstraintType constraint_type) {
    uint64_t port_id = m_nextPortId++;
    size_t index = m_ports.size();
    m_ports.emplace_back(port_id, node_id, resource_key, constraint_type);
    addPortToIndex(port_id, index);
    VLOG(3) << "Added port ID: " << port_id << " to node ID: " << node_id << " with resource: " << resource_key;
    return port_id;
}

bool FactoryGraph::removePort(uint64_t port_id) {
    auto map_it = m_portIdToIndex.find(port_id);
    if (map_it == m_portIdToIndex.end()) {
        return false;
    }

    size_t index = map_it->second;

    uint64_t node_id = m_ports[index].node_id;

    // Remove all connections involving this port
    // We'll use removeConnection() so it keeps indices consistent
    auto range = m_connectionsByPort.equal_range(port_id);
    std::vector<uint64_t> connections_to_remove;
    for (auto it = range.first; it != range.second; ++it) {
        connections_to_remove.push_back(it->second);
    }
    for (uint64_t conn_id : connections_to_remove) {
        const auto& conn = m_connections[m_connectionIdToIndex[conn_id]];
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
    size_t last_index = m_ports.size() - 1;
    if (index != last_index) {
        std::swap(m_ports[index], m_ports[last_index]);

        // Update index of swapped port
        uint64_t swapped_id = m_ports[index].id;
        m_portIdToIndex[swapped_id] = index;
    }

    // Remove from index map
    removePortFromIndex(port_id);

    // Remove from vector
    m_ports.pop_back();

    VLOG(3) << "Removed port ID: " << port_id;
    return true;
}


Port *FactoryGraph::getPort(uint64_t id) {
    auto it = m_portIdToIndex.find(id);
    return (it != m_portIdToIndex.end()) ? &m_ports[it->second] : nullptr;
}

const Port *FactoryGraph::getPort(uint64_t id) const {
    auto it = m_portIdToIndex.find(id);
    return (it != m_portIdToIndex.end()) ? &m_ports[it->second] : nullptr;
}

uint64_t FactoryGraph::addConnection(uint64_t from_port, uint64_t to_port) {
    if (!isValidConnection(from_port, to_port)) {
        return -1;
    }
    uint64_t id = m_nextConnectionId++;
    size_t index = m_connections.size();
    m_connections.emplace_back(id, from_port, to_port, getPort(from_port)->resource_key);
    addConnectionToIndex(id, index);
    m_connectionsByPort.insert({from_port, id});
    m_connectionsByPort.insert({to_port, id});
    VLOG(3) << "Added connection ID: " << id << " from port ID: " << from_port << " to port ID: " << to_port;
    return id;
}

void FactoryGraph::restoreConnection(const Connection &connection) {
    m_connections.push_back(connection);
    size_t index = m_connections.size() - 1;
    addConnectionToIndex(connection.id, index);
    m_connectionsByPort.insert({connection.from_port, connection.id});
    m_connectionsByPort.insert({connection.to_port, connection.id});
    VLOG(3) << "Restored connection ID: " << connection.id << " from port ID: " << connection.from_port << " to port ID: " << connection.to_port;
}

bool FactoryGraph::removeConnection(uint64_t from_port, uint64_t to_port) {
    for (size_t index = 0; index < m_connections.size(); ++index) {
        if (m_connections[index].from_port == from_port &&
            m_connections[index].to_port == to_port) {

            uint64_t connection_id = m_connections[index].id;

            // Remove from m_connectionsByPort before erasing
            auto range_from = m_connectionsByPort.equal_range(from_port);
            for (auto multi_it = range_from.first; multi_it != range_from.second; ++multi_it) {
                if (multi_it->second == connection_id) {
                    m_connectionsByPort.erase(multi_it);
                    break;
                }
            }
            auto range_to = m_connectionsByPort.equal_range(to_port);
            for (auto multi_it = range_to.first; multi_it != range_to.second; ++multi_it) {
                if (multi_it->second == connection_id) {
                    m_connectionsByPort.erase(multi_it);
                    break;
                }
            }

            // If it's not the last element, swap with the last
            size_t last_index = m_connections.size() - 1;
            if (index != last_index) {
                std::swap(m_connections[index], m_connections[last_index]);

                // Update index map for the swapped element
                uint64_t swapped_id = m_connections[index].id;
                m_connectionIdToIndex[swapped_id] = index;
            }

            // Remove from index map
            removeConnectionFromIndex(connection_id);

            // Actually remove the element
            m_connections.pop_back();
            VLOG(3) << "Removed connection ID: " << connection_id << " from port ID: " << from_port << " to port ID: " << to_port;
            return true;
            }
    }
    VLOG(3) << "No connection found from port ID: " << from_port << " to port ID: " << to_port << " to remove.";
    return false;
}

Connection *FactoryGraph::getConnection(uint64_t id) {
    auto it = m_connectionIdToIndex.find(id);
    return (it != m_connectionIdToIndex.end()) ? &m_connections[it->second] : nullptr;
}

const Connection *FactoryGraph::getConnection(uint64_t id) const {
    auto it = m_connectionIdToIndex.find(id);
    return (it != m_connectionIdToIndex.end()) ? &m_connections[it->second] : nullptr;
}

Connection *FactoryGraph::getConnection(uint64_t from_port, uint64_t to_port) {
    auto range = m_connectionsByPort.equal_range(from_port);
    for (auto it = range.first; it != range.second; ++it) {
        uint64_t conn_id = it->second;
        auto idx_it = m_connectionIdToIndex.find(conn_id);
        if (idx_it == m_connectionIdToIndex.end()) continue;
        const Connection &c = m_connections[idx_it->second];
        if (c.to_port == to_port) {
            return getConnection(conn_id);
        }
    }
    return nullptr;
}

std::vector<Connection *> FactoryGraph::getConnectionsForPort(uint64_t id) {
    std::vector<Connection*> result;
    auto range = m_connectionsByPort.equal_range(id);
    for (auto it = range.first; it != range.second; ++it) {
        result.push_back(getConnection(it->second));
    }
    return result;
}


void FactoryGraph::clear() {
    m_nodes.clear();
    m_ports.clear();
    m_connections.clear();
    m_nodeIdToIndex.clear();
    m_portIdToIndex.clear();
    m_connectionIdToIndex.clear();
    m_nextPortId = 0;
    m_nextNodeId = 0;
    m_nextConnectionId = 0;
    VLOG(3) << "Cleared FactoryGraph.";
}


nlohmann::json FactoryGraph::serialize() const {
    nlohmann::json j;

    // Serialize nodes
    j["nodes"] = nlohmann::json::array();
    for (const auto& node : m_nodes) {
        nlohmann::json node_json;
        node_json["id"] = node.id;
        node_json["selected_recipe_key"] = node.selected_recipe_key;
        node_json["input_ports"] = node.input_ports;
        node_json["output_ports"] = node.output_ports;
        node_json["machine"] = node.machine_key;

        if (node.clock_speed != 100.0) node_json["clock_speed"] = node.clock_speed;
        if (node.production_multiplier != 100.0) node_json["production_multiplier"] = node.production_multiplier;

        node_json["port_constraints"] = serializePortConstraints(node);

        j["nodes"].push_back(node_json);
    }

    // Serialize connections
    j["connections"] = nlohmann::json::array();
    for (const auto& connection : m_connections) {
        nlohmann::json conn_json;
        conn_json["id"] = connection.id;
        conn_json["from_port"] = connection.from_port;
        conn_json["to_port"] = connection.to_port;
        j["connections"].push_back(conn_json);
    }

    j["preferred_machines"] = m_preferredMachines;

    // Serialize ID counters
    j["next_node_id"] = m_nextNodeId;
    j["next_port_id"] = m_nextPortId;
    j["next_connection_id"] = m_nextConnectionId;
    j["game_data_uuid"] = m_gameData.uuid;
    VLOG(3) << "Serialized FactoryGraph.";
    return j;
}

nlohmann::json FactoryGraph::serializePortConstraints(const Node& node) const {
    nlohmann::json constraints = nlohmann::json::object();

    auto save_constraint = [&](uint64_t port_id) {
        const Port* port = getPort(port_id);
        if (port && port->user_constraint != -1.0) {
            constraints[std::to_string(port_id)] = port->user_constraint;
        }
    };

    for (uint64_t pid : node.input_ports) save_constraint(pid);
    for (uint64_t pid : node.output_ports) save_constraint(pid);

    return constraints;
}

void FactoryGraph::deserialize(const nlohmann::json& j, const GameData &game_data) {
    clear();
    this->m_gameData = game_data;

    // 1. Restore Global Counters
    if (j.contains("next_node_id") && j["next_node_id"].is_number()) m_nextNodeId = j.value("next_node_id", 0);
    if (j.contains("next_connection_id") && j["next_connection_id"].is_number()) m_nextConnectionId = j.value("next_connection_id", 0);
    if (j.contains("next_port_id") && j["next_port_id"].is_number()) m_nextPortId = j.value("next_port_id", 0);

    if (j.contains("preferred_machines") && j["preferred_machines"].is_array()) {
        m_preferredMachines = j["preferred_machines"].get<std::vector<std::string>>();
    }

    // 2. Deserialize Nodes
    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& node_json : j["nodes"]) {
            deserializeNode(node_json, game_data);
        }
    }

    // 3. Deserialize Connections
    if (j.contains("connections") && j["connections"].is_array()) {
        for (const auto& conn_json : j["connections"]) {
            deserializeConnection(conn_json);
        }
    }

    VLOG(3) << "Deserialized FactoryGraph.";
}

void FactoryGraph::deserializeNode(const nlohmann::json& node_json, const GameData& game_data) {
    // A. Resolve Recipe Key
    std::string recipeKey = resolveLegacyKey(node_json.value("selected_recipe_key", ""), "rec@", game_data.recipes, game_data.id_aliases);
    if (recipeKey.empty()) return; // Error logged in helper

    const Recipe& recipe = game_data.recipes.at(recipeKey);
    uint64_t nodeId = node_json.value("id", (uint64_t)-1);

    if (nodeId == (uint64_t)-1) {
        LOG(ERROR) << "Invalid node data in saved graph: missing ID.";
        return;
    }

    // B. Build Base Node
    Node node;
    node.id = nodeId;
    node.selected_recipe_key = recipeKey;
    node.name = recipe.name;
    node.clock_speed = node_json.value("clock_speed", 100.0);
    node.production_multiplier = node_json.value("production_multiplier", 100.0);

    // Update global counter to ensure next generated ID is safe
    if (node.id >= m_nextNodeId) m_nextNodeId = node.id + 1;

    // C. Resolve Machine
    std::string machKey = node_json.value("machine", "");
    // Attempt to resolve legacy machine key if necessary
    if (!machKey.empty() && game_data.machines.find(machKey) == game_data.machines.end()) {
        machKey = resolveLegacyKey(machKey, "mac@", game_data.machines, game_data.id_aliases);
    }

    // Assign Machine Strategy
    if (!recipe.produced_in_machines_keys.empty()) {
        node.machine_key = machKey.empty() ? resolvePreferredMachine(recipe.produced_in_machines_keys) : machKey;
    } else {
        LOG(WARNING) << "Recipe '" << recipeKey << "' has no associated machines.";
        node.machine_key = "";
    }

    // D. Process Ports
    const auto& portConstraints = node_json.value("port_constraints", nlohmann::json::object());

    ConstraintType inputType = getPortConstraintType(recipe, true);
    ConstraintType outputType = getPortConstraintType(recipe, false);

    processPorts(recipe.input_ports, node_json.value("input_ports", nlohmann::json::array()),
                 node.input_ports, portConstraints, nodeId, inputType);

    processPorts(recipe.output_ports, node_json.value("output_ports", nlohmann::json::array()),
                 node.output_ports, portConstraints, nodeId, outputType);

    m_nodes.push_back(node);
    addNodeToIndex(node.id, m_nodes.size() - 1);
}

void FactoryGraph::processPorts(const std::vector<RecipePort>& recipePorts,
                                const nlohmann::json& savedPortsJson,
                                std::vector<uint64_t>& nodePortList,
                                const nlohmann::json& constraints,
                                uint64_t nodeId,
                                ConstraintType constraintType) {
    for (size_t i = 0; i < recipePorts.size(); ++i) {
        Port p;
        p.resource_key = recipePorts[i].resource_key;
        p.node_id = nodeId;
        p.constraint_type = constraintType;

        if (savedPortsJson.is_array() && i < savedPortsJson.size() && savedPortsJson[i].is_number()) {
            p.id = savedPortsJson[i].get<uint64_t>();
            std::string originalIdStr = std::to_string(p.id);

            if (m_portIdToIndex.find(p.id) != m_portIdToIndex.end()) {
                LOG(WARNING) << "Duplicate Port ID " << p.id << " detected on node " << nodeId
                             << ". Remapping to new ID.";
                p.id = m_nextPortId++;
            }

            // Check for constraints on this existing port
            if (constraints.contains(originalIdStr) && constraints[originalIdStr].is_number()) {
                p.user_constraint = constraints[originalIdStr].get<double>();
            }
        } else {
            // New port (recipe changed?) -> Generate new ID
            p.id = m_nextPortId++;
        }

        // Ensure global counter is always ahead of any loaded (or remapped) ID
        if (p.id >= m_nextPortId) m_nextPortId = p.id + 1;

        nodePortList.push_back(p.id);
        m_ports.push_back(p);
        addPortToIndex(p.id, m_ports.size() - 1);
    }
}

void FactoryGraph::deserializeConnection(const nlohmann::json& conn_json) {
    Connection conn;
    conn.id = conn_json.value("id", (uint64_t)-1);
    conn.from_port = conn_json.value("from_port", (uint64_t)-1);
    conn.to_port = conn_json.value("to_port", (uint64_t)-1);

    // Basic structure check
    if (conn.id == (uint64_t)-1 || conn.from_port == (uint64_t)-1 || conn.to_port == (uint64_t)-1) {
        LOG(ERROR) << "Invalid connection data in saved graph: missing IDs.";
        return;
    }

    Port* fromPort = getPort(conn.from_port);
    Port* toPort = getPort(conn.to_port);

    if (!fromPort || !toPort) {
        LOG(WARNING) << "Skipping connection " << conn.id
                     << ": Ports " << conn.from_port << "->" << conn.to_port
                     << " do not exist.";
        return;
    }

    if (!isValidConnection(conn.from_port, conn.to_port)) {
        LOG(WARNING) << "Skipping invalid connection " << conn.id
                     << " (" << fromPort->resource_key << " -> " << toPort->resource_key << ")"
                     << ": Connection rules violated.";
        return;
    }

    if (conn.id >= m_nextConnectionId) m_nextConnectionId = conn.id + 1;

    m_connections.push_back(conn);
    addConnectionToIndex(conn.id, m_connections.size() - 1);
    m_connectionsByPort.insert({conn.from_port, conn.id});
    m_connectionsByPort.insert({conn.to_port, conn.id});
}

ConstraintType FactoryGraph::getPortConstraintType(const Recipe &recipe, bool is_input) {
    bool is_producer = recipe.input_ports.size() == 1 && recipe.input_ports.at(0).resource_key == "nothing";
    bool is_consumer = recipe.output_ports.size() == 1 && recipe.output_ports.at(0).resource_key == "nothing";

    if (is_input && is_consumer) {
        return ConstraintType::TARGET;
    }
    if (is_input || is_producer) {
        return ConstraintType::LIMIT;
    }
    return ConstraintType::TARGET;
}

bool FactoryGraph::setNodeRecipe(uint64_t node_id, const std::string& recipe_key) {
    Node *node = getNode(node_id);
    if (!node) {
        return false;
    }
    if (m_gameData.recipes.count(recipe_key) == 0) {
        return false;
    }
    const Recipe &recipe = m_gameData.recipes.at(recipe_key);
    node->selected_recipe_key = recipe_key;

    if (!recipe.produced_in_machines_keys.empty()) {
        node->machine_key = resolvePreferredMachine(recipe.produced_in_machines_keys);
    } else {
        node->machine_key = "";
    }

    node->output_ports.resize(recipe.output_ports.size());
    node->input_ports.resize(recipe.input_ports.size());
    for (int i = 0; i < recipe.input_ports.size(); ++i) {
        uint64_t port_id = addPort(recipe.input_ports.at(i).resource_key, node_id, getPortConstraintType(recipe, true));
        node->input_ports[i] = port_id;
    }
    for (int i = 0; i < recipe.output_ports.size(); ++i) {
        uint64_t port_id = addPort(recipe.output_ports.at(i).resource_key, node_id, getPortConstraintType(recipe, false));
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

    Recipe recipe = m_gameData.recipes.at(node->selected_recipe_key);
    auto it = std::find(recipe.produced_in_machines_keys.begin(), recipe.produced_in_machines_keys.end(), machine_key);
    if (it == recipe.produced_in_machines_keys.end()) {
        return false;
    }

    node->machine_key = machine_key;
    VLOG(3) << "Changed machine to '" << machine_key << "' for node ID: " << node_id;
    return true;
}

const std::vector<Node> &FactoryGraph::getNodes() const {
    return m_nodes;
}

bool FactoryGraph::isValidConnection(uint64_t from_port, uint64_t to_port) {
    Port *from_port_ptr = getPort(from_port);
    Port *to_port_ptr = getPort(to_port);
    if (!from_port_ptr || !to_port_ptr) {
        return false;
    }
    if (from_port_ptr->resource_key != to_port_ptr->resource_key) {
        auto resource_names = m_gameData.resources;
        return false;
    }
    if (isInputPort(from_port_ptr->id) || !isInputPort(to_port_ptr->id)) {
        return false;
    }
    return true;
}

bool FactoryGraph::connectionExists(uint64_t from_port, uint64_t to_port) {
    auto range = m_connectionsByPort.equal_range(from_port);
    for (auto it = range.first; it != range.second; ++it) {
        uint64_t conn_id = it->second;
        auto idx_it = m_connectionIdToIndex.find(conn_id);
        if (idx_it == m_connectionIdToIndex.end()) continue;
        const Connection &c = m_connections[idx_it->second];
        if (c.to_port == to_port) return true;
    }
    return false;
}

const std::vector<Connection> &FactoryGraph::getConnections() const {
    return m_connections;
}

const std::vector<Port> &FactoryGraph::getPorts() const {
    return m_ports;
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
    auto it = std::remove(m_preferredMachines.begin(), m_preferredMachines.end(), machineKey);
    m_preferredMachines.erase(it, m_preferredMachines.end());

    if (preferred) {
        m_preferredMachines.insert(m_preferredMachines.begin(), machineKey);
        VLOG(3) << "Machine set as preferred (High Priority): " << machineKey;
    }
}

bool FactoryGraph::isMachinePreferred(const std::string &machineKey) const {
    return std::find(m_preferredMachines.begin(), m_preferredMachines.end(), machineKey) != m_preferredMachines.end();
}

std::string FactoryGraph::resolvePreferredMachine(const std::vector<std::string> &allowedMachines) const {
    if (allowedMachines.empty()) return "";

    for (const auto& prefKey : m_preferredMachines) {
        for (const auto& allowed : allowedMachines) {
            if (prefKey == allowed) {
                return prefKey;
            }
        }
    }

    return allowedMachines[0];
}
