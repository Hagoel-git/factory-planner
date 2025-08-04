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
    graph.addNode("Ingot", NodeType::PROCESSOR, 2, graph.getGameData().getIdByRecipeName("Iron Ingot"));
    graph.addNode("Plate", NodeType::PROCESSOR, 2, graph.getGameData().getIdByRecipeName("Iron Plate"));
    graph.addNode("Rod", NodeType::PROCESSOR, 2, graph.getGameData().getIdByRecipeName("Iron Rod"));
    graph.addNode("Screw", NodeType::PROCESSOR, 2, graph.getGameData().getIdByRecipeName("Screw"));
    graph.addNode("Screw", NodeType::PROCESSOR, 2, graph.getGameData().getIdByRecipeName("Screw"));
    graph.addNode("RIP", NodeType::PROCESSOR, 3, graph.getGameData().getIdByRecipeName("Reinforced Iron Plate"));

    graph.addConnection(0,1);
    graph.addConnection(2,3);
    graph.addConnection(2,5);
    graph.addConnection(6,7);
    graph.addConnection(4,11);
    graph.addConnection(8,12);
    graph.addConnection(6,9);
    graph.addConnection(10,12);

    //graph.setPortDemand(4, 240.0); // Set demand for port 4
    graph.setPortDemand(0, 120.0); // Set demand for port 5

    solver.solve(graph);

    graph.printGraph();
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;

    //LPSolver::RunAllExamples();
    return 0;
}
