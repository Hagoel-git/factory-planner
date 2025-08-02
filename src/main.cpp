#include <iostream>

#include "core/FactoryGraph.h"
#include "utils/JsonLoader.h"
int main(int, char **) {
    std::cout << "Program started successfully!" << std::endl;
    FactoryGraph graph;

    graph.addNode("iron_mine", NodeType::PRODUCER, 1);
    graph.addNode("smelter", NodeType::PROCESSOR, 2);
    graph.addConnection(0,1, 1, 100.0);
    return 0;
}
