#ifndef FACTORYGRAPH_H
#define FACTORYGRAPH_H
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <absl/log/log.h>
#include <nlohmann/json.hpp>

#include "Connection.h"
#include "Node.h"
#include "Port.h"
#include "core/data/GameData.h"
#include "core/data/GameDataLoader.h"
#include "core/data/NodeEvaluator.h"

class FactoryGraph {
public:
    uint64_t addNode(const std::string& name, std::string recipe_key);
    void restoreNode(const Node& node, const std::vector<Port>& ports);
    bool removeNode(uint64_t node_id);
    bool setNodeRecipe(uint64_t node_id, const std::string& recipe_key,
                       const std::unordered_map<std::string, uint64_t>& preassigned_port_ids = {});
    bool changeMachine(uint64_t node_id, const std::string& machine_key,
                       const std::unordered_map<std::string, uint64_t>& preassigned_port_ids = {});

    bool setNodeParam(uint64_t node_id, const std::string& param_id, const gamedata::ParamValue& value);
    std::optional<gamedata::ParamValue> getNodeParam(uint64_t node_id, const std::string& param_id) const;

    bool setGlobalParam(const std::string& id, const gamedata::ParamValue& value);
    bool clearGlobalParam(const std::string& id);
    std::optional<gamedata::ParamValue> getGlobalParam(const std::string& id) const;
    const std::unordered_map<std::string, gamedata::ParamValue>& getGlobals() const { return m_globals; }
    const gamedata::ResolvedGlobals& getResolvedGlobals() const { return m_resolvedGlobals; }

    gamedata::RateUnit getRateUnit() const { return m_rateUnit; }
    void setRateUnit(gamedata::RateUnit unit) { m_rateUnit = unit; }
    double toDisplayRate(double canonicalPerSec) const {
        return m_gameData.toDisplayRate(canonicalPerSec, m_rateUnit);
    }
    double fromDisplayRate(double displayVal) const {
        return m_gameData.fromDisplayRate(displayVal, m_rateUnit);
    }
    double toResourceDisplayRate(std::string_view resource_id, double canonicalPerSec) const {
        return toDisplayRate(canonicalPerSec) * m_gameData.resourceDisplayScale(resource_id);
    }
    double fromResourceDisplayRate(std::string_view resource_id, double displayVal) const {
        const double scale = m_gameData.resourceDisplayScale(resource_id);
        return fromDisplayRate((scale > 0.0) ? (displayVal / scale) : displayVal);
    }
    std::string formatResourceRateSuffix(std::string_view resource_id) const {
        const std::string kindUnit = m_gameData.resourceDisplayUnit(resource_id);
        const std::string rateSuffix = gamedata::rateUnitSuffix(m_rateUnit);
        return kindUnit.empty() ? rateSuffix : (kindUnit + rateSuffix);
    }
    std::string formatResourceRateSpacedSuffix(std::string_view resource_id) const {
        std::string suffix = formatResourceRateSuffix(resource_id);
        if (!suffix.empty() && suffix.front() != '/') {
            return " " + suffix;
        }
        return suffix;
    }

    void evaluateNode(uint64_t node_id);
    void evaluateAllNodes();
    void updateWireStates();
    const gamedata::NodeEval* getNodeEval(uint64_t node_id) const;

    Node* getNode(uint64_t id);
    const Node* getNode(uint64_t id) const;
    [[nodiscard]] const std::vector<Node>& getNodes() const;

    bool isValidConnection(uint64_t from_port, uint64_t to_port);
    bool connectionExists(uint64_t from_port, uint64_t to_port) const;
    uint64_t addConnection(uint64_t from_port, uint64_t to_port);
    void restoreConnection(const Connection& connection);
    bool removeConnection(uint64_t conn_id);
    bool removeConnection(uint64_t from_port, uint64_t to_port);
    Connection* getConnection(uint64_t id);
    const Connection* getConnection(uint64_t id) const;
    Connection* getConnection(uint64_t from_port, uint64_t to_port);
    const Connection* getConnection(uint64_t from_port, uint64_t to_port) const;
    std::vector<Connection*> getConnectionsForPort(uint64_t id);
    [[nodiscard]] const std::vector<Connection>& getConnections() const;

