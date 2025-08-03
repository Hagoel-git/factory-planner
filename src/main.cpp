#include <iostream>

#include "core/FactoryGraph.h"
#include "utils/JsonLoader.h"
int main(int, char **) {
    std::cout << "Program started successfully!" << std::endl;
    FactoryGraph graph;
    graph.loadGameData("../data/satisfactory.json");

    graph.addNode("Miner", NodeType::PRODUCER, 0, 0); // todo: add ore extractions to recipes
    graph.addNode("Assembler", NodeType::PROCESSOR, 2, 0);
    graph.addNode("Assembler", NodeType::PROCESSOR, 2, 12);

    graph.addConnection(0,1,1,1,1);
    graph.addConnection(1,2,1,1,5);

    graph.printGraph();
    std::cout << std::endl << "Final nodes: ";
    auto final_nodes = graph.findFinalNodes();
    for (int node_id : final_nodes) {
        Node* node = graph.getNode(node_id);
        if (node) {
            std::cout << node->name << " (ID: " << node->id << ") ";
        } else {
            std::cout << "Invalid node ID: " << node_id << " ";
        }
    }
    std::cout << std::endl;
    return 0;
}
