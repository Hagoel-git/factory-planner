#include <iostream>

#include "core/FactoryGraph.h"
#include "utils/JsonLoader.h"
#include "utils/LpSolver.h"

int main(int argc, char** argv) {
    std::cout << "Program started successfully!" << std::endl;
    FactoryGraph graph;
    graph.loadGameData("../data/satisfactory.json");

    graph.addNode("Miner", NodeType::PRODUCER, 0, graph.getGameData().getIdByRecipeName("Iron Ore"));
    graph.addNode("Assembler", NodeType::PROCESSOR, 2, graph.getGameData().getIdByRecipeName("Iron Ingot"));
    graph.addNode("Assembler", NodeType::PROCESSOR, 2, graph.getGameData().getIdByRecipeName("Iron Plate"));

    graph.addConnection(0,1);
    graph.addConnection(2,3);

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
