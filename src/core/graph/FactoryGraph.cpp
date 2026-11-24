#include "FactoryGraph.h"
#include "core/data/GameDataManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <absl/log/log.h>

uint64_t FactoryGraph::addNode(const std::string& name, std::string recipe_key) {
    uint64_t id = m_nextNodeId++;
    size_t index = m_nodes.size();
    m_nodes.emplace_back(name, id);
    addNodeToIndex(id, index);
    setNodeRecipe(id, std::move(recipe_key));
    VLOG(3) << "Added node: " << name << " (ID: " << id << ")";
    return id;
}

void FactoryGraph::restoreNode(const Node& node, const std::vector<Port>& ports) {
    if (node.id >= m_nextNodeId) {
        m_nextNodeId = node.id + 1;
    }
    m_nodes.push_back(node);
    size_t index = m_nodes.size() - 1;
    addNodeToIndex(node.id, index);
    for (const auto& port : ports) {
        if (port.id >= m_nextPortId) {
            m_nextPortId = port.id + 1;
        }
        this->m_ports.push_back(port);
        addPortToIndex(port.id, this->m_ports.size() - 1);
    }
    evaluateNode(node.id);
    VLOG(3) << "Restored node: " << node.name << " (ID: " << node.id << ")";
}

bool FactoryGraph::removeNode(uint64_t node_id) {
    auto map_it = m_nodeIdToIndex.find(node_id);
    if (map_it == m_nodeIdToIndex.end()) {
        return false;
    }

    size_t index = map_it->second;
    Node& node = m_nodes[index];

    std::vector<uint64_t> input_ports = node.input_ports;
    std::vector<uint64_t> output_ports = node.output_ports;

    for (uint64_t port_id : input_ports) {
        removePort(port_id);
    }
    for (uint64_t port_id : output_ports) {
        removePort(port_id);
    }

    size_t last_index = m_nodes.size() - 1;
    if (index != last_index) {
        std::swap(m_nodes[index], m_nodes[last_index]);
        uint64_t swapped_id = m_nodes[index].id;
        m_nodeIdToIndex[swapped_id] = index;
    }

    removeNodeFromIndex(node_id);
    m_nodeEvals.erase(node_id);
    m_nodes.pop_back();
    VLOG(3) << "Removed node ID: " << node_id;
    return true;
}

Node* FactoryGraph::getNode(uint64_t id) {
    auto it = m_nodeIdToIndex.find(id);
    return (it != m_nodeIdToIndex.end()) ? &m_nodes[it->second] : nullptr;
}

const Node* FactoryGraph::getNode(uint64_t id) const {
    auto it = m_nodeIdToIndex.find(id);
    return (it != m_nodeIdToIndex.end()) ? &m_nodes[it->second] : nullptr;
}

uint64_t FactoryGraph::addPort(const std::string& resource_key, uint64_t node_id,
                               const std::string& slot_key,
                               std::optional<uint64_t> preassigned_id) {
    uint64_t port_id = 0;
    if (preassigned_id.has_value() && m_portIdToIndex.find(*preassigned_id) == m_portIdToIndex.end()) {
        port_id = *preassigned_id;
        if (port_id >= m_nextPortId) {
            m_nextPortId = port_id + 1;
        }
    } else {
        port_id = m_nextPortId++;
    }
    size_t index = m_ports.size();
    m_ports.emplace_back(port_id, node_id, resource_key, slot_key);
    addPortToIndex(port_id, index);
    VLOG(3) << "Added port ID: " << port_id << " (slot: " << slot_key
            << ") to node ID: " << node_id << " with resource: " << resource_key;
    return port_id;
}

