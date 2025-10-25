#include <gtest/gtest.h>
#include "core/graph/FactorySolver.h"
#include "core/graph/FactoryGraph.h"
#include "core/data/GameData.h"

class FactorySolverTest : public ::testing::Test {
protected:
    GameData testGameData;
    std::unique_ptr<FactoryGraph> graph;
    std::unique_ptr<FactorySolver> solver;

    void SetUp() override {
        // --- 1. Set up GameData (same as FactoryGraphTest) ---
        testGameData.gameName = "TestGame";
        testGameData.time_unit = "seconds";

        testGameData.resources["nothing"] = Resource{"Nothing", 0};
        testGameData.resources["iron_ore"] = Resource{"Iron Ore", 1};
        testGameData.resources["iron_ingot"] = Resource{"Iron Ingot", 2};
        testGameData.resources["iron_plate"] = Resource{"Iron Plate", 3};
        testGameData.resources["screw"] = Resource{"Screw", 4};
        testGameData.resources["reinforced_plate"] = Resource{"Reinforced Plate", 5};
        testGameData.resources["copper_ore"] = Resource{"Copper Ore", 6};
        testGameData.resources["copper_ingot"] = Resource{"Copper Ingot", 7};
        testGameData.resources["wire"] = Resource{"Wire", 8};

        testGameData.machines["miner"] = Machine{"Miner", 1, 0};
        testGameData.machines["smelter"] = Machine{"Smelter", 1, 1};
        testGameData.machines["constructor"] = Machine{"Constructor", 1, 2};
        testGameData.machines["assembler"] = Machine{"Assembler", 1, 3};
        testGameData.machines["sink"] = Machine{"AWESOME Sink", 1, 4};

        Recipe mine_ore;
        mine_ore.name = "Mine Iron Ore";
        mine_ore.produced_in_machines_keys.push_back("miner");
        mine_ore.input_ports.push_back(RecipePort{1.0, "nothing"});
        mine_ore.output_ports.push_back(RecipePort{30.0, "iron_ore"});
        mine_ore.time_seconds = 1.0; // Added for machine count
        testGameData.recipes["mine_ore"] = mine_ore;

        Recipe smelt_ingot;
        smelt_ingot.name = "Smelt Iron Ingot";
        smelt_ingot.produced_in_machines_keys.push_back("smelter");
        smelt_ingot.input_ports.push_back(RecipePort{30.0, "iron_ore"});
        smelt_ingot.output_ports.push_back(RecipePort{30.0, "iron_ingot"});
        smelt_ingot.time_seconds = 1.0; // Added for machine count
        testGameData.recipes["smelt_ingot"] = smelt_ingot;

        Recipe sink_ingot;
        sink_ingot.name = "Sink Iron Ingot";
        sink_ingot.produced_in_machines_keys.push_back("sink");
        sink_ingot.input_ports.push_back(RecipePort{1.0, "iron_ingot"});
        sink_ingot.output_ports.push_back(RecipePort{1.0, "nothing"});
        sink_ingot.time_seconds = 1.0; // Added for machine count
        testGameData.recipes["sink_ingot"] = sink_ingot;

        Recipe make_plates;
        make_plates.name = "Make Iron Plates";
        make_plates.produced_in_machines_keys.push_back("constructor");
        make_plates.input_ports.push_back(RecipePort{30.0, "iron_ingot"});
        make_plates.output_ports.push_back(RecipePort{20.0, "iron_plate"}); // 3:2 ratio
        make_plates.time_seconds = 1.0;
        testGameData.recipes["make_plates"] = make_plates;

        Recipe make_screws;
        make_screws.name = "Make Screws";
        make_screws.produced_in_machines_keys.push_back("constructor");
        make_screws.input_ports.push_back(RecipePort{10.0, "iron_ingot"});
        make_screws.output_ports.push_back(RecipePort{40.0, "screw"}); // 1:4 ratio
        make_screws.time_seconds = 1.0;
        testGameData.recipes["make_screws"] = make_screws;

        Recipe make_reinforced;
        make_reinforced.name = "Make Reinforced Plates";
        make_reinforced.produced_in_machines_keys.push_back("assembler");
        make_reinforced.input_ports.push_back(RecipePort{30.0, "iron_plate"});
        make_reinforced.input_ports.push_back(RecipePort{60.0, "screw"});
        make_reinforced.output_ports.push_back(RecipePort{5.0, "reinforced_plate"});
        make_reinforced.time_seconds = 1.0;
        testGameData.recipes["make_reinforced"] = make_reinforced;

        // --- 2. Initialize Graph and Solver ---
        graph = std::make_unique<FactoryGraph>(testGameData);
        solver = std::make_unique<FactorySolver>(); // Uses default solver
    }

    void TearDown() override {
        solver.reset();
        graph.reset();
    }

    // Helper to add a node
    uint64_t addNode(const std::string& recipeKey) {
        std::string nodeName = testGameData.recipes.at(recipeKey).name;
        uint64_t nodeId = graph->addNode(nodeName, recipeKey);
        return nodeId;
    }

