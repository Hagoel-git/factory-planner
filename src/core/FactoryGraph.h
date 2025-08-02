//
// Created by hagoel on 8/1/25.
//

#ifndef FACTORYGRAPH_H
#define FACTORYGRAPH_H
#include <vector>

#include "Connection.h"
#include "Node.h"

class FactoryGraph {
public:
    int addNode(const std::string& name, NodeType type, int key_id);
    Node* getNode(int id);
    [[nodiscard]] const std::vector<Node>& getNodes() const;

    void addConnection(int from_node_id, int to_node_id, int resource_id, double max_flow);
    [[nodiscard]] const std::vector<Connection>& getConnections() const;

    void clear();
    void printGraph() const;


private:
    std::vector<Node> nodes; // List of nodes in the graph
    std::vector<Connection> connections; // List of connections between nodes

    int next_node_id = 0; // Unique ID for the next node to be added
};
#endif //FACTORYGRAPH_H