    uint64_t addPort(const std::string& resource_key, uint64_t node_id,
                     const std::string& slot_key = "",
                     std::optional<uint64_t> preassigned_id = std::nullopt);
    bool removePort(uint64_t port_id);
    Port* getPort(uint64_t id);
    const Port* getPort(uint64_t id) const;
    Port* getPortBySlot(uint64_t node_id, const std::string& slot_key);
    const Port* getPortBySlot(uint64_t node_id, const std::string& slot_key) const;
    [[nodiscard]] const std::vector<Port>& getPorts() const;
    bool setPortConstraint(uint64_t port_id, double demand);
    bool setPortConstraint(uint64_t port_id, double demand, ConstraintMode mode);
    bool setPortConstraintMode(uint64_t port_id, ConstraintMode mode);
    bool isInputPort(uint64_t port_id) const;

    void setMachinePreferred(const std::string& machineKey, bool preferred);
    bool isMachinePreferred(const std::string& machineKey) const;
    std::string resolvePreferredMachine(const std::vector<std::string>& allowedMachines) const;

    void clear();

    nlohmann::json serialize() const;
    bool deserialize(const nlohmann::json& j, const GameData& game_data);

    const std::string& getLastLoadError() const { return m_lastLoadError; }
    const std::vector<std::string>& getLoadWarnings() const { return m_loadWarnings; }
    const std::vector<std::string>& getLastLoadWarnings() const { return m_loadWarnings; }

    const GameData& getGameData() const {
        return m_gameData;
    }

    explicit FactoryGraph(GameData game_data)
        : m_gameData(std::move(game_data)) {
        gamedata::ensureBuiltInDefaultProfile(m_gameData);
        m_rateUnit = m_gameData.units.default_rate;
        refreshResolvedGlobals();
    }

    ~FactoryGraph() = default;

private:
    GameData m_gameData;
    gamedata::RateUnit m_rateUnit = gamedata::RateUnit::PerMin;
    std::unordered_map<std::string, gamedata::ParamValue> m_globals;
    nlohmann::json m_unresolvedGlobals = nlohmann::json::object();
    nlohmann::json m_invalidGlobalRaw = nlohmann::json::object();
    gamedata::ResolvedGlobals m_resolvedGlobals;
    std::unordered_map<uint64_t, gamedata::NodeEval> m_nodeEvals;

    std::string m_lastLoadError;
    std::vector<std::string> m_loadWarnings;
    bool m_warnedUnresolvedParam = false;

    std::vector<Node> m_nodes;
    std::vector<Port> m_ports;
    std::vector<Connection> m_connections;

    // Hash maps for O(1) lookups
    std::unordered_map<uint64_t, size_t> m_nodeIdToIndex;
    std::unordered_map<uint64_t, size_t> m_portIdToIndex;
    std::unordered_map<uint64_t, size_t> m_connectionIdToIndex;

    std::unordered_multimap<uint64_t, uint64_t> m_connectionsByPort;
    std::vector<std::string> m_preferredMachines;

    uint64_t m_nextNodeId = 0;
    uint64_t m_nextPortId = 0;
    uint64_t m_nextConnectionId = 0;

    void addNodeToIndex(uint64_t id, size_t index) { m_nodeIdToIndex[id] = index; }
    void addPortToIndex(uint64_t id, size_t index) { m_portIdToIndex[id] = index; }
    void addConnectionToIndex(uint64_t id, size_t index) { m_connectionIdToIndex[id] = index; }
    void removeNodeFromIndex(uint64_t id) { m_nodeIdToIndex.erase(id); }
    void removePortFromIndex(uint64_t id) { m_portIdToIndex.erase(id); }
    void removeConnectionFromIndex(uint64_t id) { m_connectionIdToIndex.erase(id); }

    void refreshResolvedGlobals();
    void migrateNodeParams(Node& node, const std::string& oldMachineKey,
                           const std::string& oldRecipeKey,
                           const std::string& newMachineKey,
                           const std::string& newRecipeKey) const;

    nlohmann::json serializePortConstraints(const Node& node) const;
    nlohmann::json serializePortConstraintModes(const Node& node) const;

    void deserializeNode(const nlohmann::json& node_json, const GameData& game_data);
    void applyStoredPortConstraints(Port& p, const std::string& originalIdStr,
                                    const nlohmann::json& constraints,
                                    const nlohmann::json& constraintModes);
    void deserializeConnection(const nlohmann::json& conn_json);
};

#endif //FACTORYGRAPH_H
