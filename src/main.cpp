#include <iostream>

#include "core/FactoryGraph.h"
#include "utils/JsonLoader.h"
int main(int, char **) {
    std::cout << "Program started successfully!" << std::endl;
    FactoryGraph graph;
    FactoryGraph::loadGameData("../data/satisfactory.json");
    return 0;
}