bool FactoryGraph::removePort(uint64_t port_id) {
    auto map_it = m_portIdToIndex.find(port_id);
    if (map_it == m_portIdToIndex.end()) {
        return false;
    }

    size_t index = map_it->second;
    uint64_t node_id = m_ports[index].node_id;

    auto range = m_connectionsByPort.equal_range(port_id);
    std::vector<uint64_t> connections_to_remove;
    for (auto it = range.first; it != range.second; ++it) {
        connections_to_remove.push_back(it->second);
    }
    for (uint64_t conn_id : connections_to_remove) {
        removeConnection(conn_id);
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

    size_t last_index = m_ports.size() - 1;
    if (index != last_index) {
        std::swap(m_ports[index], m_ports[last_index]);
        uint64_t swapped_id = m_ports[index].id;
        m_portIdToIndex[swapped_id] = index;
    }

    removePortFromIndex(port_id);
    m_ports.pop_back();

    VLOG(3) << "Removed port ID: " << port_id;
    return true;
}

Port* FactoryGraph::getPort(uint64_t id) {
    auto it = m_portIdToIndex.find(id);
    return (it != m_portIdToIndex.end()) ? &m_ports[it->second] : nullptr;
}

const Port* FactoryGraph::getPort(uint64_t id) const {
    auto it = m_portIdToIndex.find(id);
    return (it != m_portIdToIndex.end()) ? &m_ports[it->second] : nullptr;
}

Port* FactoryGraph::getPortBySlot(uint64_t node_id, const std::string& slot_key) {
    const Node* node = getNode(node_id);
    if (!node) return nullptr;
    for (uint64_t pid : node->input_ports) {
        Port* p = getPort(pid);
        if (p && p->slot_key == slot_key) return p;
    }
    for (uint64_t pid : node->output_ports) {
        Port* p = getPort(pid);
        if (p && p->slot_key == slot_key) return p;
    }
    return nullptr;
}

const Port* FactoryGraph::getPortBySlot(uint64_t node_id, const std::string& slot_key) const {
    const Node* node = getNode(node_id);
    if (!node) return nullptr;
    for (uint64_t pid : node->input_ports) {
        const Port* p = getPort(pid);
        if (p && p->slot_key == slot_key) return p;
    }
    for (uint64_t pid : node->output_ports) {
        const Port* p = getPort(pid);
        if (p && p->slot_key == slot_key) return p;
    }
    return nullptr;
}

uint64_t FactoryGraph::addConnection(uint64_t from_port, uint64_t to_port) {
    if (!isValidConnection(from_port, to_port)) {
        return -1;
    }
    uint64_t id = m_nextConnectionId++;
    size_t index = m_connections.size();
    m_connections.emplace_back(id, from_port, to_port, getPort(from_port)->resource_key, WireState::OK);
    addConnectionToIndex(id, index);
    m_connectionsByPort.insert({from_port, id});
    m_connectionsByPort.insert({to_port, id});
    VLOG(3) << "Added connection ID: " << id << " from port ID: " << from_port << " to port ID: " << to_port;
    return id;
}

void FactoryGraph::restoreConnection(const Connection& connection) {
    if (connection.id >= m_nextConnectionId) {
        m_nextConnectionId = connection.id + 1;
    }
    m_connections.push_back(connection);
    size_t index = m_connections.size() - 1;
    addConnectionToIndex(connection.id, index);
    m_connectionsByPort.insert({connection.from_port, connection.id});
    m_connectionsByPort.insert({connection.to_port, connection.id});
    updateWireStates();
    VLOG(3) << "Restored connection ID: " << connection.id
            << " from port ID: " << connection.from_port
            << " to port ID: " << connection.to_port;
}

bool FactoryGraph::removeConnection(uint64_t conn_id) {
    auto it = m_connectionIdToIndex.find(conn_id);
    if (it == m_connectionIdToIndex.end()) {
        VLOG(3) << "No connection found with ID: " << conn_id << " to remove.";
        return false;
    }

    size_t index = it->second;
    uint64_t from_port = m_connections[index].from_port;
    uint64_t to_port = m_connections[index].to_port;

    auto range_from = m_connectionsByPort.equal_range(from_port);
    for (auto multi_it = range_from.first; multi_it != range_from.second; ++multi_it) {
        if (multi_it->second == conn_id) {
            m_connectionsByPort.erase(multi_it);
            break;
        }
    }
    auto range_to = m_connectionsByPort.equal_range(to_port);
    for (auto multi_it = range_to.first; multi_it != range_to.second; ++multi_it) {
        if (multi_it->second == conn_id) {
            m_connectionsByPort.erase(multi_it);
            break;
        }
    }

    size_t last_index = m_connections.size() - 1;
    if (index != last_index) {
        std::swap(m_connections[index], m_connections[last_index]);
        uint64_t swapped_id = m_connections[index].id;
        m_connectionIdToIndex[swapped_id] = index;
    }

    removeConnectionFromIndex(conn_id);
    m_connections.pop_back();
    VLOG(3) << "Removed connection ID: " << conn_id << " from port ID: " << from_port << " to port ID: " << to_port;
    return true;
}

bool FactoryGraph::removeConnection(uint64_t from_port, uint64_t to_port) {
    auto range = m_connectionsByPort.equal_range(from_port);
    for (auto it = range.first; it != range.second; ++it) {
        uint64_t conn_id = it->second;
        auto idx_it = m_connectionIdToIndex.find(conn_id);
        if (idx_it == m_connectionIdToIndex.end()) continue;
        const Connection& c = m_connections[idx_it->second];
        if (c.from_port == from_port && c.to_port == to_port) {
            return removeConnection(conn_id);
        }
    }
    VLOG(3) << "No connection found from port ID: " << from_port << " to port ID: " << to_port << " to remove.";
    return false;
}

Connection* FactoryGraph::getConnection(uint64_t id) {
    auto it = m_connectionIdToIndex.find(id);
    return (it != m_connectionIdToIndex.end()) ? &m_connections[it->second] : nullptr;
}

const Connection* FactoryGraph::getConnection(uint64_t id) const {
    auto it = m_connectionIdToIndex.find(id);
    return (it != m_connectionIdToIndex.end()) ? &m_connections[it->second] : nullptr;
}

Connection* FactoryGraph::getConnection(uint64_t from_port, uint64_t to_port) {
    auto range = m_connectionsByPort.equal_range(from_port);
    for (auto it = range.first; it != range.second; ++it) {
        uint64_t conn_id = it->second;
        auto idx_it = m_connectionIdToIndex.find(conn_id);
        if (idx_it == m_connectionIdToIndex.end()) continue;
        const Connection& c = m_connections[idx_it->second];
        if (c.from_port == from_port && c.to_port == to_port) {
            return getConnection(conn_id);
        }
    }
    return nullptr;
}

const Connection* FactoryGraph::getConnection(uint64_t from_port, uint64_t to_port) const {
    auto range = m_connectionsByPort.equal_range(from_port);
    for (auto it = range.first; it != range.second; ++it) {
        uint64_t conn_id = it->second;
        auto idx_it = m_connectionIdToIndex.find(conn_id);
        if (idx_it == m_connectionIdToIndex.end()) continue;
        const Connection& c = m_connections[idx_it->second];
        if (c.from_port == from_port && c.to_port == to_port) {
            return getConnection(conn_id);
        }
    }
    return nullptr;
}

std::vector<Connection*> FactoryGraph::getConnectionsForPort(uint64_t id) {
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
    m_connectionsByPort.clear();
    m_preferredMachines.clear();
    m_globals.clear();
    m_unresolvedGlobals = nlohmann::json::object();
    m_invalidGlobalRaw = nlohmann::json::object();
    refreshResolvedGlobals();
    m_nodeEvals.clear();
    m_lastLoadError.clear();
    m_loadWarnings.clear();
    m_warnedUnresolvedParam = false;
    m_rateUnit = m_gameData.units.default_rate;
    m_nextPortId = 0;
    m_nextNodeId = 0;
    m_nextConnectionId = 0;
    VLOG(3) << "Cleared FactoryGraph.";
}

void FactoryGraph::evaluateNode(uint64_t node_id) {
    Node* node = getNode(node_id);
    if (!node) return;

    if (node->is_unresolved) {
        gamedata::NodeEval unresolvedEval;
        unresolvedEval.status = gamedata::EvalStatus::ERROR;
        unresolvedEval.messages.push_back(gamedata::EvalMessage{
            gamedata::EvalStatus::ERROR,
            "Unresolved recipe or machine on node.",
            "node"
        });
        for (uint64_t pid : node->input_ports) {
            if (Port* p = getPort(pid)) {
                p->active = false;
                p->amount_per_craft = 0.0;
            }
        }
        for (uint64_t pid : node->output_ports) {
            if (Port* p = getPort(pid)) {
                p->active = false;
                p->amount_per_craft = 0.0;
            }
        }
        m_nodeEvals[node_id] = std::move(unresolvedEval);
        updateWireStates();
        return;
    }

    refreshResolvedGlobals();
    gamedata::NodeEval eval = gamedata::evaluateNode(
        m_gameData, node->machine_key, node->selected_recipe_key,
        node->params, m_resolvedGlobals, node->name);

    if (!node->invalid_param_raw.empty()) {
        if (eval.status == gamedata::EvalStatus::OK) {
            eval.status = gamedata::EvalStatus::WARNING;
        }
        for (auto it = node->invalid_param_raw.begin(); it != node->invalid_param_raw.end(); ++it) {
            eval.messages.push_back(gamedata::EvalMessage{
                gamedata::EvalStatus::WARNING,
                "Node parameter '" + it.key() + "' failed validation; using schema default.",
                "parameters/" + it.key()
            });
        }
    }

    std::unordered_map<std::string, const gamedata::EvaluatedPort*> evalPortBySlot;
    for (const auto& ep : eval.ports) {
        evalPortBySlot[ep.slot_key] = &ep;
    }

    auto updatePortList = [&](const std::vector<uint64_t>& portIds) {
        for (uint64_t pid : portIds) {
            Port* p = getPort(pid);
            if (!p) continue;
            if (p->is_unresolved) {
                p->active = false;
                p->amount_per_craft = 0.0;
                continue;
            }
            auto it = evalPortBySlot.find(p->slot_key);
            if (it != evalPortBySlot.end()) {
                const gamedata::EvaluatedPort* ep = it->second;
                p->resource_key = ep->resource;
                p->amount_per_craft = ep->amount_per_craft;
                p->active = (eval.status == gamedata::EvalStatus::OK ||
                             eval.status == gamedata::EvalStatus::WARNING) &&
                            ep->active &&
                            (ep->amount_per_craft > 1e-9);
            } else {
                p->active = false;
                p->amount_per_craft = 0.0;
            }
        }
    };

    updatePortList(node->input_ports);
    updatePortList(node->output_ports);

    m_nodeEvals[node_id] = std::move(eval);
    updateWireStates();
}

void FactoryGraph::evaluateAllNodes() {
    refreshResolvedGlobals();
    for (const auto& node : m_nodes) {
        evaluateNode(node.id);
    }
    updateWireStates();
}

void FactoryGraph::updateWireStates() {
    for (auto& conn : m_connections) {
        const Port* fromPort = getPort(conn.from_port);
        const Port* toPort = getPort(conn.to_port);
        if (!fromPort || !toPort) {
            conn.state = WireState::INVALID;
            continue;
        }
        if (!fromPort->active || !toPort->active) {
            conn.state = WireState::INACTIVE;
            continue;
        }
        if (fromPort->resource_key.empty() || toPort->resource_key.empty() ||
            fromPort->resource_key != toPort->resource_key) {
            conn.state = WireState::INVALID;
            continue;
        }
        conn.state = WireState::OK;
        conn.resource_key = fromPort->resource_key;
    }
}

const gamedata::NodeEval* FactoryGraph::getNodeEval(uint64_t node_id) const {
    auto it = m_nodeEvals.find(node_id);
    return (it != m_nodeEvals.end()) ? &it->second : nullptr;
}

bool FactoryGraph::setNodeParam(uint64_t node_id, const std::string& param_id, const gamedata::ParamValue& value) {
    Node* node = getNode(node_id);
    if (!node || node->is_unresolved) {
        return false;
    }

    const gamedata::Machine* machine = m_gameData.findMachine(node->machine_key);
    const gamedata::Profile* profile =
        machine ? m_gameData.findProfile(machine->profile) : m_gameData.findProfile("default");
    if (!profile) {
        return false;
    }
    if (std::find(profile->params.begin(), profile->params.end(), param_id) == profile->params.end()) {
        return false;
    }
    if (m_gameData.findParameter(param_id) == nullptr) {
        return false;
    }

    node->params[param_id] = value;
    if (node->invalid_param_raw.is_object() && node->invalid_param_raw.contains(param_id)) {
        node->invalid_param_raw.erase(param_id);
    }
    evaluateNode(node_id);
    return true;
}

std::optional<gamedata::ParamValue> FactoryGraph::getNodeParam(uint64_t node_id, const std::string& param_id) const {
    const Node* node = getNode(node_id);
    if (!node) return std::nullopt;
    auto it = node->params.find(param_id);
    if (it != node->params.end()) {
        return it->second;
    }
    if (const auto* eval = getNodeEval(node_id)) {
        auto rit = eval->resolved_params.find(param_id);
        if (rit != eval->resolved_params.end()) {
            return rit->second;
        }
    }
    return std::nullopt;
}

bool FactoryGraph::setGlobalParam(const std::string& id, const gamedata::ParamValue& value) {
    const gamedata::ParameterDef* def = m_gameData.findGlobal(id);
    if (!def) {
        return false;
    }
    m_globals[id] = value;
    if (m_invalidGlobalRaw.is_object() && m_invalidGlobalRaw.contains(id)) {
        m_invalidGlobalRaw.erase(id);
    }
    m_visibilityDirty = true;
    evaluateAllNodes();
    return true;
}

bool FactoryGraph::clearGlobalParam(const std::string& id) {
    const gamedata::ParameterDef* def = m_gameData.findGlobal(id);
    if (!def) {
        return false;
    }
    bool changed = m_globals.erase(id) > 0;
    if (m_invalidGlobalRaw.is_object() && m_invalidGlobalRaw.erase(id) > 0) {
        changed = true;
    }
    if (changed) {
        m_visibilityDirty = true;
        evaluateAllNodes();
    }
    return true;
}

const gamedata::EntityVisibility& FactoryGraph::getEntityVisibility() const {
    if (m_visibilityDirty) {
        ++m_visibilityRevision;
        m_entityVisibility = gamedata::evaluateEntityVisibility(
            m_gameData, m_resolvedGlobals, m_visibilityRevision);
        m_visibilityDirty = false;
    }
    return m_entityVisibility;
}

bool FactoryGraph::isMachineVisibleForRecipe(std::string_view machineId, std::string_view recipeId) const {
    return gamedata::isMachineVisibleForRecipe(
        m_gameData, getEntityVisibility(), m_resolvedGlobals, machineId, recipeId);
}

void FactoryGraph::refreshResolvedGlobals() {
    std::unordered_set<std::string> invalidRawGlobalIds;
    if (m_invalidGlobalRaw.is_object()) {
        for (auto it = m_invalidGlobalRaw.begin(); it != m_invalidGlobalRaw.end(); ++it) {
            invalidRawGlobalIds.insert(it.key());
        }
    }
    m_resolvedGlobals = gamedata::resolveProjectGlobals(m_gameData, m_globals, invalidRawGlobalIds);
}

std::optional<gamedata::ParamValue> FactoryGraph::getGlobalParam(const std::string& id) const {
    auto it = m_globals.find(id);
    if (it != m_globals.end()) {
        return it->second;
    }
    auto rit = m_resolvedGlobals.resolved_values.find(id);
    if (rit != m_resolvedGlobals.resolved_values.end()) {
        return rit->second;
    }
    return std::nullopt;
}

void FactoryGraph::migrateNodeParams(Node& node, const std::string& /*oldMachineKey*/,
                                     const std::string& oldRecipeKey,
                                     const std::string& newMachineKey,
                                     const std::string& newRecipeKey) const {
    auto defaults = gamedata::resolveInitialNodeParams(m_gameData, newMachineKey, newRecipeKey, &m_resolvedGlobals);
    if (oldRecipeKey.empty() && node.params.empty() && node.invalid_param_raw.empty()) {
        node.params = std::move(defaults);
        return;
    }

    const gamedata::Machine* newMachine = m_gameData.findMachine(newMachineKey);
    const gamedata::Profile* newProfile =
        newMachine ? m_gameData.findProfile(newMachine->profile) : m_gameData.findProfile("default");

    std::unordered_map<std::string, gamedata::ParamValue> migrated;
    nlohmann::json prunedInvalidRaw = nlohmann::json::object();
    if (newProfile) {
        for (const std::string& pid : newProfile->params) {
            auto curIt = node.params.find(pid);
            if (curIt != node.params.end()) {
                migrated[pid] = curIt->second;
            } else {
                auto defIt = defaults.find(pid);
                if (defIt != defaults.end()) {
                    migrated[pid] = defIt->second;
                }
            }
            if (node.invalid_param_raw.is_object() &&
                node.invalid_param_raw.contains(pid) &&
                m_gameData.findParameter(pid) != nullptr) {
                prunedInvalidRaw[pid] = node.invalid_param_raw[pid];
            }
        }
    }

    node.params = std::move(migrated);
    node.invalid_param_raw = std::move(prunedInvalidRaw);
}

bool FactoryGraph::setNodeRecipe(uint64_t node_id, const std::string& recipe_key,
                                 const std::unordered_map<std::string, uint64_t>& preassigned_port_ids) {
    Node* node = getNode(node_id);
    if (!node) {
        return false;
    }
    const gamedata::Recipe* recipe = m_gameData.findRecipe(recipe_key);
    if (!recipe) {
        return false;
    }

    std::string oldRecipeKey = node->selected_recipe_key;
    std::string oldMachineKey = node->machine_key;

    node->selected_recipe_key = recipe_key;
    node->is_unresolved = false;
    node->unresolved_json = nlohmann::json::object();

    refreshResolvedGlobals();

    if (!recipe->compatible_machine_ids.empty()) {
        std::vector<std::string> visibleMachines;
        visibleMachines.reserve(recipe->compatible_machine_ids.size());
        for (const auto& mid : recipe->compatible_machine_ids) {
            if (isMachineVisibleForRecipe(mid, recipe_key)) {
                visibleMachines.push_back(mid);
            }
        }
        node->machine_key = resolvePreferredMachine(
            !visibleMachines.empty() ? visibleMachines : recipe->compatible_machine_ids);
    } else {
        node->machine_key = "";
    }

    migrateNodeParams(*node, oldMachineKey, oldRecipeKey, node->machine_key, node->selected_recipe_key);

    std::vector<uint64_t> old_ports;
    old_ports.reserve(node->input_ports.size() + node->output_ports.size());
    old_ports.insert(old_ports.end(), node->input_ports.begin(), node->input_ports.end());
    old_ports.insert(old_ports.end(), node->output_ports.begin(), node->output_ports.end());
    for (uint64_t pid : old_ports) {
        removePort(pid);
    }
    node->input_ports.clear();
    node->output_ports.clear();

    auto findPreassigned = [&](const std::string& slotKey) -> std::optional<uint64_t> {
        auto it = preassigned_port_ids.find(slotKey);
        if (it != preassigned_port_ids.end()) return it->second;
        return std::nullopt;
    };

    for (size_t i = 0; i < recipe->in.size(); ++i) {
        std::string slotKey = recipe->in[i].slot_key.empty()
                                  ? ("in:" + std::to_string(i))
                                  : recipe->in[i].slot_key;
        std::string resKey = recipe->in[i].res.value_or("");
        uint64_t port_id = addPort(resKey, node_id, slotKey, findPreassigned(slotKey));
        node->input_ports.push_back(port_id);
    }

    const gamedata::Machine* machine = m_gameData.findMachine(node->machine_key);
    const gamedata::MachineProfile* profile =
        machine ? m_gameData.findProfile(machine->profile) : m_gameData.findProfile("default");

    if (profile) {
        for (const auto& ep : profile->extra_ports) {
            if (ep.dir == "in") {
                std::string slotKey = ep.slot_key.empty() ? ("xin:" + ep.id) : ep.slot_key;
                std::string resKey = ep.resource.value_or("");
                uint64_t port_id = addPort(resKey, node_id, slotKey, findPreassigned(slotKey));
                node->input_ports.push_back(port_id);
            }
        }
    }

    for (size_t i = 0; i < recipe->out.size(); ++i) {
        std::string slotKey = recipe->out[i].slot_key.empty()
                                  ? ("out:" + std::to_string(i))
                                  : recipe->out[i].slot_key;
        std::string resKey = recipe->out[i].res.value_or("");
        uint64_t port_id = addPort(resKey, node_id, slotKey, findPreassigned(slotKey));
        node->output_ports.push_back(port_id);
    }

    if (profile) {
        for (const auto& ep : profile->extra_ports) {
            if (ep.dir == "out") {
                std::string slotKey = ep.slot_key.empty() ? ("xout:" + ep.id) : ep.slot_key;
                std::string resKey = ep.resource.value_or("");
                uint64_t port_id = addPort(resKey, node_id, slotKey, findPreassigned(slotKey));
                node->output_ports.push_back(port_id);
            }
        }
    }

    evaluateNode(node_id);
    VLOG(3) << "Set recipe '" << recipe_key << "' for node ID: " << node_id;
    return true;
}

bool FactoryGraph::changeMachine(uint64_t node_id, const std::string& machine_key,
                                 const std::unordered_map<std::string, uint64_t>& preassigned_port_ids) {
    Node* node = getNode(node_id);
    if (!node || node->is_unresolved) {
        return false;
    }

    const gamedata::Recipe* recipe = m_gameData.findRecipe(node->selected_recipe_key);
    if (!recipe) {
        return false;
    }

    if (!m_gameData.canMachineRunRecipe(machine_key, node->selected_recipe_key)) {
        return false;
    }

    std::string oldMachineKey = node->machine_key;
    refreshResolvedGlobals();
    migrateNodeParams(*node, oldMachineKey, node->selected_recipe_key, machine_key, node->selected_recipe_key);
    node->machine_key = machine_key;

    std::unordered_map<std::string, uint64_t> oldInputBySlot;
    for (uint64_t pid : node->input_ports) {
        if (const Port* p = getPort(pid)) {
            if (!p->slot_key.empty()) oldInputBySlot[p->slot_key] = pid;
        }
    }
    std::unordered_map<std::string, uint64_t> oldOutputBySlot;
    for (uint64_t pid : node->output_ports) {
        if (const Port* p = getPort(pid)) {
            if (!p->slot_key.empty()) oldOutputBySlot[p->slot_key] = pid;
        }
    }

    std::vector<uint64_t> oldAllPorts;
    oldAllPorts.reserve(node->input_ports.size() + node->output_ports.size());
    oldAllPorts.insert(oldAllPorts.end(), node->input_ports.begin(), node->input_ports.end());
    oldAllPorts.insert(oldAllPorts.end(), node->output_ports.begin(), node->output_ports.end());

    auto findPreassigned = [&](const std::string& slotKey) -> std::optional<uint64_t> {
        auto it = preassigned_port_ids.find(slotKey);
        if (it != preassigned_port_ids.end()) return it->second;
        return std::nullopt;
    };

    auto reuseOrAddPort = [&](const std::unordered_map<std::string, uint64_t>& oldBySlot,
                              const std::string& slotKey,
                              const std::string& resourceKey) -> uint64_t {
        auto it = oldBySlot.find(slotKey);
        if (it != oldBySlot.end()) {
            if (Port* existing = getPort(it->second)) {
                existing->is_unresolved = false;
            }
            return it->second;
        }
        return addPort(resourceKey, node_id, slotKey, findPreassigned(slotKey));
    };

    auto appendUnresolvedRecipePorts = [&](const std::vector<uint64_t>& existingIds,
                                           std::string_view requiredPrefix,
                                           std::vector<uint64_t>& target) {
        std::unordered_set<uint64_t> matched(target.begin(), target.end());
        for (uint64_t pid : existingIds) {
            if (matched.count(pid) > 0) continue;
            const Port* p = getPort(pid);
            if (p && p->is_unresolved && p->slot_key.rfind(requiredPrefix, 0) == 0) {
                target.push_back(pid);
            }
        }
    };

    const gamedata::Machine* newMachine = m_gameData.findMachine(machine_key);
    const gamedata::MachineProfile* newProfile =
        newMachine ? m_gameData.findProfile(newMachine->profile) : m_gameData.findProfile("default");

    std::vector<uint64_t> newInputPorts;
    for (size_t i = 0; i < recipe->in.size(); ++i) {
        std::string slotKey = recipe->in[i].slot_key.empty()
                                  ? ("in:" + std::to_string(i))
                                  : recipe->in[i].slot_key;
        newInputPorts.push_back(reuseOrAddPort(oldInputBySlot, slotKey, recipe->in[i].res.value_or("")));
    }

    if (newProfile) {
        for (const auto& ep : newProfile->extra_ports) {
            if (ep.dir == "in") {
                std::string slotKey = ep.slot_key.empty() ? ("xin:" + ep.id) : ep.slot_key;
                newInputPorts.push_back(reuseOrAddPort(oldInputBySlot, slotKey, ep.resource.value_or("")));
            }
        }
    }
    appendUnresolvedRecipePorts(node->input_ports, "in:", newInputPorts);

    std::vector<uint64_t> newOutputPorts;
    for (size_t i = 0; i < recipe->out.size(); ++i) {
        std::string slotKey = recipe->out[i].slot_key.empty()
                                  ? ("out:" + std::to_string(i))
                                  : recipe->out[i].slot_key;
        newOutputPorts.push_back(reuseOrAddPort(oldOutputBySlot, slotKey, recipe->out[i].res.value_or("")));
    }

    if (newProfile) {
        for (const auto& ep : newProfile->extra_ports) {
            if (ep.dir == "out") {
                std::string slotKey = ep.slot_key.empty() ? ("xout:" + ep.id) : ep.slot_key;
                newOutputPorts.push_back(reuseOrAddPort(oldOutputBySlot, slotKey, ep.resource.value_or("")));
            }
        }
    }
    appendUnresolvedRecipePorts(node->output_ports, "out:", newOutputPorts);

    std::unordered_set<uint64_t> keptPortIds(newInputPorts.begin(), newInputPorts.end());
    keptPortIds.insert(newOutputPorts.begin(), newOutputPorts.end());

    node->input_ports = std::move(newInputPorts);
    node->output_ports = std::move(newOutputPorts);

    for (uint64_t oldPid : oldAllPorts) {
        if (keptPortIds.find(oldPid) == keptPortIds.end()) {
            removePort(oldPid);
        }
    }

    evaluateNode(node_id);
    VLOG(3) << "Changed machine to '" << machine_key << "' for node ID: " << node_id;
    return true;
}

const std::vector<Node>& FactoryGraph::getNodes() const {
    return m_nodes;
}

bool FactoryGraph::isValidConnection(uint64_t from_port, uint64_t to_port) {
    Port* from_port_ptr = getPort(from_port);
    Port* to_port_ptr = getPort(to_port);
    if (!from_port_ptr || !to_port_ptr) {
        return false;
    }
    if (!from_port_ptr->active || !to_port_ptr->active) {
        return false;
    }
    if (from_port_ptr->resource_key.empty() || to_port_ptr->resource_key.empty()) {
        return false;
    }
    if (from_port_ptr->resource_key != to_port_ptr->resource_key) {
        return false;
    }
    if (isInputPort(from_port_ptr->id) || !isInputPort(to_port_ptr->id)) {
        return false;
    }
    return true;
}

bool FactoryGraph::connectionExists(uint64_t from_port, uint64_t to_port) const {
    auto range = m_connectionsByPort.equal_range(from_port);
    for (auto it = range.first; it != range.second; ++it) {
        uint64_t conn_id = it->second;
        auto idx_it = m_connectionIdToIndex.find(conn_id);
        if (idx_it == m_connectionIdToIndex.end()) continue;
        const Connection& c = m_connections[idx_it->second];
        if (c.from_port == from_port && c.to_port == to_port) return true;
    }
    return false;
}

const std::vector<Connection>& FactoryGraph::getConnections() const {
    return m_connections;
}

const std::vector<Port>& FactoryGraph::getPorts() const {
    return m_ports;
}

bool FactoryGraph::setPortConstraint(uint64_t port_id, double demand) {
    Port* port = getPort(port_id);
    if (!port) {
        return false;
    }
    port->user_constraint = demand;
    return true;
}

bool FactoryGraph::setPortConstraint(uint64_t port_id, double demand, ConstraintMode mode) {
    Port* port = getPort(port_id);
    if (!port) {
        return false;
    }
    port->user_constraint = demand;
    port->constraint_mode = mode;
    return true;
}

bool FactoryGraph::setPortConstraintMode(uint64_t port_id, ConstraintMode mode) {
    Port* port = getPort(port_id);
    if (!port) {
        return false;
    }
    port->constraint_mode = mode;
    return true;
}

bool FactoryGraph::isInputPort(uint64_t port_id) const {
    const Port* port = getPort(port_id);
    const Node* node = port ? getNode(port->node_id) : nullptr;
    return port && node &&
           std::find(node->input_ports.begin(), node->input_ports.end(), port_id) != node->input_ports.end();
}

void FactoryGraph::setMachinePreferred(const std::string& machineKey, bool preferred) {
    auto it = std::remove(m_preferredMachines.begin(), m_preferredMachines.end(), machineKey);
    m_preferredMachines.erase(it, m_preferredMachines.end());

    if (preferred) {
        m_preferredMachines.insert(m_preferredMachines.begin(), machineKey);
        VLOG(3) << "Machine set as preferred (High Priority): " << machineKey;
    }
}

bool FactoryGraph::isMachinePreferred(const std::string& machineKey) const {
    return std::find(m_preferredMachines.begin(), m_preferredMachines.end(), machineKey) !=
           m_preferredMachines.end();
}

std::string FactoryGraph::resolvePreferredMachine(const std::vector<std::string>& allowedMachines) const {
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

nlohmann::json FactoryGraph::serialize() const {
    nlohmann::json j;

    j["game"] = {
        {"id", m_gameData.game.id},
        {"data_version", m_gameData.game.data_version}
    };
    j["rate_unit"] = gamedata::rateUnitToString(m_rateUnit);
    j["display_precision"] = numfmt::displayPrecisionToString(m_displayPrecision);

    nlohmann::json globalsJson = nlohmann::json::object();
    for (const auto& [k, v] : m_globals) {
        globalsJson[k] = v.toJson();
    }
    if (m_unresolvedGlobals.is_object()) {
        for (auto it = m_unresolvedGlobals.begin(); it != m_unresolvedGlobals.end(); ++it) {
            globalsJson[it.key()] = it.value();
        }
    }
    if (m_invalidGlobalRaw.is_object()) {
        for (auto it = m_invalidGlobalRaw.begin(); it != m_invalidGlobalRaw.end(); ++it) {
            globalsJson[it.key()] = it.value();
        }
    }
    j["globals"] = std::move(globalsJson);

    j["nodes"] = nlohmann::json::array();
    for (const auto& node : m_nodes) {
        if (node.is_unresolved && node.unresolved_json.is_object() && !node.unresolved_json.empty()) {
            nlohmann::json node_json = node.unresolved_json;
            node_json["id"] = node.id;
            node_json["port_constraints"] = serializePortConstraints(node);
            nlohmann::json portModes = serializePortConstraintModes(node);
            if (!portModes.empty()) {
                node_json["port_constraint_modes"] = std::move(portModes);
            }
            j["nodes"].push_back(std::move(node_json));
            continue;
        }

        nlohmann::json node_json;
        node_json["id"] = node.id;
        node_json["name"] = node.name;
        node_json["recipe"] = node.selected_recipe_key;
        node_json["machine"] = node.machine_key;

        nlohmann::json paramsJson = nlohmann::json::object();
        for (const auto& [k, v] : node.params) {
            paramsJson[k] = v.toJson();
        }
        if (node.unresolved_params.is_object()) {
            for (auto it = node.unresolved_params.begin(); it != node.unresolved_params.end(); ++it) {
                paramsJson[it.key()] = it.value();
            }
        }
        if (node.invalid_param_raw.is_object()) {
            for (auto it = node.invalid_param_raw.begin(); it != node.invalid_param_raw.end(); ++it) {
                paramsJson[it.key()] = it.value();
            }
        }
        node_json["params"] = std::move(paramsJson);

        nlohmann::json portsJson = nlohmann::json::object();
        for (uint64_t pid : node.input_ports) {
            if (const Port* p = getPort(pid)) {
                if (!p->slot_key.empty()) {
                    portsJson[p->slot_key] = p->id;
                }
            }
        }
        for (uint64_t pid : node.output_ports) {
            if (const Port* p = getPort(pid)) {
                if (!p->slot_key.empty()) {
                    portsJson[p->slot_key] = p->id;
                }
            }
        }
        node_json["ports"] = std::move(portsJson);

        node_json["port_constraints"] = serializePortConstraints(node);
        nlohmann::json portModes = serializePortConstraintModes(node);
        if (!portModes.empty()) {
            node_json["port_constraint_modes"] = std::move(portModes);
        }

        j["nodes"].push_back(std::move(node_json));
    }

    j["connections"] = nlohmann::json::array();
    for (const auto& connection : m_connections) {
        nlohmann::json conn_json;
        conn_json["id"] = connection.id;
        conn_json["from_port"] = connection.from_port;
        conn_json["to_port"] = connection.to_port;
        j["connections"].push_back(std::move(conn_json));
    }

    j["preferred_machines"] = m_preferredMachines;
    j["next_node_id"] = m_nextNodeId;
    j["next_port_id"] = m_nextPortId;
    j["next_connection_id"] = m_nextConnectionId;
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

nlohmann::json FactoryGraph::serializePortConstraintModes(const Node& node) const {
    nlohmann::json modes = nlohmann::json::object();

    auto save_mode = [&](uint64_t port_id) {
        const Port* port = getPort(port_id);
        if (port && port->constraint_mode != ConstraintMode::AUTO) {
            modes[std::to_string(port_id)] = constraintModeToString(port->constraint_mode);
        }
    };

    for (uint64_t pid : node.input_ports) save_mode(pid);
    for (uint64_t pid : node.output_ports) save_mode(pid);

    return modes;
}

bool FactoryGraph::deserialize(const nlohmann::json& j, const GameData& game_data) {
    // §8.2 Check game.id compatibility before mutating state
    if (j.contains("game") && j["game"].is_object()) {
        std::string projGameId = j["game"].value("id", "");
        if (!projGameId.empty() && !game_data.game.id.empty() && projGameId != game_data.game.id) {
            m_lastLoadError = "Project game '" + projGameId +
                              "' does not match loaded game package '" + game_data.game.id + "'.";
            LOG(ERROR) << m_lastLoadError;
            return false;
        }
    }

    clear();
    this->m_gameData = game_data;
    gamedata::ensureBuiltInDefaultProfile(this->m_gameData);
    m_rateUnit = m_gameData.units.default_rate;
    m_displayPrecision = numfmt::DisplayPrecision::Auto;

    if (j.contains("game") && j["game"].is_object()) {
        std::string projVer = j["game"].value("data_version", "");
        if (!projVer.empty() && !m_gameData.game.data_version.empty() &&
            projVer != m_gameData.game.data_version) {
            std::string warnMsg = "Project was saved against game data version '" + projVer +
                                  "' and is being opened with version '" + m_gameData.game.data_version + "'.";
            LOG(WARNING) << warnMsg;
            m_loadWarnings.push_back(std::move(warnMsg));
        }
    }

    if (j.contains("rate_unit") && j["rate_unit"].is_string()) {
        std::string ru = j["rate_unit"].get<std::string>();
        if (auto parsedRu = gamedata::rateUnitFromString(ru)) {
            m_rateUnit = *parsedRu;
        } else if (ru == "per_sec" || ru == "per_second") {
            m_rateUnit = gamedata::RateUnit::PerSecond;
        } else if (ru == "per_min" || ru == "per_minute") {
            m_rateUnit = gamedata::RateUnit::PerMinute;
        } else if (ru == "per_hour") {
            m_rateUnit = gamedata::RateUnit::PerHour;
        }
    }

    if (j.contains("display_precision")) {
        if (j["display_precision"].is_string()) {
            if (auto parsedPrec = numfmt::displayPrecisionFromString(j["display_precision"].get<std::string>())) {
                m_displayPrecision = *parsedPrec;
            }
        } else if (j["display_precision"].is_number_integer()) {
            if (auto parsedPrec = numfmt::displayPrecisionFromInt(j["display_precision"].get<int>())) {
                m_displayPrecision = *parsedPrec;
            }
        }
    }

    if (j.contains("globals") && j["globals"].is_object()) {
        for (auto it = j["globals"].begin(); it != j["globals"].end(); ++it) {
            const std::string& key = it.key();
            const nlohmann::json& val = it.value();
            const gamedata::ParameterDef* gdef = m_gameData.findGlobal(key);
            if (!gdef) {
                m_unresolvedGlobals[key] = val;
                if (!m_warnedUnresolvedParam) {
                    m_warnedUnresolvedParam = true;
                    std::string msg = "Project contains unknown parameter IDs not in the loaded schema; preserved in memory.";
                    LOG(WARNING) << msg;
                    m_loadWarnings.push_back(std::move(msg));
                }
            } else {
                auto parsed = gamedata::ParamValue::fromJson(val);
                if (parsed.has_value()) {
                    m_globals[key] = std::move(*parsed);
                } else {
                    m_invalidGlobalRaw[key] = val;
                    std::string msg = "Global parameter '" + key + "' failed validation; using schema default.";
                    LOG(WARNING) << msg;
                    m_loadWarnings.push_back(std::move(msg));
                }
            }
        }
    }

    refreshResolvedGlobals();
    m_visibilityDirty = true;
    std::unordered_set<std::string> seenLoadWarnings(m_loadWarnings.begin(), m_loadWarnings.end());
    for (const auto& [gid, msgs] : m_resolvedGlobals.messages_by_global) {
        for (const auto& m : msgs) {
            if (seenLoadWarnings.insert(m.text).second) {
                m_loadWarnings.push_back(m.text);
            }
        }
    }

    if (j.contains("next_node_id") && j["next_node_id"].is_number()) m_nextNodeId = j.value("next_node_id", 0ULL);
    if (j.contains("next_connection_id") && j["next_connection_id"].is_number()) m_nextConnectionId = j.value("next_connection_id", 0ULL);
    if (j.contains("next_port_id") && j["next_port_id"].is_number()) m_nextPortId = j.value("next_port_id", 0ULL);

    if (j.contains("preferred_machines") && j["preferred_machines"].is_array()) {
        m_preferredMachines = j["preferred_machines"].get<std::vector<std::string>>();
    }

    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& node_json : j["nodes"]) {
            deserializeNode(node_json, m_gameData);
        }
    }

    if (j.contains("connections") && j["connections"].is_array()) {
        for (const auto& conn_json : j["connections"]) {
            deserializeConnection(conn_json);
        }
    }

    evaluateAllNodes();

    VLOG(3) << "Deserialized FactoryGraph.";
    return true;
}

void FactoryGraph::applyStoredPortConstraints(Port& p, const std::string& originalIdStr,
                                              const nlohmann::json& constraints,
                                              const nlohmann::json& constraintModes) {
    if (constraints.is_object() && constraints.contains(originalIdStr)) {
        const auto& entry = constraints[originalIdStr];
        if (entry.is_number()) {
            p.user_constraint = entry.get<double>();
        } else if (entry.is_object()) {
            if (entry.contains("value") && entry["value"].is_number()) {
                p.user_constraint = entry["value"].get<double>();
            }
            if (entry.contains("mode") && entry["mode"].is_string()) {
                p.constraint_mode = constraintModeFromString(entry["mode"].get<std::string>());
            }
        }
    }
    if (constraintModes.is_object() && constraintModes.contains(originalIdStr) &&
        constraintModes[originalIdStr].is_string()) {
        p.constraint_mode = constraintModeFromString(constraintModes[originalIdStr].get<std::string>());
    }
}

void FactoryGraph::deserializeNode(const nlohmann::json& node_json, const GameData& game_data) {
    uint64_t nodeId = node_json.value("id", static_cast<uint64_t>(-1));
    if (nodeId == static_cast<uint64_t>(-1)) {
        LOG(ERROR) << "Invalid node data in saved graph: missing ID.";
        return;
    }

    if (nodeId >= m_nextNodeId) m_nextNodeId = nodeId + 1;

    bool isV4Format = node_json.contains("recipe") ||
                      (node_json.contains("ports") && node_json["ports"].is_object());

    std::string rawRecipeKey = node_json.contains("recipe")
                                   ? node_json.value("recipe", "")
                                   : node_json.value("selected_recipe_key", "");
    std::string rawMachineKey = node_json.value("machine", "");

    std::string recipeKey = game_data.resolveRecipeId(rawRecipeKey);
    std::string machKey = rawMachineKey.empty() ? "" : game_data.resolveMachineId(rawMachineKey);

    const auto& portConstraints = node_json.value("port_constraints", nlohmann::json::object());
    const auto& portConstraintModes = node_json.value("port_constraint_modes", nlohmann::json::object());

    auto allocateStoredOrNewPortId = [&](std::optional<uint64_t> storedId, std::string& outOrigStr) -> uint64_t {
        if (storedId.has_value()) {
            uint64_t pid = *storedId;
            outOrigStr = std::to_string(pid);
            if (m_portIdToIndex.find(pid) != m_portIdToIndex.end()) {
                LOG(WARNING) << "Duplicate Port ID " << pid << " detected on node " << nodeId
                             << ". Remapping to new ID.";
                pid = m_nextPortId++;
            }
            if (pid >= m_nextPortId) m_nextPortId = pid + 1;
            return pid;
        }
        uint64_t pid = m_nextPortId++;
        outOrigStr.clear();
        return pid;
    };

    // Unresolved recipe or (in v4 format) explicitly stored machine not found in GamePackage
    bool machineUnresolvedInV4 = isV4Format && !rawMachineKey.empty() && machKey.empty();
    if (recipeKey.empty() || machineUnresolvedInV4) {
        if (!isV4Format && recipeKey.empty()) {
            // Legacy v3 behavior: skip unknown recipe node if not v4 format
            LOG(ERROR) << "Unknown key: " << rawRecipeKey << " (prefix: rec@)";
            return;
        }
        std::string warnMsg = "Node " + std::to_string(nodeId) + " references unresolved recipe '" +
                              rawRecipeKey + "' or machine '" + rawMachineKey + "'; preserved in ERROR state.";
        LOG(WARNING) << warnMsg;
        m_loadWarnings.push_back(std::move(warnMsg));

        Node node;
        node.id = nodeId;
        node.is_unresolved = true;
        node.unresolved_json = node_json;
        node.selected_recipe_key = rawRecipeKey;
        node.machine_key = rawMachineKey;
        node.name = node_json.value("name", rawRecipeKey.empty() ? "Unresolved" : rawRecipeKey);

        if (node_json.contains("ports") && node_json["ports"].is_object()) {
            for (auto it = node_json["ports"].begin(); it != node_json["ports"].end(); ++it) {
                if (!it.value().is_number()) continue;
                std::string slotKey = it.key();
                std::string origIdStr;
                uint64_t pid = allocateStoredOrNewPortId(it.value().get<uint64_t>(), origIdStr);
                Port p(pid, nodeId, "", slotKey, 0.0, false, true);
                if (!origIdStr.empty()) {
                    applyStoredPortConstraints(p, origIdStr, portConstraints, portConstraintModes);
                }
                if (slotKey.rfind("in:", 0) == 0 || slotKey.rfind("xin:", 0) == 0) {
                    node.input_ports.push_back(p.id);
                } else {
                    node.output_ports.push_back(p.id);
                }
                m_ports.push_back(p);
                addPortToIndex(p.id, m_ports.size() - 1);
            }
        }

        m_nodes.push_back(std::move(node));
        addNodeToIndex(nodeId, m_nodes.size() - 1);
        return;
    }

    const gamedata::Recipe* recipe = game_data.findRecipe(recipeKey);
    if (!recipe) return;

    Node node;
    node.id = nodeId;
    node.selected_recipe_key = recipeKey;
    node.name = node_json.value("name", recipe->name);

    if (!recipe->compatible_machine_ids.empty()) {
        bool machValid = !machKey.empty() &&
                         std::find(recipe->compatible_machine_ids.begin(),
                                   recipe->compatible_machine_ids.end(),
                                   machKey) != recipe->compatible_machine_ids.end();
        node.machine_key = machValid ? machKey : resolvePreferredMachine(recipe->compatible_machine_ids);
    } else {
        LOG(WARNING) << "Recipe '" << recipeKey << "' has no associated machines.";
        node.machine_key = "";
    }

    const gamedata::Machine* machine = game_data.findMachine(node.machine_key);
    const gamedata::Profile* profile =
        machine ? game_data.findProfile(machine->profile) : game_data.findProfile("default");

    node.params = gamedata::resolveInitialNodeParams(game_data, node.machine_key, node.selected_recipe_key, &m_resolvedGlobals);

    if (node_json.contains("params") && node_json["params"].is_object()) {
        for (auto it = node_json["params"].begin(); it != node_json["params"].end(); ++it) {
            const std::string& paramId = it.key();
            const nlohmann::json& val = it.value();
            bool inProfile = profile &&
                             std::find(profile->params.begin(), profile->params.end(), paramId) != profile->params.end() &&
                             game_data.findParameter(paramId) != nullptr;
            if (!inProfile) {
                node.unresolved_params[paramId] = val;
                if (!m_warnedUnresolvedParam) {
                    m_warnedUnresolvedParam = true;
                    std::string msg = "Project contains unknown parameter IDs not in the loaded schema; preserved in memory.";
                    LOG(WARNING) << msg;
                    m_loadWarnings.push_back(std::move(msg));
                }
            } else {
                auto parsed = gamedata::ParamValue::fromJson(val);
                if (parsed.has_value()) {
                    node.params[paramId] = std::move(*parsed);
                } else {
                    node.invalid_param_raw[paramId] = val;
                    LOG(WARNING) << "Node " << nodeId << " parameter '" << paramId
                                 << "' failed validation; using schema default.";
                }
            }
        }
    } else {
        // Legacy v3 fallback for clock_speed and production_multiplier if present in JSON
        if (node_json.contains("clock_speed") && node_json["clock_speed"].is_number() && profile) {
            if (std::find(profile->params.begin(), profile->params.end(), "clock") != profile->params.end()) {
                node.params["clock"] = gamedata::ParamValue::number(node_json["clock_speed"].get<double>());
            }
        }
        if (node_json.contains("production_multiplier") && node_json["production_multiplier"].is_number() && profile) {
            double pm = node_json["production_multiplier"].get<double>();
            if (std::find(profile->params.begin(), profile->params.end(), "output_mult") != profile->params.end()) {
                node.params["output_mult"] = gamedata::ParamValue::number(pm);
            } else if (std::find(profile->params.begin(), profile->params.end(), "production_multiplier") != profile->params.end()) {
                node.params["production_multiplier"] = gamedata::ParamValue::number(pm);
            }
        }
    }

    // Build expected port slots
    struct ExpectedSlot {
        std::string slot_key;
        std::string resource_key;
        bool is_input;
    };
    std::vector<ExpectedSlot> expectedSlots;
    for (size_t i = 0; i < recipe->in.size(); ++i) {
        std::string sk = recipe->in[i].slot_key.empty() ? ("in:" + std::to_string(i)) : recipe->in[i].slot_key;
        expectedSlots.push_back({sk, recipe->in[i].res.value_or(""), true});
    }
    if (profile) {
        for (const auto& ep : profile->extra_ports) {
            if (ep.dir == "in") {
                std::string sk = ep.slot_key.empty() ? ("xin:" + ep.id) : ep.slot_key;
                expectedSlots.push_back({sk, ep.resource.value_or(""), true});
            }
        }
    }
    for (size_t i = 0; i < recipe->out.size(); ++i) {
        std::string sk = recipe->out[i].slot_key.empty() ? ("out:" + std::to_string(i)) : recipe->out[i].slot_key;
        expectedSlots.push_back({sk, recipe->out[i].res.value_or(""), false});
    }
    if (profile) {
        for (const auto& ep : profile->extra_ports) {
            if (ep.dir == "out") {
                std::string sk = ep.slot_key.empty() ? ("xout:" + ep.id) : ep.slot_key;
                expectedSlots.push_back({sk, ep.resource.value_or(""), false});
            }
        }
    }

    std::unordered_map<std::string, uint64_t> storedSlotToId;
    if (node_json.contains("ports") && node_json["ports"].is_object()) {
        for (auto it = node_json["ports"].begin(); it != node_json["ports"].end(); ++it) {
            if (it.value().is_number()) {
                storedSlotToId[it.key()] = it.value().get<uint64_t>();
            }
        }
    } else {
        const auto& savedIn = node_json.value("input_ports", nlohmann::json::array());
        if (savedIn.is_array()) {
            for (size_t i = 0; i < savedIn.size(); ++i) {
                if (savedIn[i].is_number()) {
                    storedSlotToId["in:" + std::to_string(i)] = savedIn[i].get<uint64_t>();
                }
            }
        }
        const auto& savedOut = node_json.value("output_ports", nlohmann::json::array());
        if (savedOut.is_array()) {
            for (size_t i = 0; i < savedOut.size(); ++i) {
                if (savedOut[i].is_number()) {
                    storedSlotToId["out:" + std::to_string(i)] = savedOut[i].get<uint64_t>();
                }
            }
        }
    }

    std::unordered_set<std::string> matchedSlots;
    for (const auto& es : expectedSlots) {
        matchedSlots.insert(es.slot_key);
        std::optional<uint64_t> storedId;
        auto it = storedSlotToId.find(es.slot_key);
        if (it != storedSlotToId.end()) {
            storedId = it->second;
        }
        std::string origIdStr;
        uint64_t pid = allocateStoredOrNewPortId(storedId, origIdStr);
        Port p(pid, nodeId, es.resource_key, es.slot_key, 0.0, true, false);
        if (!origIdStr.empty()) {
            applyStoredPortConstraints(p, origIdStr, portConstraints, portConstraintModes);
        }
        if (es.is_input) {
            node.input_ports.push_back(p.id);
        } else {
            node.output_ports.push_back(p.id);
        }
        m_ports.push_back(p);
        addPortToIndex(p.id, m_ports.size() - 1);
    }

    // §8.2: Stored slot key missing in the current data is created as an unresolved
    // input (in:/xin:) or output (out:/xout:) port (inactive, wires INACTIVE) and kept.
    for (const auto& [slotKey, rawPid] : storedSlotToId) {
        if (matchedSlots.find(slotKey) != matchedSlots.end()) continue;
        std::string origIdStr;
        uint64_t pid = allocateStoredOrNewPortId(rawPid, origIdStr);
        Port p(pid, nodeId, "", slotKey, 0.0, false, true);
        if (!origIdStr.empty()) {
            applyStoredPortConstraints(p, origIdStr, portConstraints, portConstraintModes);
        }
        if (slotKey.rfind("in:", 0) == 0 || slotKey.rfind("xin:", 0) == 0) {
            node.input_ports.push_back(p.id);
        } else {
            node.output_ports.push_back(p.id);
        }
        m_ports.push_back(p);
        addPortToIndex(p.id, m_ports.size() - 1);
    }

    m_nodes.push_back(std::move(node));
    addNodeToIndex(nodeId, m_nodes.size() - 1);
}

void FactoryGraph::deserializeConnection(const nlohmann::json& conn_json) {
    Connection conn;
    conn.id = conn_json.value("id", static_cast<uint64_t>(-1));
    conn.from_port = conn_json.value("from_port", static_cast<uint64_t>(-1));
    conn.to_port = conn_json.value("to_port", static_cast<uint64_t>(-1));

    if (conn.id == static_cast<uint64_t>(-1) ||
        conn.from_port == static_cast<uint64_t>(-1) ||
        conn.to_port == static_cast<uint64_t>(-1)) {
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

    if (isInputPort(fromPort->id) || !isInputPort(toPort->id)) {
        LOG(WARNING) << "Skipping connection " << conn.id
                     << ": Port direction mismatch.";
        return;
    }

    conn.resource_key = !fromPort->resource_key.empty()
                            ? fromPort->resource_key
                            : conn_json.value("resource_key", "");

    if (conn.id >= m_nextConnectionId) m_nextConnectionId = conn.id + 1;

    m_connections.push_back(conn);
    addConnectionToIndex(conn.id, m_connections.size() - 1);
    m_connectionsByPort.insert({conn.from_port, conn.id});
    m_connectionsByPort.insert({conn.to_port, conn.id});
}
