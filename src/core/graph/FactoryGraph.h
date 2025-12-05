#ifndef FACTORYGRAPH_H
#define FACTORYGRAPH_H
#include <utility>
#include <vector>
#include <unordered_map>

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

    uint64_t addPort(const std::string& resource_key, uint64_t node_id);
    bool removePort(uint64_t port_id);
    Port* getPort(uint64_t id);
    const Port* getPort(uint64_t id) const;
    [[nodiscard]] const std::vector<Port>& getPorts() const;
    bool setPortConstraint(uint64_t port_id, double demand);
    bool isInputPort(uint64_t port_id) const;

    void clear();

    nlohmann::json serialize() const;

    void deserialize(const nlohmann::json &j, const GameData &game_data);

    const GameData& getGameData() const {
        return game_data;
    }


    explicit FactoryGraph(GameData game_data) {
        this->game_data = std::move(game_data);
    }

    ~FactoryGraph() = default;

private:
    GameData game_data;

    std::vector<Node> nodes;
    std::vector<Port> ports;
    std::vector<Connection> connections;

    // Hash maps for O(1) lookups
    std::unordered_map<uint64_t, size_t> node_id_to_index_;
    std::unordered_map<uint64_t, size_t> port_id_to_index_;
    std::unordered_map<uint64_t, size_t> connection_id_to_index_;

    std::unordered_multimap<uint64_t, uint64_t> connectionsByPort; // Maps port_id to connection_id for quick access

    uint64_t next_node_id = 0;
    uint64_t next_port_id = 0;
    uint64_t next_connection_id = 0;

    // Helper methods to maintain hash maps
    void addNodeToIndex(uint64_t id, size_t index) { node_id_to_index_[id] = index; }
    void addPortToIndex(uint64_t id, size_t index) { port_id_to_index_[id] = index; }
    void addConnectionToIndex(uint64_t id, size_t index) { connection_id_to_index_[id] = index; }
    void removeNodeFromIndex(uint64_t id) { node_id_to_index_.erase(id); }
    void removePortFromIndex(uint64_t id) { port_id_to_index_.erase(id); }
    void removeConnectionFromIndex(uint64_t id) { connection_id_to_index_.erase(id); }
};

#endif //FACTORYGRAPH_H