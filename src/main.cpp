#include <iostream>

#include "core/FactoryGraph.h"
#include "core/FactorySolver.h"
#include "utils/JsonLoader.h"
#include "utils/LpSolver.h"

int main(int argc, char** argv) {
    std::cout << "Program started successfully!" << std::endl;
    FactoryGraph graph;
    FactorySolver solver;
    graph.loadGameData("../data/satisfactory.json");

    graph.addNode("Miner", NodeType::PRODUCER, 0, graph.getGameData().getIdByRecipeName("Iron Ore"));
    graph.addNode("Assembler", NodeType::PROCESSOR, 2, graph.getGameData().getIdByRecipeName("Iron Ingot"));
    graph.addNode("Assembler", NodeType::PROCESSOR, 2, graph.getGameData().getIdByRecipeName("Iron Plate"));

    graph.addConnection(0,1);
    graph.addConnection(2,3);

    graph.setPortDemand(4, 240.0); // Set demand for port 4

    solver.solve(graph);

    graph.printGraph();
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;

    LPSolver::RunAllExamples();
    return 0;
}
