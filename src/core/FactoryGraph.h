//
// Created by hagoel on 8/1/25.
//

#ifndef FACTORYGRAPH_H
#define FACTORYGRAPH_H
#include <vector>

#include "Connection.h"
#include "Node.h"
#include "Port.h"
#include "../utils/GameDataLoader.h"

class FactoryGraph {
public:
    int addNode(const std::string& name, NodeType type, int key_id, int recipe_id);
    bool setNodeRecipe(int node_id, int recipe_id);
    Node* getNode(int id);
    [[nodiscard]] const std::vector<Node>& getNodes() const;

    bool isValidConnection(int from_port, int to_port);
    bool addConnection(int from_port, int to_port);
    [[nodiscard]] const std::vector<Connection>& getConnections() const;

    int addPort(int resource_id);
    Port* getPort(int id);
    [[nodiscard]] const std::vector<Port>& getPorts() const;
    bool setPortDemand(int port_id, double demand);

    void clear();
    void printGraph();

    bool loadGameData(const std::string& jsonFile);
    GameDataLoader::GameData getGameData() const {
        return game_data;
    }
private:
    GameDataLoader::GameData game_data;

    std::vector<Node> nodes; // List of nodes in the graph
    std::vector<Port> ports; // List of ports
    std::vector<Connection> connections; // List of connections between ports

    int next_node_id = 0; // Unique ID for the next node to be added
    int next_port_id = 0;
};
#endif //FACTORYGRAPH_H
