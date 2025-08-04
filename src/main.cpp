#include <iostream>

#include "core/FactoryGraph.h"
#include "utils/JsonLoader.h"
#include "utils/LpSolver.h"

int main(int argc, char** argv) {
    std::cout << "Program started successfully!" << std::endl;
    FactoryGraph graph;
    graph.loadGameData("../data/satisfactory.json");

    graph.addNode("Miner", NodeType::PRODUCER, 0, 0); // todo: add ore extractions to recipes
    graph.addNode("Assembler", NodeType::PROCESSOR, 2, 0);
    graph.addNode("Assembler", NodeType::PROCESSOR, 2, 12);

    graph.addConnection(1,2);
    graph.addConnection(3,4);

    graph.setPortDemand(4, 60.0); // Set demand for port 4

    graph.printGraph();
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;

    LPSolver::RunAllExamples();
    return 0;
}