    // Helper to get a port
    Port* getPort(uint64_t nodeId, const std::string& resourceKey, bool isInput) {
        Node* node = graph->getNode(nodeId);
        if (!node) return nullptr;
        const auto& port_ids = isInput ? node->input_ports : node->output_ports;
        for (uint64_t portId : port_ids) {
            Port* port = graph->getPort(portId);
            if (port && port->resource_key == resourceKey) {
                return port;
            }
        }
        return nullptr;
    }

    // Helper to add a connection
    uint64_t addConnection(Port* from, Port* to) {
        if (!from || !to) return -1;
        return graph->addConnection(from->id, to->id);
    }
};

TEST_F(FactorySolverTest, EmptyGraph) {
    FactorySolver::SolverResult result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
}

TEST_F(FactorySolverTest, SimpleChain) {
    uint64_t n_miner = addNode("mine_ore"); // 30 ore/s
    uint64_t n_smelter = addNode("smelt_ingot"); // 30 ore/s -> 30 ingot/s

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);

    addConnection(p_miner_out, p_smelter_in);

    graph->setPortConstraint(p_smelter_out->id, 60.0);
    FactorySolver::SolverResult result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_smelter_out->rate, 60.0, 0.001);
    EXPECT_NEAR(p_smelter_in->rate, 60.0, 0.001);
    EXPECT_NEAR(p_miner_out->rate, 60.0, 0.001);

    EXPECT_NEAR(graph->getNode(n_miner)->machine_count, 2.0, 0.001);
    EXPECT_NEAR(graph->getNode(n_smelter)->machine_count, 2.0, 0.001);
}

TEST_F(FactorySolverTest, SimpleSplit) {
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter1 = addNode("smelt_ingot");
    uint64_t n_smelter2 = addNode("smelt_ingot");

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter1_in = getPort(n_smelter1, "iron_ore", true);
    Port* p_smelter2_in = getPort(n_smelter2, "iron_ore", true);
    Port* p_smelter1_out = getPort(n_smelter1, "iron_ingot", false);
    Port* p_smelter2_out = getPort(n_smelter2, "iron_ingot", false);

    addConnection(p_miner_out, p_smelter1_in);
    addConnection(p_miner_out, p_smelter2_in);

    graph->setPortConstraint(p_smelter1_out->id, 30.0);
    graph->setPortConstraint(p_smelter2_out->id, 60.0);

    FactorySolver::SolverResult result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_smelter1_out->rate, 30.0, 0.001);
    EXPECT_NEAR(p_smelter1_in->rate, 30.0, 0.001);
    EXPECT_NEAR(p_smelter2_out->rate, 60.0, 0.001);
    EXPECT_NEAR(p_smelter2_in->rate, 60.0, 0.001);
    EXPECT_NEAR(p_miner_out->rate, 90.0, 0.001);

    EXPECT_NEAR(graph->getNode(n_miner)->machine_count, 3.0, 0.001);
    EXPECT_NEAR(graph->getNode(n_smelter1)->machine_count, 1.0, 0.001);
    EXPECT_NEAR(graph->getNode(n_smelter2)->machine_count, 2.0, 0.001);
}

TEST_F(FactorySolverTest, SimpleMerge) {
    uint64_t n_miner1 = addNode("mine_ore");
    uint64_t n_miner2 = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");

    Port* p_miner1_out = getPort(n_miner1, "iron_ore", false);
    Port* p_miner2_out = getPort(n_miner2, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);

    addConnection(p_miner1_out, p_smelter_in);
    addConnection(p_miner2_out, p_smelter_in);

    graph->setPortConstraint(p_smelter_out->id, 60.0);

    FactorySolver::SolverResult result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_smelter_out->rate, 60.0, 0.001);
    EXPECT_NEAR(p_smelter_in->rate, 60.0, 0.001);

    // The solver doesn't guarantee an even split, just that the sum is correct.
    EXPECT_NEAR(p_miner1_out->rate + p_miner2_out->rate, 60.0, 0.001);

    EXPECT_NEAR(graph->getNode(n_smelter)->machine_count, 2.0, 0.001);
    EXPECT_NEAR(graph->getNode(n_miner1)->machine_count + graph->getNode(n_miner2)->machine_count, 2.0, 0.001);
}

