#ifndef FACTORYGRAPH_H
#define FACTORYGRAPH_H
#include <utility>
#include <vector>
#include <unordered_map>
#include <absl/log/log.h>

#include "Connection.h"
#include "Node.h"
#include "Port.h"
#include "core/data/GameData.h"
#include <nlohmann/json.hpp>

class FactoryGraph {
public:
    uint64_t addNode(const std::string& name, std::string recipe_key);
    void restoreNode(const Node& node, const std::vector<Port>& ports);
    bool removeNode(uint64_t node_id);
    bool setNodeRecipe(uint64_t node_id, const std::string& recipe_key);
    bool setNodeClockSpeed(uint64_t node_id, double speed);
    bool setProductionMultiplier(uint64_t node_id, double multiplier);
    bool changeMachine(uint64_t node_id, const std::string& machine_key);
    Node* getNode(uint64_t id);

    const Node *getNode(uint64_t id) const;

    [[nodiscard]] const std::vector<Node>& getNodes() const;

    bool isValidConnection(uint64_t from_port, uint64_t to_port);
    bool connectionExists(uint64_t from_port, uint64_t to_port);
    uint64_t addConnection(uint64_t from_port, uint64_t to_port);
    void restoreConnection(const Connection& connection);
    bool removeConnection(uint64_t from_port, uint64_t to_port);
    Connection* getConnection(uint64_t id);
    const Connection *getConnection(uint64_t id) const;
    Connection* getConnection(uint64_t from_port, uint64_t to_port);
    std::vector<Connection*> getConnectionsForPort(uint64_t id);
    [[nodiscard]] const std::vector<Connection>& getConnections() const;

    uint64_t addPort(const std::string& resource_key, uint64_t node_id, ConstraintType constraint_type = ConstraintType::LIMIT);
    bool removePort(uint64_t port_id);
    Port* getPort(uint64_t id);
    const Port* getPort(uint64_t id) const;
    [[nodiscard]] const std::vector<Port>& getPorts() const;
    bool setPortConstraint(uint64_t port_id, double demand);
    bool isInputPort(uint64_t port_id) const;

    void setMachinePreferred(const std::string& machineKey, bool preferred);
    bool isMachinePreferred(const std::string& machineKey) const;
    std::string resolvePreferredMachine(const std::vector<std::string>& allowedMachines) const;

    void clear();

    nlohmann::json serialize() const;

    void deserialize(const nlohmann::json &j, const GameData &game_data);

    const GameData& getGameData() const {
        return m_gameData;
    }


    explicit FactoryGraph(GameData game_data) {
        this->m_gameData = std::move(game_data);
    }

    ~FactoryGraph() = default;

private:
    GameData m_gameData;

    std::vector<Node> m_nodes;
    std::vector<Port> m_ports;
    std::vector<Connection> m_connections;

    // Hash maps for O(1) lookups
    std::unordered_map<uint64_t, size_t> m_nodeIdToIndex;
    std::unordered_map<uint64_t, size_t> m_portIdToIndex;
    std::unordered_map<uint64_t, size_t> m_connectionIdToIndex;

    std::unordered_multimap<uint64_t, uint64_t> m_connectionsByPort; // Maps port_id to connection_id for quick access
    std::vector<std::string> m_preferredMachines; // Ordered list of preferred machine keys

    uint64_t m_nextNodeId = 0;
    uint64_t m_nextPortId = 0;
    uint64_t m_nextConnectionId = 0;

    // Helper methods to maintain hash maps
    void addNodeToIndex(uint64_t id, size_t index) { m_nodeIdToIndex[id] = index; }
    void addPortToIndex(uint64_t id, size_t index) { m_portIdToIndex[id] = index; }
    void addConnectionToIndex(uint64_t id, size_t index) { m_connectionIdToIndex[id] = index; }
    void removeNodeFromIndex(uint64_t id) { m_nodeIdToIndex.erase(id); }
    void removePortFromIndex(uint64_t id) { m_portIdToIndex.erase(id); }
    void removeConnectionFromIndex(uint64_t id) { m_connectionIdToIndex.erase(id); }

    nlohmann::json serializePortConstraints(const Node& node) const;

    void deserializeNode(const nlohmann::json &node_json, const GameData &game_data);
    void processPorts(const std::vector<RecipePort> &recipePorts, const nlohmann::json &savedPortsJson,
                      std::vector<uint64_t> &nodePortList, const nlohmann::json &constraints, uint64_t nodeId,
                      ConstraintType constraintType);
    void deserializeConnection(const nlohmann::json &conn_json);

    template<typename TMap>
    std::string resolveLegacyKey(std::string key, const std::string &prefix,
                                 const TMap &primaryMap,
                                 const auto &aliasMap) {
        if (key.empty()) return "";

        // Direct match
        if (primaryMap.find(key) != primaryMap.end()) return key;

        // Alias lookup
        auto it = aliasMap.find(prefix + key);
        if (it != aliasMap.end()) return it->second;

        LOG(ERROR) << "Unknown key: " << key << " (prefix: " << prefix << ")";
        return ""; // Empty string indicates failure
    }

    ConstraintType getPortConstraintType(const Recipe &recipe, bool is_input);
};

#endif //FACTORYGRAPH_H