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

    graph.addConnection(1,2);
    graph.addConnection(3,4);
    graph.addConnection(3,6);
    graph.addConnection(7,8);
    graph.addConnection(5,12);
    graph.addConnection(9,13);
    graph.addConnection(7,10);
    graph.addConnection(11,13);

    graph.setPortDemand(14, 10.0);
    graph.setPortDemand(0, 120.0);
    graph.setPortDemand(7, 30.0);
    graph.setPortDemand(9, 30.0);
    graph.setPortDemand(11, 30.0);

    solver.solve(graph);

    //graph.printGraph();
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;

    //LPSolver::RunAllExamples();
    return 0;
}