TEST_F(FactorySolverTest, MultiInputRecipe) {
    uint64_t n_smelter = addNode("smelt_ingot");
    uint64_t n_screws = addNode("make_screws");
    uint64_t n_plates = addNode("make_plates");
    uint64_t n_rfp = addNode("make_reinforced");

    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);
    Port* p_screws_in = getPort(n_screws, "iron_ingot", true);
    Port* p_plates_in = getPort(n_plates, "iron_ingot", true);
    Port* p_screws_out = getPort(n_screws, "screw", false);
    Port* p_plates_out = getPort(n_plates, "iron_plate", false);
    Port* p_rfp_in_screws = getPort(n_rfp, "screw", true);
    Port* p_rfp_in_plates = getPort(n_rfp, "iron_plate", true);
    Port* p_rfp_out = getPort(n_rfp, "reinforced_plate", false);

    addConnection(p_smelter_out, p_screws_in);
    addConnection(p_smelter_out, p_plates_in);
    addConnection(p_screws_out, p_rfp_in_screws);
    addConnection(p_plates_out, p_rfp_in_plates);

    graph->setPortConstraint(p_rfp_out->id, 10.0); // Want 10 RFP/s

    FactorySolver::SolverResult result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    // RFP (5/s base): 10 / 5 = 2 machines
    // Inputs: 30 plate/s, 60 screw/s (base)
    EXPECT_NEAR(p_rfp_out->rate, 10.0, 0.001);
    EXPECT_NEAR(graph->getNode(n_rfp)->machine_count, 2.0, 0.001);
    EXPECT_NEAR(p_rfp_in_plates->rate, 60.0, 0.001); // 30 * 2
    EXPECT_NEAR(p_rfp_in_screws->rate, 120.0, 0.001); // 60 * 2

    // Plates (20/s base): 60 / 20 = 3 machines
    // Inputs: 30 ingot/s (base)
    EXPECT_NEAR(p_plates_out->rate, 60.0, 0.001);
    EXPECT_NEAR(graph->getNode(n_plates)->machine_count, 3.0, 0.001);
    EXPECT_NEAR(p_plates_in->rate, 90.0, 0.001); // 30 * 3

    // Screws (40/s base): 120 / 40 = 3 machines
    // Inputs: 10 ingot/s (base)
    EXPECT_NEAR(p_screws_out->rate, 120.0, 0.001);
    EXPECT_NEAR(graph->getNode(n_screws)->machine_count, 3.0, 0.001);
    EXPECT_NEAR(p_screws_in->rate, 30.0, 0.001); // 10 * 3

    // Smelter (30/s base): (90 + 30) / 30 = 4 machines
    EXPECT_NEAR(p_smelter_out->rate, 120.0, 0.001); // 90 + 30
    EXPECT_NEAR(graph->getNode(n_smelter)->machine_count, 4.0, 0.001);
}

TEST_F(FactorySolverTest, ChainWithDifferentRatios) {
    uint64_t n_smelter = addNode("smelt_ingot");
    uint64_t n_plates = addNode("make_plates");

    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);
    Port* p_plates_in = getPort(n_plates, "iron_ingot", true);
    Port* p_plates_out = getPort(n_plates, "iron_plate", false);

    addConnection(p_smelter_out, p_plates_in);

    // Want 20 plates/s. Recipe is 30 ingots -> 20 plates (3:2)
    graph->setPortConstraint(p_plates_out->id, 20.0);

    FactorySolver::SolverResult result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    // Plates
    EXPECT_NEAR(p_plates_out->rate, 20.0, 0.001);
    EXPECT_NEAR(p_plates_in->rate, 30.0, 0.001); // Should be 30
    EXPECT_NEAR(graph->getNode(n_plates)->machine_count, 1.0, 0.001);

    // Smelter
    EXPECT_NEAR(p_smelter_out->rate, 30.0, 0.001);
    EXPECT_NEAR(graph->getNode(n_smelter)->machine_count, 1.0, 0.001);
}

TEST_F(FactorySolverTest, InfeasibleSolution) {
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);

    addConnection(p_miner_out, p_smelter_in);

    // Constrain producer: Miner can only output 30 ore/s (1 machine)
    graph->setPortConstraint(p_miner_out->id, 30.0);

    // Constrain consumer: Smelter *wants* 60 ingots/s
    graph->setPortConstraint(p_smelter_out->id, 60.0);

    FactorySolver::SolverResult result = solver->solve(*graph);

    // The solver will *succeed* by finding the *optimal* solution,
    // which is to run at the maximum possible (30).
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    // All rates should be capped at 30, not 60.
    EXPECT_NEAR(p_smelter_out->rate, 30.0, 0.001);
    EXPECT_NEAR(p_smelter_in->rate, 30.0, 0.001);
    EXPECT_NEAR(p_miner_out->rate, 30.0, 0.001);

    EXPECT_NEAR(graph->getNode(n_miner)->machine_count, 1.0, 0.001);
    EXPECT_NEAR(graph->getNode(n_smelter)->machine_count, 1.0, 0.001);
}

TEST_F(FactorySolverTest, UnconstrainedGraph) {
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    addConnection(
        getPort(n_miner, "iron_ore", false),
        getPort(n_smelter, "iron_ore", true)
    );

    // No constraints are set.
    FactorySolver::SolverResult result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    // Without constraints, the objective is to maximize leaf nodes.
    // The leaf node (smelter output) is maximized, but since there's
    // no upper bound, it could be anything.
    // The current solver implementation will default to 0.
    EXPECT_NEAR(getPort(n_smelter, "iron_ingot", false)->rate, 0.0, 0.001);
}