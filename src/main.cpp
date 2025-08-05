#include <iostream>

#include "core/FactoryGraph.h"
#include "core/FactorySolver.h"
#include "utils/JsonLoader.h"

int main(int argc, char** argv) {
    double exp = 10.0; //stopped on 17
    std::cout << "Program started successfully!" << std::endl;
    for (int i = 0; i < 100; i++) {
        FactoryGraph graph("../data/satisfactory.json");
    FactorySolver solver;
    //
    // graph.addNode("Miner", NodeType::PRODUCER,  graph.getGameData().getIdByRecipeName("Iron Ore"));
    // graph.addNode("Ingot", NodeType::PROCESSOR, graph.getGameData().getIdByRecipeName("Iron Ingot"));
    // graph.addNode("Plate", NodeType::PROCESSOR,  graph.getGameData().getIdByRecipeName("Iron Plate"));
    // graph.addNode("Rod", NodeType::PROCESSOR,  graph.getGameData().getIdByRecipeName("Iron Rod"));
    // graph.addNode("Screw", NodeType::PROCESSOR, graph.getGameData().getIdByRecipeName("Screw"));
    // graph.addNode("Screw", NodeType::PROCESSOR, graph.getGameData().getIdByRecipeName("Screw"));
    // graph.addNode("RIP", NodeType::PROCESSOR, graph.getGameData().getIdByRecipeName("Reinforced Iron Plate"));
    //
    // graph.addConnection(1,2);
    // graph.addConnection(3,4);
    // graph.addConnection(3,6);
    // graph.addConnection(7,8);
    // graph.addConnection(5,12);
    // graph.addConnection(9,13);
    // graph.addConnection(7,10);
    // graph.addConnection(11,13);
    //
    // graph.setPortDemand(14, 10.0);
    // graph.setPortDemand(0, 120.0);
    // graph.setPortDemand(7, 30.0);
    // graph.setPortDemand(9, 30.0);
    // graph.setPortDemand(11, 30.0);
    //
    // std::cout << graph.getGameData().time_unit << std::endl;
    // std::cout << graph.getGameData().time_unit << std::endl;
    // std::cout << graph.getGameData().time_unit << std::endl;
    // std::cout << graph.getGameData().time_unit << std::endl;
    // std::cout << graph.getGameData().time_unit << std::endl;
    // std::cout << graph.getGameData().time_unit << std::endl;
    //
    // for (auto machine : graph.getGameData().machines) {
    //     std::cout << "Machine: " << machine.name << ", ID: " << machine.id
    //               << ", Base Power Usage: " << machine.base_power_usage
    //               << ", Max Somersloop Slots: " << machine.max_somersloop_slots << std::endl;
    // }

    // stress-test with a lot of simple nodes
    for (int i = 0; i < 0; ++i) {
        // add not connected Nodes (another production in same factory that is not connected to the main production line)
        int copper_miner = graph.addNode("Copper Ore", NodeType::PRODUCER,  graph.getGameData().getIdByRecipeName("Copper Ore"));
        int cop_ingot = graph.addNode("Copper Ingot", NodeType::PROCESSOR,  graph.getGameData().getIdByRecipeName("Copper Ingot"));
        int cop_wire = graph.addNode("Copper Wire", NodeType::PROCESSOR,  graph.getGameData().getIdByRecipeName("Wire"));

        graph.addConnection(graph.getNode(copper_miner)->output_ports[0], graph.getNode(cop_ingot)->input_ports[0]);
        graph.addConnection(graph.getNode(cop_ingot)->output_ports[0], graph.getNode(cop_wire)->input_ports[0]);

        graph.setPortDemand(graph.getNode(cop_wire)->output_ports[0], 30.0);
    }

    // stress-test with a lot of complex nodes
    for (int i = 0; i < pow(2,exp); ++i) {
        int quartz_miner = graph.addNode("Quartz Ore", NodeType::PRODUCER,  graph.getGameData().getIdByRecipeName("Raw Quartz"));
        int caterium_miner = graph.addNode("Caterium Ore", NodeType::PRODUCER,  graph.getGameData().getIdByRecipeName("Caterium Ore"));
        int oil_extractor = graph.addNode("Oil Extractor", NodeType::PRODUCER,  graph.getGameData().getIdByRecipeName("Crude Oil"));

        int quartz_crystal = graph.addNode("Quartz Crystal", NodeType::PROCESSOR,  graph.getGameData().getIdByRecipeName("Quartz Crystal"));
        int caterium_ingot = graph.addNode("Caterium Ingot", NodeType::PROCESSOR,  graph.getGameData().getIdByRecipeName("Caterium Ingot"));
        int plastic = graph.addNode("Plastic", NodeType::PROCESSOR,  graph.getGameData().getIdByRecipeName("Plastic"));
        int rubber = graph.addNode("Rubber", NodeType::PROCESSOR,  graph.getGameData().getIdByRecipeName("Rubber"));
        int fuel = graph.addNode("Fuel", NodeType::PROCESSOR,  graph.getGameData().getIdByRecipeName("Residual Fuel"));
        int quickwire = graph.addNode("Quickwire", NodeType::PROCESSOR, graph.getGameData().getIdByRecipeName("Quickwire"));
        int ai_limiter = graph.addNode("AI Limiter", NodeType::PROCESSOR, graph.getGameData().getIdByRecipeName("Alternate: Plastic AI Limiter"));
        int crystal_oscillator = graph.addNode("Crystal Oscillator", NodeType::PROCESSOR, graph.getGameData().getIdByRecipeName("Alternate: Insulated Crystal Oscillator"));

        graph.addConnection(graph.getNode(quartz_miner)->output_ports[0], graph.getNode(quartz_crystal)->input_ports[0]);
        graph.addConnection(graph.getNode(caterium_miner)->output_ports[0], graph.getNode(caterium_ingot)->input_ports[0]);
        graph.addConnection(graph.getNode(oil_extractor)->output_ports[0], graph.getNode(plastic)->input_ports[0]);
        graph.addConnection(graph.getNode(oil_extractor)->output_ports[0], graph.getNode(rubber)->input_ports[0]);
        graph.addConnection(graph.getNode(rubber)->output_ports[1], graph.getNode(fuel)->input_ports[0]);
        graph.addConnection(graph.getNode(plastic)->output_ports[1], graph.getNode(fuel)->input_ports[0]);
        graph.addConnection(graph.getNode(caterium_ingot)->output_ports[0], graph.getNode(quickwire)->input_ports[0]);
        graph.addConnection(graph.getNode(plastic)->output_ports[0], graph.getNode(ai_limiter)->input_ports[1]);
        graph.addConnection(graph.getNode(quickwire)->output_ports[0], graph.getNode(ai_limiter)->input_ports[0]);
        graph.addConnection(graph.getNode(quartz_crystal)->output_ports[0], graph.getNode(crystal_oscillator)->input_ports[0]);
        graph.addConnection(graph.getNode(rubber)->output_ports[0], graph.getNode(crystal_oscillator)->input_ports[1]);
        graph.addConnection(graph.getNode(ai_limiter)->output_ports[0], graph.getNode(crystal_oscillator)->input_ports[2]);

        graph.setPortDemand(graph.getNode(crystal_oscillator)->output_ports[0], 45.0);
        graph.setPortDemand(graph.getNode(oil_extractor)->output_ports[0], 1200);
        graph.setPortDemand(graph.getNode(caterium_miner)->output_ports[0], 780);
    }


    solver.solve(graph);
        graph.clear();
}

    //graph.printGraph()

    //LPSolver::RunAllExamples();
    return 0;
}
