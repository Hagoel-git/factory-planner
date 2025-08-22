//
// Created by hagoel on 8/1/25.
//

#ifndef FACTORYGRAPH_H
#define FACTORYGRAPH_H
#include <utility>
#include <vector>
#include <unordered_map>

#include "Connection.h"
#include "Node.h"
#include "Port.h"
#include "../common/GameData.h"
#include "../utils/GameDataManager.h"

class FactoryGraph {
public:
    int addNode(const std::string& name, NodeType type, int recipe_id);
    void restoreNode(const Node& node, const std::vector<Port>& ports);
    bool removeNode(int node_id);
    bool setNodeRecipe(int node_id, int recipe_id);
    Node* getNode(int id);
    [[nodiscard]] const std::vector<Node>& getNodes() const;

    bool isValidConnection(int from_port, int to_port);
    bool connectionExists(int from_port, int to_port);
    int addConnection(int from_port, int to_port);
    void restoreConnection(const Connection& connection);
    bool removeConnection(int from_port, int to_port);
    Connection* getConnection(int id);
    Connection* getConnection(int from_port, int to_port);
    std::vector<Connection*> getConnectionsForPort(int id);
    [[nodiscard]] const std::vector<Connection>& getConnections() const;

    int addPort(int resource_id, int node_id, bool isInput);
    bool removePort(int port_id);
    Port* getPort(int id);
    [[nodiscard]] const std::vector<Port>& getPorts() const;
    bool setPortDemand(int port_id, double demand);

    void clear();

    nlohmann::json serialize() const;

    void deserialize(const nlohmann::json &j);

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
    std::unordered_map<int, size_t> node_id_to_index_;
    std::unordered_map<int, size_t> port_id_to_index_;
    std::unordered_map<int, size_t> connection_id_to_index_;

    std::unordered_multimap<int, int> connectionsByPort; // Maps port_id to connection_id for quick access

    int next_node_id = 0;
    int next_port_id = 0;
    int next_connection_id = 0;

    // Helper methods to maintain hash maps
    void addNodeToIndex(int id, size_t index) { node_id_to_index_[id] = index; }
    void addPortToIndex(int id, size_t index) { port_id_to_index_[id] = index; }
    void addConnectionToIndex(int id, size_t index) { connection_id_to_index_[id] = index; }
    void removeNodeFromIndex(int id) { node_id_to_index_.erase(id); }
    void removePortFromIndex(int id) { port_id_to_index_.erase(id); }
    void removeConnectionFromIndex(int id) { connection_id_to_index_.erase(id); }
};

#endif //FACTORYGRAPH_H