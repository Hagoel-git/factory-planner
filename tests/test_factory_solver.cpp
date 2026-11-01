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

TEST_F(FactorySolverTest, RobustnessInvalidRecipeOrMachine) {
    uint64_t n1 = addNode("mine_ore");
    Node* node = graph->getNode(n1);
    ASSERT_NE(node, nullptr);

    // Corrupt recipe key
    node->selected_recipe_key = "non_existent_recipe";
    FactorySolver::SolverResult result = solver->solve(*graph);
    EXPECT_NEAR(node->machine_count, 0.0, 0.001);

    // Restore recipe, corrupt machine key
    node->selected_recipe_key = "mine_ore";
    node->machine_key = "non_existent_machine";
    result = solver->solve(*graph);
    EXPECT_NEAR(node->machine_count, 0.0, 0.001);
}

TEST_F(FactorySolverTest, RobustnessZeroOrNegativeClockSpeed) {
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    addConnection(getPort(n_miner, "iron_ore", false), getPort(n_smelter, "iron_ore", true));

    // Zero clock speed
    graph->setNodeClockSpeed(n_smelter, 0.0);
    graph->setPortConstraint(getPort(n_miner, "iron_ore", false)->id, 30.0);

    FactorySolver::SolverResult result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(graph->getNode(n_smelter)->machine_count, 0.0, 0.001);

    // Negative clock speed
    graph->setNodeClockSpeed(n_smelter, -50.0);
    result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(graph->getNode(n_smelter)->machine_count, 0.0, 0.001);
}

TEST_F(FactorySolverTest, RobustnessZeroRecipeTime) {
    Recipe zero_time_recipe;
    zero_time_recipe.name = "Zero Time Recipe";
    zero_time_recipe.produced_in_machines_keys.push_back("smelter");
    zero_time_recipe.input_ports.push_back(RecipePort{30.0, "iron_ore"});
    zero_time_recipe.output_ports.push_back(RecipePort{30.0, "iron_ingot"});
    zero_time_recipe.time_seconds = 0.0;
    testGameData.recipes["zero_time"] = zero_time_recipe;

    graph = std::make_unique<FactoryGraph>(testGameData);

    uint64_t n_node = graph->addNode("Zero Time Node", "zero_time");
    FactorySolver::SolverResult result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(graph->getNode(n_node)->machine_count, 0.0, 0.001);
}

TEST_F(FactorySolverTest, RobustnessNodeFewerPortsThanRecipe) {
    uint64_t n_rfp = addNode("make_reinforced");
    Node* node = graph->getNode(n_rfp);
    ASSERT_NE(node, nullptr);

    // Case A: 0 input and 0 output ports
    node->input_ports.clear();
    node->output_ports.clear();
    FactorySolver::SolverResult result = solver->solve(*graph);
    EXPECT_NEAR(node->machine_count, 0.0, 0.001);

    // Case B: 1 input port (recipe has 2 input ports) and 0 output ports
    node->input_ports.push_back(999999); // non-existent port variable ID
    result = solver->solve(*graph);
    EXPECT_NEAR(node->machine_count, 0.0, 0.001);
}

TEST_F(FactorySolverTest, ZeroProductionMultiplierOrClockSpeed_ClampsToZeroAndNoConsumption) {
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);

    addConnection(p_miner_out, p_smelter_in);

    // Limit miner to 30
    graph->setPortConstraint(p_miner_out->id, 30.0);
    // Set smelter production_multiplier to 0
    graph->setProductionMultiplier(n_smelter, 0.0);

    FactorySolver::SolverResult result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_smelter_in->rate, 0.0, 0.001);
    EXPECT_NEAR(p_smelter_out->rate, 0.0, 0.001);
    EXPECT_NEAR(graph->getNode(n_smelter)->machine_count, 0.0, 0.001);

    // Now test clock speed 0
    graph->setProductionMultiplier(n_smelter, 100.0);
    graph->setNodeClockSpeed(n_smelter, 0.0);

    result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_smelter_in->rate, 0.0, 0.001);
    EXPECT_NEAR(p_smelter_out->rate, 0.0, 0.001);
    EXPECT_NEAR(graph->getNode(n_smelter)->machine_count, 0.0, 0.001);
    // Test negative production multiplier
    graph->setProductionMultiplier(n_smelter, -50.0);
    graph->setNodeClockSpeed(n_smelter, 100.0);
    result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_smelter_in->rate, 0.0, 0.001);
    EXPECT_NEAR(p_smelter_out->rate, 0.0, 0.001);
    EXPECT_NEAR(graph->getNode(n_smelter)->machine_count, 0.0, 0.001);

    // Test negative clock speed
    graph->setProductionMultiplier(n_smelter, 100.0);
    graph->setNodeClockSpeed(n_smelter, -25.0);
    result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_smelter_in->rate, 0.0, 0.001);
    EXPECT_NEAR(p_smelter_out->rate, 0.0, 0.001);
    EXPECT_NEAR(graph->getNode(n_smelter)->machine_count, 0.0, 0.001);
}

TEST_F(FactorySolverTest, Stage3WasteMinimization_PrefersDownstreamLinkOverExcess) {
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    uint64_t n_sink = addNode("sink_ingot");

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);
    Port* p_sink_in = getPort(n_sink, "iron_ingot", true);

    addConnection(p_miner_out, p_smelter_in);
    addConnection(p_smelter_out, p_sink_in);

    // Capacity constraint on miner output of 30.0
    graph->setPortConstraint(p_miner_out->id, 30.0);

    FactorySolver::SolverResult result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_miner_out->rate, 30.0, 0.001);
    EXPECT_NEAR(p_miner_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_smelter_in->rate, 30.0, 0.001);
    EXPECT_NEAR(p_smelter_out->rate, 30.0, 0.001);
    EXPECT_NEAR(p_sink_in->rate, 30.0, 0.001);
}

TEST_F(FactorySolverTest, Stage3WasteMinimization_ByproductRoutingPrefersDownstreamLinkOverExcess) {
    Recipe split_recipe;
    split_recipe.name = "Split Production";
    split_recipe.produced_in_machines_keys.push_back("constructor");
    split_recipe.input_ports.push_back(RecipePort{30.0, "iron_ingot"});
    split_recipe.output_ports.push_back(RecipePort{20.0, "iron_plate"}); // output 0: main product
    split_recipe.output_ports.push_back(RecipePort{10.0, "screw"});      // output 1: byproduct
    split_recipe.time_seconds = 1.0;

    Recipe sink_screw;
    sink_screw.name = "Sink Screws";
    sink_screw.produced_in_machines_keys.push_back("sink");
    sink_screw.input_ports.push_back(RecipePort{1.0, "screw"});
    sink_screw.output_ports.push_back(RecipePort{1.0, "nothing"});
    sink_screw.time_seconds = 1.0;

    GameData gd = testGameData;
    gd.recipes["split_recipe"] = split_recipe;
    gd.recipes["sink_screw"] = sink_screw;

    FactoryGraph g(gd);
    FactorySolver s;

    uint64_t n_miner = g.addNode("Miner", "mine_ore");
    uint64_t n_smelter = g.addNode("Smelter", "smelt_ingot");
    uint64_t n_split = g.addNode("Splitter", "split_recipe");
    uint64_t n_sink = g.addNode("Sink", "sink_screw");

    auto getP = [&](uint64_t node_id, const std::string& res, bool is_in) -> Port* {
        const Node* node = g.getNode(node_id);
        const auto& p_ids = is_in ? node->input_ports : node->output_ports;
        for (uint64_t pid : p_ids) {
            Port* port = g.getPort(pid);
            if (port && port->resource_key == res) return port;
        }
        return nullptr;
    };

    Port* p_miner_out = getP(n_miner, "iron_ore", false);
    Port* p_smelter_in = getP(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getP(n_smelter, "iron_ingot", false);
    Port* p_split_in = getP(n_split, "iron_ingot", true);
    Port* p_split_plate = getP(n_split, "iron_plate", false);
    Port* p_split_screw = getP(n_split, "screw", false);
    Port* p_sink_in = getP(n_sink, "screw", true);

    ASSERT_NE(p_miner_out, nullptr);
    ASSERT_NE(p_smelter_in, nullptr);
    ASSERT_NE(p_smelter_out, nullptr);
    ASSERT_NE(p_split_in, nullptr);
    ASSERT_NE(p_split_plate, nullptr);
    ASSERT_NE(p_split_screw, nullptr);
    ASSERT_NE(p_sink_in, nullptr);

    g.addConnection(p_miner_out->id, p_smelter_in->id);
    g.addConnection(p_smelter_out->id, p_split_in->id);
    g.addConnection(p_split_screw->id, p_sink_in->id);

    // Target constraint on main product: 20.0
    g.setPortConstraint(p_split_plate->id, 20.0);

    FactorySolver::SolverResult result = s.solve(g);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_split_plate->rate, 20.0, 0.001);
    EXPECT_NEAR(p_split_screw->rate, 10.0, 0.001);
    // Crucial: byproduct should be routed downstream into sink, NOT left as unconsumed excess
    EXPECT_NEAR(p_split_screw->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_sink_in->rate, 10.0, 0.001);
}

TEST_F(FactorySolverTest, Phase1WaterFilling_CompetingTargetsSharingCapacityNotFalselyBottlenecked) {
    // Finding #16 reproduction test:
    // 3-target factory:
    // - Target 3: separate chain with 20 coal/s supply, 100 demand (bottlenecks globally at S = 0.20).
    // - Target 1: recipe 1 ore -> 1 item, target 100 items/s.
    // - Target 2: recipe 2 ore -> 1 item, target 100 items/s.
    // - Targets 1 and 2 share a common raw source producing 90 ore/s.
    // When solved, Target 3 must be locked at 20 items/s.
    // Under fair max-min water-filling, the remaining 30 surplus ore must be split proportionally
    // between Target 1 and Target 2, achieving S = 0.30 (Target 1 = 30.0 items/s, Target 2 = 30.0 items/s).

    Recipe r_coal_source;
    r_coal_source.name = "Mine Coal";
    r_coal_source.produced_in_machines_keys.push_back("miner");
    r_coal_source.input_ports.push_back(RecipePort{1.0, "nothing"});
    r_coal_source.output_ports.push_back(RecipePort{20.0, "coal"});
    r_coal_source.time_seconds = 1.0;

    Recipe r_coal_consumer;
    r_coal_consumer.name = "Process Coal";
    r_coal_consumer.produced_in_machines_keys.push_back("constructor");
    r_coal_consumer.input_ports.push_back(RecipePort{1.0, "coal"});
    r_coal_consumer.output_ports.push_back(RecipePort{1.0, "coal_item"});
    r_coal_consumer.time_seconds = 1.0;

    Recipe r_ore_source;
    r_ore_source.name = "Mine Raw Ore";
    r_ore_source.produced_in_machines_keys.push_back("miner");
    r_ore_source.input_ports.push_back(RecipePort{1.0, "nothing"});
    r_ore_source.output_ports.push_back(RecipePort{90.0, "ore"});
    r_ore_source.time_seconds = 1.0;

    Recipe r_target1;
    r_target1.name = "Make Item 1";
    r_target1.produced_in_machines_keys.push_back("constructor");
    r_target1.input_ports.push_back(RecipePort{1.0, "ore"});
    r_target1.output_ports.push_back(RecipePort{1.0, "item1"});
    r_target1.time_seconds = 1.0;

    Recipe r_target2;
    r_target2.name = "Make Item 2";
    r_target2.produced_in_machines_keys.push_back("constructor");
    r_target2.input_ports.push_back(RecipePort{2.0, "ore"});
    r_target2.output_ports.push_back(RecipePort{1.0, "item2"});
    r_target2.time_seconds = 1.0;

    GameData gd = testGameData;
    gd.recipes["mine_coal"] = r_coal_source;
    gd.recipes["process_coal"] = r_coal_consumer;
    gd.recipes["mine_ore_90"] = r_ore_source;
    gd.recipes["make_item1"] = r_target1;
    gd.recipes["make_item2"] = r_target2;

    FactoryGraph g(gd);
    FactorySolver s;

    // Target 3 chain
    uint64_t n_coal_mine = g.addNode("Mine Coal", "mine_coal");
    uint64_t n_coal_proc = g.addNode("Process Coal", "process_coal");

    // Targets 1 and 2 shared ore chain
    uint64_t n_ore_mine = g.addNode("Mine Ore", "mine_ore_90");
    uint64_t n_t1 = g.addNode("Make Item 1", "make_item1");
    uint64_t n_t2 = g.addNode("Make Item 2", "make_item2");

    auto getPortId = [&](uint64_t node_id, const std::string& res, bool is_in) -> uint64_t {
        const Node* node = g.getNode(node_id);
        if (!node) return 0;
        const auto& p_ids = is_in ? node->input_ports : node->output_ports;
        for (uint64_t pid : p_ids) {
            const Port* p = g.getPort(pid);
            if (p && p->resource_key == res) return pid;
        }
        return 0;
    };

    uint64_t p_coal_mine_out = getPortId(n_coal_mine, "coal", false);
    uint64_t p_coal_proc_in = getPortId(n_coal_proc, "coal", true);
    uint64_t p_coal_proc_out = getPortId(n_coal_proc, "coal_item", false);

    ASSERT_NE(p_coal_mine_out, 0);
    ASSERT_NE(p_coal_proc_in, 0);
    ASSERT_NE(p_coal_proc_out, 0);

    g.addConnection(p_coal_mine_out, p_coal_proc_in);
    g.setPortConstraint(p_coal_mine_out, 20.0); // 20 coal/s supply limit
    g.setPortConstraint(p_coal_proc_out, 100.0); // 100 demand (Target 3)

    uint64_t p_ore_mine_out = getPortId(n_ore_mine, "ore", false);
    uint64_t p_t1_in = getPortId(n_t1, "ore", true);
    uint64_t p_t1_out = getPortId(n_t1, "item1", false);
    uint64_t p_t2_in = getPortId(n_t2, "ore", true);
    uint64_t p_t2_out = getPortId(n_t2, "item2", false);

    ASSERT_NE(p_ore_mine_out, 0);
    ASSERT_NE(p_t1_in, 0);
    ASSERT_NE(p_t1_out, 0);
    ASSERT_NE(p_t2_in, 0);
    ASSERT_NE(p_t2_out, 0);

    g.addConnection(p_ore_mine_out, p_t1_in);
    g.addConnection(p_ore_mine_out, p_t2_in);
    g.setPortConstraint(p_ore_mine_out, 90.0); // 90 ore/s supply limit
    g.setPortConstraint(p_t1_out, 100.0);      // 100 demand (Target 1)
    g.setPortConstraint(p_t2_out, 100.0);      // 100 demand (Target 2)

    FactorySolver::SolverResult result = s.solve(g);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(g.getPort(p_coal_proc_out)->rate, 20.0, 0.001);
    EXPECT_NEAR(g.getPort(p_t1_out)->rate, 30.0, 0.001);
    EXPECT_NEAR(g.getPort(p_t2_out)->rate, 30.0, 0.001);
}

TEST_F(FactorySolverTest, Phase2LeafThroughput_SupplyLimitDoesNotPushUnwantedOverproductionWhenTargetExists) {
    // Chain:
    // - Miner outputs iron_ore, capped with LIMIT = 120.0
    // - Smelter converts iron_ore -> iron_ingot (30:30, 1:1)
    // - Constructor converts iron_ingot -> iron_plate (30:20, 3:2)
    // - Constructor plate output has TARGET = 10.0
    // Demand for 10.0 plates requires 15.0 iron_ingots and 15.0 iron_ore.
    // The miner limit of 120.0 must NOT push through to overproduce plates or waste ore.

    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    uint64_t n_const = addNode("make_plates");

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);
    Port* p_const_in = getPort(n_const, "iron_ingot", true);
    Port* p_const_out = getPort(n_const, "iron_plate", false);

    ASSERT_NE(p_miner_out, nullptr);
    ASSERT_NE(p_smelter_in, nullptr);
    ASSERT_NE(p_smelter_out, nullptr);
    ASSERT_NE(p_const_in, nullptr);
    ASSERT_NE(p_const_out, nullptr);

    addConnection(p_miner_out, p_smelter_in);
    addConnection(p_smelter_out, p_const_in);

    graph->setPortConstraint(p_miner_out->id, 120.0); // Raw source limit (capacity)
    graph->setPortConstraint(p_const_out->id, 10.0);   // Target demand

    FactorySolver::SolverResult result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_const_out->rate, 10.0, 0.001);
    EXPECT_NEAR(p_const_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_smelter_out->rate, 15.0, 0.001);
    EXPECT_NEAR(p_miner_out->rate, 15.0, 0.001);
    EXPECT_NEAR(p_miner_out->excess_rate, 0.0, 0.001);
}

TEST_F(FactorySolverTest, MultiInputRecipeWithZeroAmountIngredientDerivesMachineCount) {
    Recipe cat_recipe;
    cat_recipe.name = "Catalytic Smelting";
    cat_recipe.produced_in_machines_keys.push_back("smelter");
    cat_recipe.input_ports.push_back(RecipePort{0.0, "copper_ingot"}); // 0 amount catalyst
    cat_recipe.input_ports.push_back(RecipePort{30.0, "iron_ore"});
    cat_recipe.output_ports.push_back(RecipePort{30.0, "iron_ingot"});
    cat_recipe.time_seconds = 1.0;
    testGameData.recipes["cat_recipe"] = cat_recipe;

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_cat = graph->addNode("Cat Smelter", "cat_recipe");
    addConnection(getPort(n_miner, "iron_ore", false), getPort(n_cat, "iron_ore", true));
    graph->setPortConstraint(getPort(n_cat, "iron_ingot", false)->id, 30.0);

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(graph->getNode(n_cat)->machine_count, 1.0, 0.001);
}

TEST_F(FactorySolverTest, MultiInputRecipeWithZeroAmountCatalystNothingDerivesMachineCount) {
    Recipe cat_recipe;
    cat_recipe.name = "Catalytic Smelting With Nothing";
    cat_recipe.produced_in_machines_keys.push_back("smelter");
    cat_recipe.input_ports.push_back(RecipePort{0.0, "nothing"});
    cat_recipe.input_ports.push_back(RecipePort{30.0, "iron_ore"});
    cat_recipe.output_ports.push_back(RecipePort{30.0, "iron_ingot"});
    cat_recipe.time_seconds = 1.0;
    testGameData.recipes["cat_recipe_nothing"] = cat_recipe;

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_cat = graph->addNode("Cat Smelter Nothing", "cat_recipe_nothing");
    addConnection(getPort(n_miner, "iron_ore", false), getPort(n_cat, "iron_ore", true));
    graph->setPortConstraint(getPort(n_cat, "iron_ingot", false)->id, 30.0);

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(graph->getNode(n_cat)->machine_count, 1.0, 0.001);
}

TEST_F(FactorySolverTest, AllZeroAmountInputsFallBackToOutputForMachineCount) {
    Recipe zero_in_recipe;
    zero_in_recipe.name = "Zero Input Recipe";
    zero_in_recipe.produced_in_machines_keys.push_back("smelter");
    zero_in_recipe.input_ports.push_back(RecipePort{0.0, "copper_ingot"});
    zero_in_recipe.output_ports.push_back(RecipePort{30.0, "iron_ingot"});
    zero_in_recipe.time_seconds = 1.0;
    testGameData.recipes["zero_in_recipe"] = zero_in_recipe;

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_node = graph->addNode("Zero In Smelter", "zero_in_recipe");
    graph->setPortConstraint(getPort(n_node, "iron_ingot", false)->id, 30.0);

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(graph->getNode(n_node)->machine_count, 1.0, 0.001);
}

TEST_F(FactorySolverTest, UnboundedPhase2FailureNotMaskedByPhase3) {
    Recipe amp_a;
    amp_a.name = "Amp A";
    amp_a.produced_in_machines_keys.push_back("smelter");
    amp_a.input_ports.push_back(RecipePort{1.0, "iron_ore"});
    amp_a.output_ports.push_back(RecipePort{2.0, "iron_ingot"});
    amp_a.time_seconds = 1.0;
    testGameData.recipes["amp_a"] = amp_a;

    Recipe amp_b;
    amp_b.name = "Amp B";
    amp_b.produced_in_machines_keys.push_back("smelter");
    amp_b.input_ports.push_back(RecipePort{1.0, "iron_ingot"});
    amp_b.output_ports.push_back(RecipePort{2.0, "iron_ore"});
    amp_b.output_ports.push_back(RecipePort{1.0, "screw"}); // unconstrained leaf output
    amp_b.time_seconds = 1.0;
    testGameData.recipes["amp_b"] = amp_b;

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_a = graph->addNode("Amp Node A", "amp_a");
    uint64_t n_b = graph->addNode("Amp Node B", "amp_b");

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_a_in = getPort(n_a, "iron_ore", true);
    Port* p_a_out = getPort(n_a, "iron_ingot", false);
    Port* p_b_in = getPort(n_b, "iron_ingot", true);
    Port* p_b_out_ore = getPort(n_b, "iron_ore", false);
    Port* p_b_out_screw = getPort(n_b, "screw", false);

    addConnection(p_miner_out, p_a_in);
    addConnection(p_a_out, p_b_in);
    addConnection(p_b_out_ore, p_a_in);

    // Under v3 (P2), there is no unbounded leaf throughput maximization stage:
    // an unconstrained amplifying loop stabilizes cleanly at 0.0 with SUCCESS.
    graph->setPortConstraint(p_miner_out->id, 10.0);

    auto result = solver->solve(*graph);
    EXPECT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_miner_out->rate, 0.0, 0.001);
    EXPECT_NEAR(p_b_out_screw->rate, 0.0, 0.001);
}

TEST_F(FactorySolverTest, StaleMachineCountsResetOnFailedSolve) {
    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    addConnection(p_miner_out, p_smelter_in);
    graph->setPortConstraint(p_miner_out->id, 30.0);

    auto result1 = solver->solve(*graph);
    ASSERT_EQ(result1.status, FactorySolver::SolverResultStatus::SUCCESS);
    ASSERT_NEAR(graph->getNode(n_smelter)->machine_count, 1.0, 0.001);
    ASSERT_NEAR(graph->getNode(n_miner)->machine_count, 1.0, 0.001);

    // Inject a non-finite constraint to trigger a genuine component solve failure
    graph->setPortConstraint(p_miner_out->id, std::numeric_limits<double>::quiet_NaN());

    auto result2 = solver->solve(*graph);
    EXPECT_NE(result2.status, FactorySolver::SolverResultStatus::SUCCESS);
    // When solve fails on the component, machine counts and rates in that component must be reset to 0.0.
    EXPECT_DOUBLE_EQ(graph->getNode(n_smelter)->machine_count, 0.0);
    EXPECT_DOUBLE_EQ(graph->getNode(n_miner)->machine_count, 0.0);
    EXPECT_DOUBLE_EQ(p_miner_out->rate, 0.0);
    EXPECT_DOUBLE_EQ(p_smelter_in->rate, 0.0);
}

// ============================================================================
// Solver v3 Rework Tests (T1, T2 Scenarios 1-11, T3)
// ============================================================================

TEST_F(FactorySolverTest, V3_Scenario1_SupplyDrivenFactory_PushesDownstreamWithoutOverflow) {
    // Miner (60 ore/s) -> Smelter (1:1) -> Constructor (30 ingot -> 20 plate)
    // No targets set: Miner's AUTO constraint resolves to PUSH 60, E_p = 0 on all wired outputs.
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    uint64_t n_plates = addNode("make_plates");

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);
    Port* p_plates_in = getPort(n_plates, "iron_ingot", true);
    Port* p_plates_out = getPort(n_plates, "iron_plate", false);

    addConnection(p_miner_out, p_smelter_in);
    addConnection(p_smelter_out, p_plates_in);

    graph->setPortConstraint(p_miner_out->id, 60.0);

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_EQ(solver->getResolvedConstraintMode(p_miner_out->id), ConstraintMode::PUSH);
    EXPECT_NEAR(solver->getGoalSaturation(p_miner_out->id), 1.0, 1e-6);
    EXPECT_FALSE(solver->isGoalBottlenecked(p_miner_out->id));

    EXPECT_NEAR(p_miner_out->rate, 60.0, 0.001);
    EXPECT_NEAR(p_miner_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_smelter_out->rate, 60.0, 0.001);
    EXPECT_NEAR(p_smelter_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_plates_out->rate, 40.0, 0.001);
}

TEST_F(FactorySolverTest, V3_Scenario2_DemandDrivenFactoryWithSupplyCap_PullsMinimumNeeded) {
    // Miner (60 ore/s) -> Smelter (1:1) -> Constructor (30 ingot -> 20 plate, TARGET 20)
    // Miner's AUTO constraint resolves to LIMIT 60; Miner produces only 30 ore/s.
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    uint64_t n_plates = addNode("make_plates");

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);
    Port* p_plates_in = getPort(n_plates, "iron_ingot", true);
    Port* p_plates_out = getPort(n_plates, "iron_plate", false);

    addConnection(p_miner_out, p_smelter_in);
    addConnection(p_smelter_out, p_plates_in);

    graph->setPortConstraint(p_miner_out->id, 60.0);
    graph->setPortConstraint(p_plates_out->id, 20.0);

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_EQ(solver->getResolvedConstraintMode(p_miner_out->id), ConstraintMode::LIMIT);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_plates_out->id), ConstraintMode::TARGET);
    EXPECT_NEAR(p_plates_out->rate, 20.0, 0.001);
    EXPECT_NEAR(p_smelter_out->rate, 30.0, 0.001);
    EXPECT_NEAR(p_miner_out->rate, 30.0, 0.001);
    EXPECT_NEAR(p_miner_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_smelter_out->excess_rate, 0.0, 0.001);
}

TEST_F(FactorySolverTest, V3_Scenario3_ByproductOverflowIntoSinkAndTargetConsumer) {
    // Refinery produces 60 iron_plate (Fuel) + 40 screw (Resin).
    // Fuel has TARGET 60. Resin splits to:
    //  - Plastic maker (10 screw -> 10 wire, TARGET 10)
    //  - Sink (consumes screw -> nothing)
    // Stage 1 locks Fuel = 60 and Plastic = 10.
    // Stage 2a minimizes T_K activity (Refinery = 1 machine -> 40 screw produced, Plastic maker consumes 10 screw).
    // Stage 2b minimizes E_p on screw output -> routes the remaining 30 screw into the Sink!
    Recipe refine;
    refine.name = "Refine";
    refine.produced_in_machines_keys.push_back("assembler");
    refine.input_ports.push_back(RecipePort{30.0, "iron_ore"});
    refine.output_ports.push_back(RecipePort{60.0, "iron_plate"});
    refine.output_ports.push_back(RecipePort{40.0, "screw"});
    refine.time_seconds = 1.0;
    testGameData.recipes["refine"] = refine;

    Recipe make_plastic;
    make_plastic.name = "Make Plastic";
    make_plastic.produced_in_machines_keys.push_back("constructor");
    make_plastic.input_ports.push_back(RecipePort{10.0, "screw"});
    make_plastic.output_ports.push_back(RecipePort{10.0, "wire"});
    make_plastic.time_seconds = 1.0;
    testGameData.recipes["make_plastic"] = make_plastic;

    Recipe sink_screw;
    sink_screw.name = "Sink Screw";
    sink_screw.produced_in_machines_keys.push_back("sink");
    sink_screw.input_ports.push_back(RecipePort{1.0, "screw"});
    sink_screw.output_ports.push_back(RecipePort{1.0, "nothing"});
    sink_screw.time_seconds = 1.0;
    testGameData.recipes["sink_screw"] = sink_screw;

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_ref = addNode("refine");
    uint64_t n_plas = addNode("make_plastic");
    uint64_t n_sink = addNode("sink_screw");

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_ref_in = getPort(n_ref, "iron_ore", true);
    Port* p_fuel_out = getPort(n_ref, "iron_plate", false);
    Port* p_resin_out = getPort(n_ref, "screw", false);
    Port* p_plas_in = getPort(n_plas, "screw", true);
    Port* p_plas_out = getPort(n_plas, "wire", false);
    Port* p_sink_in = getPort(n_sink, "screw", true);

    addConnection(p_miner_out, p_ref_in);
    addConnection(p_resin_out, p_plas_in);
    addConnection(p_resin_out, p_sink_in);

    graph->setPortConstraint(p_fuel_out->id, 60.0);
    graph->setPortConstraint(p_plas_out->id, 10.0);

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_fuel_out->rate, 60.0, 0.001);
    EXPECT_NEAR(p_plas_out->rate, 10.0, 0.001);
    EXPECT_NEAR(p_plas_in->rate, 10.0, 0.001);
    EXPECT_NEAR(p_sink_in->rate, 30.0, 0.001);
    EXPECT_NEAR(p_resin_out->excess_rate, 0.0, 0.001);
}

TEST_F(FactorySolverTest, V3_Scenario4_MixedSplitterSinkBypassGuard_PrefersNonSinkBranch) {
    // Miner (60) -> Smelter (60) -> splits to Constructor (make_plates, unconstrained export) and Sink (sink_ingot).
    // Stage 2c.1 mixed-splitter sink-branch minimization ensures 0 goes to Sink when Constructor is uncapped,
    // and only overflow (30) goes to Sink when Constructor input is capped at 30.
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    uint64_t n_plates = addNode("make_plates");
    uint64_t n_sink = addNode("sink_ingot");

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);
    Port* p_plates_in = getPort(n_plates, "iron_ingot", true);
    Port* p_plates_out = getPort(n_plates, "iron_plate", false);
    Port* p_sink_in = getPort(n_sink, "iron_ingot", true);

    addConnection(p_miner_out, p_smelter_in);
    addConnection(p_smelter_out, p_plates_in);
    addConnection(p_smelter_out, p_sink_in);

    graph->setPortConstraint(p_miner_out->id, 60.0);

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_miner_out->rate, 60.0, 0.001);
    EXPECT_NEAR(p_plates_in->rate, 60.0, 0.001);
    EXPECT_NEAR(p_plates_out->rate, 40.0, 0.001);
    EXPECT_NEAR(p_sink_in->rate, 0.0, 0.001);

    // Now cap Constructor input at 30: remaining 30 must overflow into the Sink
    graph->setPortConstraint(p_plates_in->id, 30.0);
    result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_miner_out->rate, 60.0, 0.001);
    EXPECT_NEAR(p_plates_in->rate, 30.0, 0.001);
    EXPECT_NEAR(p_plates_out->rate, 20.0, 0.001);
    EXPECT_NEAR(p_sink_in->rate, 30.0, 0.001);
}

TEST_F(FactorySolverTest, V3_Scenario5_DisconnectedComponents_IndependentModeResolutionAndFailureIsolation) {
    // Component K1: Supply-driven chain (Miner 1 = 60 -> Smelter 1, no targets)
    uint64_t n_miner1 = addNode("mine_ore");
    uint64_t n_smelter1 = addNode("smelt_ingot");
    // Component K2: Demand-driven chain (Miner 2 = 60 -> Smelter 2 with Target = 30)
    uint64_t n_miner2 = addNode("mine_ore");
    uint64_t n_smelter2 = addNode("smelt_ingot");

    Port* p_m1_out = getPort(n_miner1, "iron_ore", false);
    Port* p_s1_in = getPort(n_smelter1, "iron_ore", true);
    Port* p_s1_out = getPort(n_smelter1, "iron_ingot", false);
    addConnection(p_m1_out, p_s1_in);
    graph->setPortConstraint(p_m1_out->id, 60.0);

    Port* p_m2_out = getPort(n_miner2, "iron_ore", false);
    Port* p_s2_in = getPort(n_smelter2, "iron_ore", true);
    Port* p_s2_out = getPort(n_smelter2, "iron_ingot", false);
    addConnection(p_m2_out, p_s2_in);
    graph->setPortConstraint(p_m2_out->id, 60.0);
    graph->setPortConstraint(p_s2_out->id, 30.0);

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    // K1 is supply-driven (PUSH 60 -> 60)
    EXPECT_EQ(solver->getResolvedConstraintMode(p_m1_out->id), ConstraintMode::PUSH);
    EXPECT_NEAR(p_m1_out->rate, 60.0, 0.001);
    EXPECT_NEAR(p_s1_out->rate, 60.0, 0.001);

    // K2 is demand-driven (LIMIT 60 + TARGET 30 -> 30)
    EXPECT_EQ(solver->getResolvedConstraintMode(p_m2_out->id), ConstraintMode::LIMIT);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_s2_out->id), ConstraintMode::TARGET);
    EXPECT_NEAR(p_m2_out->rate, 30.0, 0.001);
    EXPECT_NEAR(p_s2_out->rate, 30.0, 0.001);

    // Now break K2 with NaN constraint: K1 must still solve to 60 while K2 is zeroed!
    graph->setPortConstraint(p_s2_out->id, std::numeric_limits<double>::quiet_NaN());
    auto result2 = solver->solve(*graph);
    EXPECT_EQ(result2.status, FactorySolver::SolverResultStatus::ERROR_);
    EXPECT_NEAR(p_m1_out->rate, 60.0, 0.001);
    EXPECT_NEAR(p_s1_out->rate, 60.0, 0.001);
    EXPECT_NEAR(graph->getNode(n_smelter1)->machine_count, 2.0, 0.001);
    EXPECT_DOUBLE_EQ(p_m2_out->rate, 0.0);
    EXPECT_DOUBLE_EQ(p_s2_out->rate, 0.0);
    EXPECT_DOUBLE_EQ(graph->getNode(n_smelter2)->machine_count, 0.0);
}

TEST_F(FactorySolverTest, V3_Scenario6_EqualAndUnequalEfficiencyParallelSuppliers) {
    // Subcase A: Two identical Miner -> Smelter chains merging into Constructor (make_plates, TARGET 40 -> needs 60 ingots).
    // Equal efficiency: Stage 2a locks total machines, Stage 4 splits 50/50 (30 ingots from each branch).
    uint64_t n_m1 = addNode("mine_ore");
    uint64_t n_s1 = addNode("smelt_ingot");
    uint64_t n_m2 = addNode("mine_ore");
    uint64_t n_s2 = addNode("smelt_ingot");
    uint64_t n_plates = addNode("make_plates");

    addConnection(getPort(n_m1, "iron_ore", false), getPort(n_s1, "iron_ore", true));
    addConnection(getPort(n_m2, "iron_ore", false), getPort(n_s2, "iron_ore", true));
    addConnection(getPort(n_s1, "iron_ingot", false), getPort(n_plates, "iron_ingot", true));
    addConnection(getPort(n_s2, "iron_ingot", false), getPort(n_plates, "iron_ingot", true));

    graph->setPortConstraint(getPort(n_plates, "iron_plate", false)->id, 40.0);

    auto resultA = solver->solve(*graph);
    ASSERT_EQ(resultA.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(getPort(n_s1, "iron_ingot", false)->rate, 30.0, 0.001);
    EXPECT_NEAR(getPort(n_s2, "iron_ingot", false)->rate, 30.0, 0.001);

    // Subcase B: Branch 2 uses an alternate recipe (30 ore -> 60 ingot in 1s, i.e. half the machines per ingot).
    // Stage 2a prefers the more machine-efficient Branch 2 (60 ingots from Branch 2, 0 from Branch 1).
    Recipe pure_ingot;
    pure_ingot.name = "Pure Iron Ingot";
    pure_ingot.produced_in_machines_keys.push_back("smelter");
    pure_ingot.input_ports.push_back(RecipePort{30.0, "iron_ore"});
    pure_ingot.output_ports.push_back(RecipePort{60.0, "iron_ingot"});
    pure_ingot.time_seconds = 1.0;
    testGameData.recipes["pure_ingot"] = pure_ingot;

    graph = std::make_unique<FactoryGraph>(testGameData);
    n_m1 = addNode("mine_ore");
    n_s1 = addNode("smelt_ingot");
    n_m2 = addNode("mine_ore");
    n_s2 = graph->addNode("Pure Smelter", "pure_ingot");
    n_plates = addNode("make_plates");

    addConnection(getPort(n_m1, "iron_ore", false), getPort(n_s1, "iron_ore", true));
    addConnection(getPort(n_m2, "iron_ore", false), getPort(n_s2, "iron_ore", true));
    addConnection(getPort(n_s1, "iron_ingot", false), getPort(n_plates, "iron_ingot", true));
    addConnection(getPort(n_s2, "iron_ingot", false), getPort(n_plates, "iron_ingot", true));
    graph->setPortConstraint(getPort(n_plates, "iron_plate", false)->id, 40.0);

    auto resultB = solver->solve(*graph);
    ASSERT_EQ(resultB.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(getPort(n_s1, "iron_ingot", false)->rate, 0.0, 0.001);
    EXPECT_NEAR(getPort(n_s2, "iron_ingot", false)->rate, 60.0, 0.001);

    // Subcase C: Equal-efficiency parallel suppliers where both Miners are capped at 15 (s_val = 0.5 bottlenecked).
    graph = std::make_unique<FactoryGraph>(testGameData);
    n_m1 = addNode("mine_ore");
    n_s1 = addNode("smelt_ingot");
    n_m2 = addNode("mine_ore");
    n_s2 = addNode("smelt_ingot");
    n_plates = addNode("make_plates");

    addConnection(getPort(n_m1, "iron_ore", false), getPort(n_s1, "iron_ore", true));
    addConnection(getPort(n_m2, "iron_ore", false), getPort(n_s2, "iron_ore", true));
    addConnection(getPort(n_s1, "iron_ingot", false), getPort(n_plates, "iron_ingot", true));
    addConnection(getPort(n_s2, "iron_ingot", false), getPort(n_plates, "iron_ingot", true));
    graph->setPortConstraint(getPort(n_m1, "iron_ore", false)->id, 15.0);
    graph->setPortConstraint(getPort(n_m2, "iron_ore", false)->id, 15.0);
    graph->setPortConstraint(getPort(n_plates, "iron_plate", false)->id, 40.0);

    auto resultC = solver->solve(*graph);
    ASSERT_EQ(resultC.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(getPort(n_s1, "iron_ingot", false)->rate, 15.0, 0.001);
    EXPECT_NEAR(getPort(n_s2, "iron_ingot", false)->rate, 15.0, 0.001);
    EXPECT_NEAR(getPort(n_plates, "iron_plate", false)->rate, 20.0, 0.001);
}

TEST_F(FactorySolverTest, V3_Scenario7_SelfSustainingAmplifyingLoopWithTarget) {
    // Amp A: 1 ore -> 2 ingot; Amp B: 1 ingot -> 2 ore + 1 screw.
    // Miner (iron_ore) + Amp B (iron_ore) -> Amp A (iron_ore).
    // Target = 10 on Amp B's screw output.
    // Loop produces 20 ore while Amp A only needs 5 ore, so Miner produces 0 and 15 excess ore is shed cleanly.
    Recipe amp_a;
    amp_a.name = "Amp A";
    amp_a.produced_in_machines_keys.push_back("smelter");
    amp_a.input_ports.push_back(RecipePort{1.0, "iron_ore"});
    amp_a.output_ports.push_back(RecipePort{2.0, "iron_ingot"});
    amp_a.time_seconds = 1.0;
    testGameData.recipes["amp_a"] = amp_a;

    Recipe amp_b;
    amp_b.name = "Amp B";
    amp_b.produced_in_machines_keys.push_back("smelter");
    amp_b.input_ports.push_back(RecipePort{1.0, "iron_ingot"});
    amp_b.output_ports.push_back(RecipePort{2.0, "iron_ore"});
    amp_b.output_ports.push_back(RecipePort{1.0, "screw"});
    amp_b.time_seconds = 1.0;
    testGameData.recipes["amp_b"] = amp_b;

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_a = graph->addNode("Amp A", "amp_a");
    uint64_t n_b = graph->addNode("Amp B", "amp_b");

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_a_in = getPort(n_a, "iron_ore", true);
    Port* p_a_out = getPort(n_a, "iron_ingot", false);
    Port* p_b_in = getPort(n_b, "iron_ingot", true);
    Port* p_b_out_ore = getPort(n_b, "iron_ore", false);
    Port* p_b_out_screw = getPort(n_b, "screw", false);

    addConnection(p_miner_out, p_a_in);
    addConnection(p_a_out, p_b_in);
    addConnection(p_b_out_ore, p_a_in);

    graph->setPortConstraint(p_b_out_screw->id, 10.0);

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_b_out_screw->rate, 10.0, 0.001);
    EXPECT_NEAR(p_b_in->rate, 10.0, 0.001);
    EXPECT_NEAR(p_a_out->rate, 10.0, 0.001);
    EXPECT_NEAR(p_a_in->rate, 5.0, 0.001);
    EXPECT_NEAR(p_miner_out->rate, 0.0, 0.001);
    EXPECT_NEAR(p_b_out_ore->excess_rate, 15.0, 0.001);
}

TEST_F(FactorySolverTest, V3_Scenario8_MultipleCompetingTargets_WaterFillingFairnessAndSaturations) {
    // Miner (60 ore/s) -> Smelter (60 ingot/s) -> splits to:
    //  - Constructor 1 (make_plates: 30 ingot -> 20 plate), TARGET = 40 plates (needs 60 ingots)
    //  - Constructor 2 (1:1 iron_ingot -> wire), TARGET = 60 wire (needs 60 ingots)
    // Total ingots needed for 100% = 120, available = 60 -> S = 0.5 (20 plates, 30 wire).
    Recipe make_rods;
    make_rods.name = "Make Rods";
    make_rods.produced_in_machines_keys.push_back("constructor");
    make_rods.input_ports.push_back(RecipePort{15.0, "iron_ingot"});
    make_rods.output_ports.push_back(RecipePort{15.0, "wire"});
    make_rods.time_seconds = 1.0;
    testGameData.recipes["make_rods"] = make_rods;

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    uint64_t n_plates = addNode("make_plates");
    uint64_t n_rods = addNode("make_rods");

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);
    Port* p_plates_in = getPort(n_plates, "iron_ingot", true);
    Port* p_plates_out = getPort(n_plates, "iron_plate", false);
    Port* p_rods_in = getPort(n_rods, "iron_ingot", true);
    Port* p_rods_out = getPort(n_rods, "wire", false);

    addConnection(p_miner_out, p_smelter_in);
    addConnection(p_smelter_out, p_plates_in);
    addConnection(p_smelter_out, p_rods_in);

    graph->setPortConstraint(p_miner_out->id, 60.0);
    graph->setPortConstraint(p_plates_out->id, 40.0);
    graph->setPortConstraint(p_rods_out->id, 60.0);

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_plates_out->rate, 20.0, 0.001);
    EXPECT_NEAR(p_rods_out->rate, 30.0, 0.001);
    EXPECT_NEAR(solver->getGoalSaturation(p_plates_out->id), 0.5, 1e-4);
    EXPECT_NEAR(solver->getGoalSaturation(p_rods_out->id), 0.5, 1e-4);
    EXPECT_TRUE(solver->isGoalBottlenecked(p_plates_out->id));
    EXPECT_TRUE(solver->isGoalBottlenecked(p_rods_out->id));
}

TEST_F(FactorySolverTest, V3_Scenario9_SupplyPushWithAsymmetricDownstreamBottleneck) {
    // Miner (100 ore/s, no targets -> PUSH 100) -> splits to Smelter A (input capped at 30) and Smelter B (uncapped).
    // Because E_p = 0 on Z_K, Miner pushes all 100 ore/s: 30 to Smelter A and 70 to Smelter B.
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_sA = addNode("smelt_ingot");
    uint64_t n_sB = addNode("smelt_ingot");

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_sA_in = getPort(n_sA, "iron_ore", true);
    Port* p_sA_out = getPort(n_sA, "iron_ingot", false);
    Port* p_sB_in = getPort(n_sB, "iron_ore", true);
    Port* p_sB_out = getPort(n_sB, "iron_ingot", false);

    addConnection(p_miner_out, p_sA_in);
    addConnection(p_miner_out, p_sB_in);

    graph->setPortConstraint(p_miner_out->id, 100.0);
    graph->setPortConstraint(p_sA_in->id, 30.0);

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_miner_out->rate, 100.0, 0.001);
    EXPECT_NEAR(p_miner_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_sA_out->rate, 30.0, 0.001);
    EXPECT_NEAR(p_sB_out->rate, 70.0, 0.001);
}

TEST_F(FactorySolverTest, V3_Scenario10_OverCapableSupplyPushThrottledByDownstreamBottleneck) {
    // Miner (60 ore/s -> PUSH 60) -> Smelter (input capped at 30).
    // Because p_miner_out is in Z_K, E_p = 0, so Miner throttles back to 30 (S = 0.5) instead of dumping 30 excess.
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);

    addConnection(p_miner_out, p_smelter_in);

    graph->setPortConstraint(p_miner_out->id, 60.0);
    graph->setPortConstraint(p_smelter_in->id, 30.0);

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_miner_out->rate, 30.0, 0.001);
    EXPECT_NEAR(p_miner_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_smelter_out->rate, 30.0, 0.001);
    EXPECT_NEAR(solver->getGoalSaturation(p_miner_out->id), 0.5, 1e-4);
    EXPECT_TRUE(solver->isGoalBottlenecked(p_miner_out->id));
}

TEST_F(FactorySolverTest, V3_Scenario11_UnwiredImportPortSymmetry) {
    // Constructor (make_plates: 30 ingot -> 20 plate) with unwired input port constrained to 60.
    // Case A: No targets in component -> unwired import resolves to PUSH 60, producing 40 plates.
    uint64_t n_plates = addNode("make_plates");
    Port* p_in = getPort(n_plates, "iron_ingot", true);
    Port* p_out = getPort(n_plates, "iron_plate", false);

    graph->setPortConstraint(p_in->id, 60.0);

    auto resultA = solver->solve(*graph);
    ASSERT_EQ(resultA.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_in->id), ConstraintMode::PUSH);
    EXPECT_NEAR(p_in->rate, 60.0, 0.001);
    EXPECT_NEAR(p_out->rate, 40.0, 0.001);

    // Case B: Output port has TARGET = 20 -> unwired import resolves to LIMIT 60, pulling only 30 ingots.
    graph->setPortConstraint(p_out->id, 20.0);
    auto resultB = solver->solve(*graph);
    ASSERT_EQ(resultB.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_in->id), ConstraintMode::LIMIT);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_out->id), ConstraintMode::TARGET);
    EXPECT_NEAR(p_in->rate, 30.0, 0.001);
    EXPECT_NEAR(p_out->rate, 20.0, 0.001);
}

TEST_F(FactorySolverTest, V3_AuxiliarySinkSupplierDemotion_MultiInputSinkVsSharedInputPort) {
    // Multi-input sink consuming 1 screw + 1 wire -> nothing.
    // Main factory: Miner (30 ore/s) -> Smelter -> produces 30 iron_plate (export) + 10 screw (byproduct to Sink in_1).
    // Auxiliary supplier: Wire maker (unwired copper_ingot input = 60 -> 120 wire) connected to Sink in_2.
    // Because Auxiliary supplier is in S+_K and only reaches Sink via in_2 while Main factory reaches Sink via in_1,
    // Auxiliary supplier's 60 constraint is demoted to LIMIT (not PUSH), so Sink only consumes the 0/10 needed!
    Recipe main_prod;
    main_prod.name = "Main Prod";
    main_prod.produced_in_machines_keys.push_back("assembler");
    main_prod.input_ports.push_back(RecipePort{1.0, "nothing"});
    main_prod.output_ports.push_back(RecipePort{30.0, "iron_plate"}); // export
    main_prod.output_ports.push_back(RecipePort{10.0, "screw"});      // byproduct to sink
    main_prod.time_seconds = 1.0;
    testGameData.recipes["main_prod"] = main_prod;

    Recipe aux_wire;
    aux_wire.name = "Aux Wire";
    aux_wire.produced_in_machines_keys.push_back("constructor");
    aux_wire.input_ports.push_back(RecipePort{1.0, "nothing"});
    aux_wire.output_ports.push_back(RecipePort{10.0, "wire"});
    aux_wire.time_seconds = 1.0;
    testGameData.recipes["aux_wire"] = aux_wire;

    Recipe dual_sink;
    dual_sink.name = "Dual Sink";
    dual_sink.produced_in_machines_keys.push_back("sink");
    dual_sink.input_ports.push_back(RecipePort{1.0, "screw"});
    dual_sink.input_ports.push_back(RecipePort{1.0, "wire"});
    dual_sink.output_ports.push_back(RecipePort{1.0, "nothing"});
    dual_sink.time_seconds = 1.0;
    testGameData.recipes["dual_sink"] = dual_sink;

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_main = addNode("main_prod");
    uint64_t n_aux = addNode("aux_wire");
    uint64_t n_sink = addNode("dual_sink");

    Port* p_main_plate = getPort(n_main, "iron_plate", false);
    Port* p_main_screw = getPort(n_main, "screw", false);
    Port* p_aux_wire = getPort(n_aux, "wire", false);
    Port* p_sink_screw = getPort(n_sink, "screw", true);
    Port* p_sink_wire = getPort(n_sink, "wire", true);

    addConnection(p_main_screw, p_sink_screw);
    addConnection(p_aux_wire, p_sink_wire);

    // Supply-driven graph: main_prod pushes 10 screw, aux_wire has AUTO constraint 50.0.
    // aux_wire must be demoted to LIMIT so it only supplies the 10 wire required by the 10 screw from main_prod!
    graph->setPortConstraint(p_main_screw->id, 10.0, ConstraintMode::PUSH);
    graph->setPortConstraint(p_aux_wire->id, 50.0, ConstraintMode::AUTO);

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_aux_wire->id), ConstraintMode::LIMIT);
    EXPECT_NEAR(p_main_plate->rate, 30.0, 0.001);
    EXPECT_NEAR(p_sink_screw->rate, 10.0, 0.001);
    EXPECT_NEAR(p_aux_wire->rate, 10.0, 0.001);
}

TEST_F(FactorySolverTest, V3_SharedSingleInputSink_DoesNotDemoteDedicatedSupplier) {
    // Both a main-factory node (with an unwired export) and a dedicated supplier merge into the SAME input port
    // of a single-input sink. The dedicated supplier's AUTO constraint must resolve to PUSH (not demoted to LIMIT).
    Recipe main_prod;
    main_prod.name = "Main Prod";
    main_prod.produced_in_machines_keys.push_back("assembler");
    main_prod.input_ports.push_back(RecipePort{1.0, "nothing"});
    main_prod.output_ports.push_back(RecipePort{30.0, "iron_plate"}); // export
    main_prod.output_ports.push_back(RecipePort{10.0, "iron_ingot"}); // to sink
    main_prod.time_seconds = 1.0;
    testGameData.recipes["main_prod"] = main_prod;

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_main = addNode("main_prod");
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    uint64_t n_sink = addNode("sink_ingot");

    addConnection(getPort(n_main, "iron_ingot", false), getPort(n_sink, "iron_ingot", true));
    addConnection(getPort(n_miner, "iron_ore", false), getPort(n_smelter, "iron_ore", true));
    addConnection(getPort(n_smelter, "iron_ingot", false), getPort(n_sink, "iron_ingot", true));

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    graph->setPortConstraint(p_miner_out->id, 30.0); // AUTO on dedicated supplier merging into shared sink input

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_miner_out->id), ConstraintMode::PUSH);
    EXPECT_NEAR(p_miner_out->rate, 30.0, 0.001);
    EXPECT_NEAR(getPort(n_sink, "iron_ingot", true)->rate, 30.0, 0.001);
}

TEST_F(FactorySolverTest, V3_MissingRecipeWithPorts_ClampsPortsToZero) {
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);
    addConnection(p_miner_out, p_smelter_in);

    // Corrupt smelter's recipe key while keeping its ports intact
    graph->getNode(n_smelter)->selected_recipe_key = "missing_recipe_key";
    graph->setPortConstraint(p_smelter_out->id, 30.0);

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_smelter_in->rate, 0.0, 0.001);
    EXPECT_NEAR(p_smelter_out->rate, 0.0, 0.001);
    EXPECT_NEAR(p_miner_out->rate, 0.0, 0.001);
}

TEST_F(FactorySolverTest, V3_TargetZero_DoesNotSwitchComponentIntoDemandDrivenMode) {
    // Miner (60) -> Smelter -> splits to Constructor 1 (TARGET = 0) and Constructor 2 (unconstrained).
    // TARGET = 0 is only a zero cap, NOT an effective target (g > 0), so Miner still resolves to PUSH 60!
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    uint64_t n_p1 = addNode("make_plates");
    uint64_t n_p2 = addNode("make_plates");

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);
    Port* p_p1_in = getPort(n_p1, "iron_ingot", true);
    Port* p_p1_out = getPort(n_p1, "iron_plate", false);
    Port* p_p2_in = getPort(n_p2, "iron_ingot", true);
    Port* p_p2_out = getPort(n_p2, "iron_plate", false);

    addConnection(p_miner_out, p_smelter_in);
    addConnection(p_smelter_out, p_p1_in);
    addConnection(p_smelter_out, p_p2_in);

    graph->setPortConstraint(p_miner_out->id, 60.0);
    graph->setPortConstraint(p_p1_out->id, 0.0); // Zero cap

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_miner_out->id), ConstraintMode::PUSH);
    EXPECT_NEAR(p_p1_out->rate, 0.0, 0.001);
    EXPECT_NEAR(p_p2_out->rate, 40.0, 0.001);
}

TEST_F(FactorySolverTest, V3_T3_ExplicitConstraintModes_LimitTargetRatedAndUnratedPush) {
    // 1. Explicit LIMIT on a Miner in a targetless graph: stays at 0 (does not push).
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);
    addConnection(p_miner_out, p_smelter_in);

    graph->setPortConstraint(p_miner_out->id, 60.0, ConstraintMode::LIMIT);
    auto res1 = solver->solve(*graph);
    ASSERT_EQ(res1.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_miner_out->id), ConstraintMode::LIMIT);
    EXPECT_NEAR(p_miner_out->rate, 0.0, 0.001);

    // 2. Explicit Rated PUSH on Miner alongside a smaller Target on Smelter:
    // Smelter output has TARGET 30 (which also caps Smelter output at 30), and Miner has explicit PUSH 60.
    // Because Miner is PUSH (E_p = 0 on Z_K) and Smelter output is capped at 30, Miner is throttled to 30.
    graph->setPortConstraint(p_miner_out->id, 60.0, ConstraintMode::PUSH);
    graph->setPortConstraint(p_smelter_out->id, 30.0, ConstraintMode::TARGET);
    auto res2 = solver->solve(*graph);
    ASSERT_EQ(res2.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_miner_out->id), ConstraintMode::PUSH);
    EXPECT_NEAR(p_miner_out->rate, 30.0, 0.001);
    EXPECT_NEAR(solver->getGoalSaturation(p_miner_out->id), 0.5, 1e-4);
    EXPECT_NEAR(solver->getGoalSaturation(p_smelter_out->id), 1.0, 1e-4);

    // 3. Explicit Unrated PUSH (user_constraint = -1.0, ConstraintMode::PUSH) on a byproduct output port:
    // Refinery produces 60 iron_plate (TARGET 60) + 40 screw (wired to a consumer capped at 20 screw).
    // With AUTO on the screw output, Refinery produces 60 iron_plate and sheds 20 screw as excess_rate.
    // With Unrated PUSH on the screw output, E_p = 0 is enforced on the screw port, so Refinery is throttled to 30 iron_plate!
    Recipe split_rec;
    split_rec.name = "Split";
    split_rec.produced_in_machines_keys.push_back("assembler");
    split_rec.input_ports.push_back(RecipePort{1.0, "nothing"});
    split_rec.output_ports.push_back(RecipePort{60.0, "iron_plate"});
    split_rec.output_ports.push_back(RecipePort{40.0, "screw"});
    split_rec.time_seconds = 1.0;
    testGameData.recipes["split_rec"] = split_rec;

    Recipe screw_user;
    screw_user.name = "Screw User";
    screw_user.produced_in_machines_keys.push_back("constructor");
    screw_user.input_ports.push_back(RecipePort{20.0, "screw"});
    screw_user.output_ports.push_back(RecipePort{20.0, "wire"});
    screw_user.time_seconds = 1.0;
    testGameData.recipes["screw_user"] = screw_user;

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_split = addNode("split_rec");
    uint64_t n_user = addNode("screw_user");
    Port* p_plate = getPort(n_split, "iron_plate", false);
    Port* p_screw = getPort(n_split, "screw", false);
    Port* p_user_in = getPort(n_user, "screw", true);
    addConnection(p_screw, p_user_in);

    graph->setPortConstraint(p_plate->id, 60.0, ConstraintMode::TARGET);
    graph->setPortConstraint(p_user_in->id, 20.0, ConstraintMode::LIMIT);

    // Default AUTO: 60 plate produced, 20 screw consumed, 20 screw excess
    auto res3a = solver->solve(*graph);
    ASSERT_EQ(res3a.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_plate->rate, 60.0, 0.001);
    EXPECT_NEAR(p_screw->excess_rate, 20.0, 0.001);

    // Unrated PUSH on p_screw: E_screw = 0 -> throttles split_rec to 50% (30 plate, 20 screw, 0 excess)
    graph->setPortConstraintMode(p_screw->id, ConstraintMode::PUSH);
    auto res3b = solver->solve(*graph);
    ASSERT_EQ(res3b.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_plate->rate, 30.0, 0.001);
    EXPECT_NEAR(p_screw->rate, 20.0, 0.001);
    EXPECT_NEAR(p_screw->excess_rate, 0.0, 0.001);

    // 4. Unrated PUSH feeding a 2-way splitter into two screw_user nodes (capped at 10 each):
    uint64_t n_user2 = addNode("screw_user");
    p_plate = getPort(n_split, "iron_plate", false);
    p_screw = getPort(n_split, "screw", false);
    p_user_in = getPort(n_user, "screw", true);
    Port* p_user2_in = getPort(n_user2, "screw", true);
    addConnection(p_screw, p_user2_in);
    graph->setPortConstraint(p_user_in->id, 10.0, ConstraintMode::LIMIT);
    graph->setPortConstraint(p_user2_in->id, 10.0, ConstraintMode::LIMIT);

    auto res4 = solver->solve(*graph);
    ASSERT_EQ(res4.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_plate->rate, 30.0, 0.001);
    EXPECT_NEAR(p_screw->rate, 20.0, 0.001);
    EXPECT_NEAR(p_user_in->rate, 10.0, 0.001);
    EXPECT_NEAR(p_user2_in->rate, 10.0, 0.001);
    EXPECT_NEAR(p_screw->excess_rate, 0.0, 0.001);
}

// ============================================================================
// Explicit Scenario Table Coverage (docs/solver_v3_rework_plan.md §6)
// ============================================================================

TEST_F(FactorySolverTest, V3_SpecTable_Scenarios1_2_3a_3b_FoundryWithByproductAndGreenhouse) {
    // Setup from §6: Foundry: 1 Iron + 1 Coal -> 1 Steel + 1 CO2, Iron Miner (240) and Coal Miner (30).
    // Greenhouse: 1 CO2 -> 1 Wood.
    testGameData.resources["coal"] = Resource{"Coal", 10};
    testGameData.resources["steel"] = Resource{"Steel", 11};
    testGameData.resources["co2"] = Resource{"CO2", 12};
    testGameData.resources["wood"] = Resource{"Wood", 13};

    Recipe mine_coal;
    mine_coal.name = "Mine Coal";
    mine_coal.produced_in_machines_keys.push_back("miner");
    mine_coal.input_ports.push_back(RecipePort{1.0, "nothing"});
    mine_coal.output_ports.push_back(RecipePort{30.0, "coal"});
    mine_coal.time_seconds = 1.0;
    testGameData.recipes["mine_coal"] = mine_coal;

    Recipe foundry_steel;
    foundry_steel.name = "Foundry Steel";
    foundry_steel.produced_in_machines_keys.push_back("assembler");
    foundry_steel.input_ports.push_back(RecipePort{1.0, "iron_ore"});
    foundry_steel.input_ports.push_back(RecipePort{1.0, "coal"});
    foundry_steel.output_ports.push_back(RecipePort{1.0, "steel"});
    foundry_steel.output_ports.push_back(RecipePort{1.0, "co2"});
    foundry_steel.time_seconds = 1.0;
    testGameData.recipes["foundry_steel"] = foundry_steel;

    Recipe greenhouse_wood;
    greenhouse_wood.name = "Greenhouse Wood";
    greenhouse_wood.produced_in_machines_keys.push_back("constructor");
    greenhouse_wood.input_ports.push_back(RecipePort{1.0, "co2"});
    greenhouse_wood.output_ports.push_back(RecipePort{1.0, "wood"});
    greenhouse_wood.time_seconds = 1.0;
    testGameData.recipes["greenhouse_wood"] = greenhouse_wood;

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_iron = addNode("mine_ore");
    uint64_t n_coal = addNode("mine_coal");
    uint64_t n_foundry = addNode("foundry_steel");

    Port* p_iron_out = getPort(n_iron, "iron_ore", false);
    Port* p_coal_out = getPort(n_coal, "coal", false);
    Port* p_foundry_iron = getPort(n_foundry, "iron_ore", true);
    Port* p_foundry_coal = getPort(n_foundry, "coal", true);
    Port* p_steel_out = getPort(n_foundry, "steel", false);
    Port* p_co2_out = getPort(n_foundry, "co2", false);

    addConnection(p_iron_out, p_foundry_iron);
    addConnection(p_coal_out, p_foundry_coal);

    graph->setPortConstraint(p_iron_out->id, 240.0);
    graph->setPortConstraint(p_coal_out->id, 30.0);

    // --- Scenario 1: Pure Supply (Push) ---
    auto res1 = solver->solve(*graph);
    ASSERT_EQ(res1.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_iron_out->id), ConstraintMode::PUSH);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_coal_out->id), ConstraintMode::PUSH);
    EXPECT_NEAR(p_iron_out->rate, 30.0, 0.001);
    EXPECT_NEAR(p_coal_out->rate, 30.0, 0.001);
    EXPECT_NEAR(p_iron_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_coal_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(solver->getGoalSaturation(p_iron_out->id), 0.125, 1e-4);
    EXPECT_TRUE(solver->isGoalBottlenecked(p_iron_out->id));
    EXPECT_NEAR(solver->getGoalSaturation(p_coal_out->id), 1.0, 1e-4);
    EXPECT_FALSE(solver->isGoalBottlenecked(p_coal_out->id));
    EXPECT_NEAR(p_steel_out->rate, 30.0, 0.001);
    EXPECT_NEAR(p_co2_out->rate, 30.0, 0.001);

    // --- Scenario 2: Pure Demand (Pull): Target(Steel) = 10, CO2 unwired ---
    graph->setPortConstraint(p_steel_out->id, 10.0);
    auto res2 = solver->solve(*graph);
    ASSERT_EQ(res2.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_steel_out->id), ConstraintMode::TARGET);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_iron_out->id), ConstraintMode::LIMIT);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_coal_out->id), ConstraintMode::LIMIT);
    EXPECT_NEAR(p_steel_out->rate, 10.0, 0.001);
    EXPECT_NEAR(p_co2_out->rate, 10.0, 0.001);
    EXPECT_NEAR(p_iron_out->rate, 10.0, 0.001);
    EXPECT_NEAR(p_coal_out->rate, 10.0, 0.001);

    // --- Scenario 3a: Byproduct to Converter (Default AUTO), unconstrained then capped at LIMIT 4 ---
    uint64_t n_green = addNode("greenhouse_wood");
    p_co2_out = getPort(n_foundry, "co2", false);
    p_steel_out = getPort(n_foundry, "steel", false);
    Port* p_green_in = getPort(n_green, "co2", true);
    Port* p_wood_out = getPort(n_green, "wood", false);
    addConnection(p_co2_out, p_green_in);

    auto res3a_uncapped = solver->solve(*graph);
    ASSERT_EQ(res3a_uncapped.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_steel_out->rate, 10.0, 0.001);
    EXPECT_NEAR(p_co2_out->rate, 10.0, 0.001);
    EXPECT_NEAR(p_co2_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_wood_out->rate, 10.0, 0.001);

    // Cap Greenhouse input at LIMIT 4: Greenhouse runs at 4, CO2 shows 6 excess without throttling Steel = 10
    graph->setPortConstraint(p_green_in->id, 4.0);
    auto res3a_capped = solver->solve(*graph);
    ASSERT_EQ(res3a_capped.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_steel_out->rate, 10.0, 0.001);
    EXPECT_NEAR(p_co2_out->rate, 10.0, 0.001);
    EXPECT_NEAR(p_co2_out->excess_rate, 6.0, 0.001);
    EXPECT_NEAR(p_wood_out->rate, 4.0, 0.001);

    // --- Scenario 3b: Byproduct to Converter (Explicit Unrated PUSH on CO2) ---
    graph->setPortConstraint(p_co2_out->id, -1.0, ConstraintMode::PUSH);
    auto res3b = solver->solve(*graph);
    ASSERT_EQ(res3b.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_steel_out->rate, 4.0, 0.001);
    EXPECT_NEAR(p_co2_out->rate, 4.0, 0.001);
    EXPECT_NEAR(p_co2_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_wood_out->rate, 4.0, 0.001);
    EXPECT_NEAR(solver->getGoalSaturation(p_steel_out->id), 0.4, 1e-4);
    EXPECT_TRUE(solver->isGoalBottlenecked(p_steel_out->id));
}

TEST_F(FactorySolverTest, V3_SpecTable_Scenario4_ByproductToMultiInputGeneratorWithDedicatedWaterExtractor) {
    // Target(Plastic) = 30 produces 30 Coke wired to Coal Generator (1 Coke + 2 Water -> nothing),
    // also fed by dedicated Water Extractor (Limit 120 in AUTO mode).
    testGameData.resources["oil"] = Resource{"Oil", 14};
    testGameData.resources["plastic"] = Resource{"Plastic", 15};
    testGameData.resources["coke"] = Resource{"Coke", 16};
    testGameData.resources["water"] = Resource{"Water", 17};

    Recipe extract_oil;
    extract_oil.name = "Extract Oil";
    extract_oil.produced_in_machines_keys.push_back("miner");
    extract_oil.input_ports.push_back(RecipePort{1.0, "nothing"});
    extract_oil.output_ports.push_back(RecipePort{30.0, "oil"});
    extract_oil.time_seconds = 1.0;
    testGameData.recipes["extract_oil"] = extract_oil;

    Recipe extract_water;
    extract_water.name = "Extract Water";
    extract_water.produced_in_machines_keys.push_back("miner");
    extract_water.input_ports.push_back(RecipePort{1.0, "nothing"});
    extract_water.output_ports.push_back(RecipePort{120.0, "water"});
    extract_water.time_seconds = 1.0;
    testGameData.recipes["extract_water"] = extract_water;

    Recipe make_plastic_coke;
    make_plastic_coke.name = "Plastic & Coke";
    make_plastic_coke.produced_in_machines_keys.push_back("assembler");
    make_plastic_coke.input_ports.push_back(RecipePort{30.0, "oil"});
    make_plastic_coke.output_ports.push_back(RecipePort{30.0, "plastic"});
    make_plastic_coke.output_ports.push_back(RecipePort{30.0, "coke"});
    make_plastic_coke.time_seconds = 1.0;
    testGameData.recipes["make_plastic_coke"] = make_plastic_coke;

    Recipe coal_gen;
    coal_gen.name = "Coal Generator";
    coal_gen.produced_in_machines_keys.push_back("sink");
    coal_gen.input_ports.push_back(RecipePort{15.0, "coke"});
    coal_gen.input_ports.push_back(RecipePort{45.0, "water"});
    coal_gen.output_ports.push_back(RecipePort{1.0, "nothing"});
    coal_gen.time_seconds = 1.0;
    testGameData.recipes["coal_gen"] = coal_gen;

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_oil = addNode("extract_oil");
    uint64_t n_ref = addNode("make_plastic_coke");
    uint64_t n_water = addNode("extract_water");
    uint64_t n_gen = addNode("coal_gen");

    Port* p_oil_out = getPort(n_oil, "oil", false);
    Port* p_ref_oil = getPort(n_ref, "oil", true);
    Port* p_plastic_out = getPort(n_ref, "plastic", false);
    Port* p_coke_out = getPort(n_ref, "coke", false);
    Port* p_water_out = getPort(n_water, "water", false);
    Port* p_gen_coke = getPort(n_gen, "coke", true);
    Port* p_gen_water = getPort(n_gen, "water", true);

    addConnection(p_oil_out, p_ref_oil);
    addConnection(p_coke_out, p_gen_coke);
    addConnection(p_water_out, p_gen_water);

    graph->setPortConstraint(p_plastic_out->id, 30.0); // Target(Plastic) = 30
    graph->setPortConstraint(p_water_out->id, 120.0);  // Water Extractor (Limit 120 in AUTO)

    auto res = solver->solve(*graph);
    ASSERT_EQ(res.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_plastic_out->id), ConstraintMode::TARGET);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_water_out->id), ConstraintMode::LIMIT);
    EXPECT_NEAR(p_plastic_out->rate, 30.0, 0.001);
    EXPECT_NEAR(p_coke_out->rate, 30.0, 0.001);
    EXPECT_NEAR(p_coke_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_gen_coke->rate, 30.0, 0.001);
    EXPECT_NEAR(p_gen_water->rate, 90.0, 0.001);
    EXPECT_NEAR(p_water_out->rate, 90.0, 0.001);
    EXPECT_NEAR(p_water_out->excess_rate, 0.0, 0.001);
}

TEST_F(FactorySolverTest, V3_SpecTable_Scenarios6a_6b_7_8_SharedMinerDownstreamClogAndSupplyCycle) {
    testGameData.resources["coal"] = Resource{"Coal", 10};
    testGameData.resources["steel"] = Resource{"Steel", 11};
    testGameData.resources["co2"] = Resource{"CO2", 12};

    Recipe mine_coal;
    mine_coal.name = "Mine Coal";
    mine_coal.produced_in_machines_keys.push_back("miner");
    mine_coal.input_ports.push_back(RecipePort{1.0, "nothing"});
    mine_coal.output_ports.push_back(RecipePort{30.0, "coal"});
    mine_coal.time_seconds = 1.0;
    testGameData.recipes["mine_coal"] = mine_coal;

    Recipe foundry_steel;
    foundry_steel.name = "Foundry Steel";
    foundry_steel.produced_in_machines_keys.push_back("assembler");
    foundry_steel.input_ports.push_back(RecipePort{1.0, "iron_ore"});
    foundry_steel.input_ports.push_back(RecipePort{1.0, "coal"});
    foundry_steel.output_ports.push_back(RecipePort{1.0, "steel"});
    foundry_steel.output_ports.push_back(RecipePort{1.0, "co2"});
    foundry_steel.time_seconds = 1.0;
    testGameData.recipes["foundry_steel"] = foundry_steel;

    Recipe coal_gen;
    coal_gen.name = "Coal Generator";
    coal_gen.produced_in_machines_keys.push_back("sink");
    coal_gen.input_ports.push_back(RecipePort{1.0, "coal"});
    coal_gen.output_ports.push_back(RecipePort{1.0, "nothing"});
    coal_gen.time_seconds = 1.0;
    testGameData.recipes["coal_gen"] = coal_gen;

    Recipe steel_constructor;
    steel_constructor.name = "Steel Constructor";
    steel_constructor.produced_in_machines_keys.push_back("constructor");
    steel_constructor.input_ports.push_back(RecipePort{1.0, "steel"});
    steel_constructor.output_ports.push_back(RecipePort{1.0, "iron_plate"});
    steel_constructor.time_seconds = 1.0;
    testGameData.recipes["steel_constructor"] = steel_constructor;

    // --- Scenario 6a & 6b: Coal Miner (60) wired to both Foundry (Target(Steel) = 10) and Coal Generator ---
    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_iron = addNode("mine_ore");
    uint64_t n_coal = addNode("mine_coal");
    uint64_t n_foundry = addNode("foundry_steel");
    uint64_t n_gen = addNode("coal_gen");

    Port* p_iron_out = getPort(n_iron, "iron_ore", false);
    Port* p_coal_out = getPort(n_coal, "coal", false);
    Port* p_foundry_iron = getPort(n_foundry, "iron_ore", true);
    Port* p_foundry_coal = getPort(n_foundry, "coal", true);
    Port* p_steel_out = getPort(n_foundry, "steel", false);
    Port* p_gen_coal = getPort(n_gen, "coal", true);

    addConnection(p_iron_out, p_foundry_iron);
    addConnection(p_coal_out, p_foundry_coal);
    addConnection(p_coal_out, p_gen_coal);

    graph->setPortConstraint(p_coal_out->id, 60.0, ConstraintMode::AUTO);
    graph->setPortConstraint(p_steel_out->id, 10.0, ConstraintMode::AUTO);

    // 6a: AUTO (LIMIT 60) -> Mines 10 Coal for Foundry, Coal Generator = 0
    auto res6a = solver->solve(*graph);
    ASSERT_EQ(res6a.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_coal_out->id), ConstraintMode::LIMIT);
    EXPECT_NEAR(p_coal_out->rate, 10.0, 0.001);
    EXPECT_NEAR(p_foundry_coal->rate, 10.0, 0.001);
    EXPECT_NEAR(p_gen_coal->rate, 0.0, 0.001);

    // 6b: Explicit PUSH 60 -> Foundry takes 10 Coal, remaining 50 Coal pushes into Coal Generator
    graph->setPortConstraint(p_coal_out->id, 60.0, ConstraintMode::PUSH);
    auto res6b = solver->solve(*graph);
    ASSERT_EQ(res6b.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_coal_out->id), ConstraintMode::PUSH);
    EXPECT_NEAR(p_coal_out->rate, 60.0, 0.001);
    EXPECT_NEAR(p_coal_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_steel_out->rate, 10.0, 0.001);
    EXPECT_NEAR(p_foundry_coal->rate, 10.0, 0.001);
    EXPECT_NEAR(p_gen_coal->rate, 50.0, 0.001);

    // --- Scenario 7: Supply Mode With Downstream Clog (LIMIT 10 on Constructor input) ---
    graph = std::make_unique<FactoryGraph>(testGameData);
    n_iron = addNode("mine_ore");
    n_coal = addNode("mine_coal");
    n_foundry = addNode("foundry_steel");
    uint64_t n_const = addNode("steel_constructor");

    p_iron_out = getPort(n_iron, "iron_ore", false);
    p_coal_out = getPort(n_coal, "coal", false);
    p_foundry_iron = getPort(n_foundry, "iron_ore", true);
    p_foundry_coal = getPort(n_foundry, "coal", true);
    p_steel_out = getPort(n_foundry, "steel", false);
    Port* p_const_in = getPort(n_const, "steel", true);

    addConnection(p_iron_out, p_foundry_iron);
    addConnection(p_coal_out, p_foundry_coal);
    addConnection(p_steel_out, p_const_in);

    graph->setPortConstraint(p_iron_out->id, 240.0);
    graph->setPortConstraint(p_coal_out->id, 30.0);
    graph->setPortConstraint(p_const_in->id, 10.0);

    auto res7 = solver->solve(*graph);
    ASSERT_EQ(res7.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_steel_out->rate, 10.0, 0.001);
    EXPECT_NEAR(p_steel_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_coal_out->rate, 10.0, 0.001);
    EXPECT_NEAR(p_iron_out->rate, 10.0, 0.001);
    EXPECT_NEAR(solver->getGoalSaturation(p_coal_out->id), 10.0 / 30.0, 1e-4);
    EXPECT_NEAR(solver->getGoalSaturation(p_iron_out->id), 10.0 / 240.0, 1e-4);
    EXPECT_TRUE(solver->isGoalBottlenecked(p_coal_out->id));
    EXPECT_TRUE(solver->isGoalBottlenecked(p_iron_out->id));

    // --- Scenario 8: Supply Mode With Amplifying Cycle (gain > 1) and no sink/export exit ---
    Recipe loop_a;
    loop_a.name = "Loop A";
    loop_a.produced_in_machines_keys.push_back("smelter");
    loop_a.input_ports.push_back(RecipePort{1.0, "iron_ore"});
    loop_a.output_ports.push_back(RecipePort{2.0, "iron_ingot"});
    loop_a.time_seconds = 1.0;
    testGameData.recipes["loop_a"] = loop_a;

    Recipe loop_b;
    loop_b.name = "Loop B";
    loop_b.produced_in_machines_keys.push_back("smelter");
    loop_b.input_ports.push_back(RecipePort{1.0, "iron_ingot"});
    loop_b.output_ports.push_back(RecipePort{2.0, "iron_ore"});
    loop_b.time_seconds = 1.0;
    testGameData.recipes["loop_b"] = loop_b;

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_miner8 = addNode("mine_ore");
    uint64_t n_la = addNode("loop_a");
    uint64_t n_lb = addNode("loop_b");
    Port* p_m8_out = getPort(n_miner8, "iron_ore", false);
    Port* p_la_in = getPort(n_la, "iron_ore", true);
    Port* p_la_out = getPort(n_la, "iron_ingot", false);
    Port* p_lb_in = getPort(n_lb, "iron_ingot", true);
    Port* p_lb_out = getPort(n_lb, "iron_ore", false);

    addConnection(p_m8_out, p_la_in);
    addConnection(p_la_out, p_lb_in);
    addConnection(p_lb_out, p_la_in);
    graph->setPortConstraint(p_m8_out->id, 10.0);

    auto res8 = solver->solve(*graph);
    ASSERT_EQ(res8.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_m8_out->id), ConstraintMode::PUSH);
    EXPECT_NEAR(p_m8_out->rate, 0.0, 0.001);
    EXPECT_NEAR(p_la_out->rate, 0.0, 0.001);
    EXPECT_NEAR(p_lb_out->rate, 0.0, 0.001);
    EXPECT_NEAR(solver->getGoalSaturation(p_m8_out->id), 0.0, 1e-4);
    EXPECT_TRUE(solver->isGoalBottlenecked(p_m8_out->id));
}

TEST_F(FactorySolverTest, V3_SpecTable_Scenario9_ByproductRecyclingLoop_AluminumWaterLoop) {
    // Chain needs 100 Water total (Target on final product), produces 30 Water byproduct wired back to input
    // alongside Water Extractor (Limit 1000). Water Extractor pulls only the remaining 70 Water (0 excess).
    testGameData.resources["water"] = Resource{"Water", 17};
    testGameData.resources["alumina"] = Resource{"Alumina", 18};
    testGameData.resources["scrap"] = Resource{"Aluminum Scrap", 19};

    Recipe extract_water;
    extract_water.name = "Extract Water";
    extract_water.produced_in_machines_keys.push_back("miner");
    extract_water.input_ports.push_back(RecipePort{1.0, "nothing"});
    extract_water.output_ports.push_back(RecipePort{100.0, "water"});
    extract_water.time_seconds = 1.0;
    testGameData.recipes["extract_water"] = extract_water;

    Recipe make_alumina;
    make_alumina.name = "Alumina Solution";
    make_alumina.produced_in_machines_keys.push_back("assembler");
    make_alumina.input_ports.push_back(RecipePort{100.0, "water"});
    make_alumina.output_ports.push_back(RecipePort{100.0, "alumina"});
    make_alumina.time_seconds = 1.0;
    testGameData.recipes["make_alumina"] = make_alumina;

    Recipe make_scrap;
    make_scrap.name = "Aluminum Scrap";
    make_scrap.produced_in_machines_keys.push_back("assembler");
    make_scrap.input_ports.push_back(RecipePort{100.0, "alumina"});
    make_scrap.output_ports.push_back(RecipePort{100.0, "scrap"});
    make_scrap.output_ports.push_back(RecipePort{30.0, "water"});
    make_scrap.time_seconds = 1.0;
    testGameData.recipes["make_scrap"] = make_scrap;

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_water = addNode("extract_water");
    uint64_t n_alumina = addNode("make_alumina");
    uint64_t n_scrap = addNode("make_scrap");

    Port* p_water_out = getPort(n_water, "water", false);
    Port* p_alumina_in = getPort(n_alumina, "water", true);
    Port* p_alumina_out = getPort(n_alumina, "alumina", false);
    Port* p_scrap_in = getPort(n_scrap, "alumina", true);
    Port* p_scrap_out = getPort(n_scrap, "scrap", false);
    Port* p_byproduct_water = getPort(n_scrap, "water", false);

    addConnection(p_water_out, p_alumina_in);
    addConnection(p_alumina_out, p_scrap_in);
    addConnection(p_byproduct_water, p_alumina_in);

    graph->setPortConstraint(p_water_out->id, 1000.0);
    graph->setPortConstraint(p_scrap_out->id, 100.0);

    auto res = solver->solve(*graph);
    ASSERT_EQ(res.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_scrap_out->rate, 100.0, 0.001);
    EXPECT_NEAR(p_alumina_in->rate, 100.0, 0.001);
    EXPECT_NEAR(p_byproduct_water->rate, 30.0, 0.001);
    EXPECT_NEAR(p_byproduct_water->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_water_out->rate, 70.0, 0.001);
    EXPECT_NEAR(p_water_out->excess_rate, 0.0, 0.001);
}

TEST_F(FactorySolverTest, V3_SpecTable_Scenario10_ParallelSplitterBranches_10a_10b_10c_And_Stage2c2ExcludesTK) {
    // make_plates: 30 iron_ingot -> 20 iron_plate in 1s (1/30 machines per ingot)
    // make_rods:   15 iron_ingot -> 15 wire in 1s       (1/15 machines per ingot — 2x machine density!)
    Recipe make_rods;
    make_rods.name = "Make Rods";
    make_rods.produced_in_machines_keys.push_back("constructor");
    make_rods.input_ports.push_back(RecipePort{15.0, "iron_ingot"});
    make_rods.output_ports.push_back(RecipePort{15.0, "wire"});
    make_rods.time_seconds = 1.0;
    testGameData.recipes["make_rods"] = make_rods;

    // --- 10a: Supply mode with different machine densities (Plates vs Rods) and with LIMIT 40 ---
    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    uint64_t n_plates = addNode("make_plates");
    uint64_t n_rods = addNode("make_rods");

    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);
    Port* p_plates_in = getPort(n_plates, "iron_ingot", true);
    Port* p_rods_in = getPort(n_rods, "iron_ingot", true);

    addConnection(p_miner_out, p_smelter_in);
    addConnection(p_smelter_out, p_plates_in);
    addConnection(p_smelter_out, p_rods_in);

    // Ore = 60, AUTO with Plates (LIMIT 40) and Rods (unconstrained): splits 30 / 30 despite 2x machine density difference
    graph->setPortConstraint(p_miner_out->id, 60.0);
    graph->setPortConstraint(p_plates_in->id, 40.0);

    auto res10a_60 = solver->solve(*graph);
    ASSERT_EQ(res10a_60.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_plates_in->rate, 30.0, 0.001);
    EXPECT_NEAR(p_rods_in->rate, 30.0, 0.001);

    // Ore = 100, AUTO with Plates (LIMIT 40): splits 40 / 60
    graph->setPortConstraint(p_miner_out->id, 100.0);
    auto res10a_100 = solver->solve(*graph);
    ASSERT_EQ(res10a_100.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_plates_in->rate, 40.0, 0.001);
    EXPECT_NEAR(p_rods_in->rate, 60.0, 0.001);

    // --- 10b: Mixed mode: Ore (PUSH 60) with Branch 1 (TARGET 40) and Branch 2 (unconstrained) splits 40 / 20 ---
    graph->setPortConstraint(p_miner_out->id, 60.0, ConstraintMode::PUSH);
    graph->setPortConstraint(p_plates_in->id, 40.0, ConstraintMode::TARGET);
    auto res10b = solver->solve(*graph);
    ASSERT_EQ(res10b.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_plates_in->rate, 40.0, 0.001);
    EXPECT_NEAR(p_rods_in->rate, 20.0, 0.001);

    // --- 10b (Stage 2c.2 excludes T_K wires):
    // Smelter (120 Ingots from Miner PUSH 120) splits 4 ways:
    //  - Target Branch 1 (in T_K, TARGET = 10 ingots)
    //  - Target Branch 2 (in T_K, TARGET = 50 ingots)
    //  - Unconstrained Plates (not in T_K, 1/30 machines/ingot)
    //  - Unconstrained Rods   (not in T_K, 1/15 machines/ingot)
    // Because Stage 2c.2 excludes T_K wires, the remaining 60 ingots must split 30 / 30 between Plates and Rods!
    uint64_t n_t1 = addNode("make_plates");
    uint64_t n_t2 = addNode("make_plates");
    p_miner_out = getPort(n_miner, "iron_ore", false);
    p_smelter_out = getPort(n_smelter, "iron_ingot", false);
    p_plates_in = getPort(n_plates, "iron_ingot", true);
    p_rods_in = getPort(n_rods, "iron_ingot", true);
    Port* p_t1_in = getPort(n_t1, "iron_ingot", true);
    Port* p_t2_in = getPort(n_t2, "iron_ingot", true);

    addConnection(p_smelter_out, p_t1_in);
    addConnection(p_smelter_out, p_t2_in);

    graph->setPortConstraint(p_miner_out->id, 120.0, ConstraintMode::PUSH);
    graph->setPortConstraint(p_t1_in->id, 10.0, ConstraintMode::TARGET);
    graph->setPortConstraint(p_t2_in->id, 50.0, ConstraintMode::TARGET);
    graph->setPortConstraint(p_plates_in->id, -1.0, ConstraintMode::AUTO);
    graph->setPortConstraint(p_rods_in->id, -1.0, ConstraintMode::AUTO);

    auto res10b_4way = solver->solve(*graph);
    ASSERT_EQ(res10b_4way.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_t1_in->rate, 10.0, 0.001);
    EXPECT_NEAR(p_t2_in->rate, 50.0, 0.001);
    EXPECT_NEAR(p_plates_in->rate, 30.0, 0.001);
    EXPECT_NEAR(p_rods_in->rate, 30.0, 0.001);

    // --- 10c: 60 on Smelter.out in AUTO mode (Ore -> Smelter (60, AUTO) -> Plates + Rods) ---
    // Locks Smelter = 60 in Stage 2a, pushes all 60 into Plates + Rods in Stage 2b (0 excess),
    // and splits 30 / 30 in Stage 2c.2 despite different machine densities!
    graph = std::make_unique<FactoryGraph>(testGameData);
    n_miner = addNode("mine_ore");
    n_smelter = addNode("smelt_ingot");
    n_plates = addNode("make_plates");
    n_rods = addNode("make_rods");

    p_miner_out = getPort(n_miner, "iron_ore", false);
    p_smelter_in = getPort(n_smelter, "iron_ore", true);
    p_smelter_out = getPort(n_smelter, "iron_ingot", false);
    p_plates_in = getPort(n_plates, "iron_ingot", true);
    p_rods_in = getPort(n_rods, "iron_ingot", true);

    addConnection(p_miner_out, p_smelter_in);
    addConnection(p_smelter_out, p_plates_in);
    addConnection(p_smelter_out, p_rods_in);

    graph->setPortConstraint(p_smelter_out->id, 60.0, ConstraintMode::AUTO);

    auto res10c = solver->solve(*graph);
    ASSERT_EQ(res10c.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_smelter_out->id), ConstraintMode::TARGET);
    EXPECT_NEAR(p_smelter_out->rate, 60.0, 0.001);
    EXPECT_NEAR(p_smelter_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_plates_in->rate, 30.0, 0.001);
    EXPECT_NEAR(p_rods_in->rate, 30.0, 0.001);
}

TEST_F(FactorySolverTest, V3_SpecTable_Scenario11_BlockedDownstreamMultiInputWasteCompression_Factory1) {
    // Scenario 11 (Factory_1.fpp):
    // - Rubber Refinery (in T_K, driven by Target(Fuel) = 50) produces 50 Fuel + 50 Rubber from Crude Oil.
    // - Rubber splits to:
    //   1) Recycled Plastic (in T_K, 30 Rubber -> 60 Plastic with Target(Plastic) = 60, consuming 30 Rubber).
    //   2) Circuit Board (not in T_K, 4 Rubber + 8 Coke -> 1 Circuit Board), wired to Electric Motor
    //      (1 Circuit Board + 4 Rotor -> 4 Motor), where Rotor is produced by Rotor Maker with 6.0 on Rotor.out.
    // - Because Circuit Board is not in T_K, E_CB = 0 is enforced (Rule 2 of §3.3).
    // - Circuit Board runs at 1.5 (consuming 6.0 Rubber and 12.0 Coke for the 6.0 Motors, with 0 excess on CB),
    //   and all remaining 14.0 surplus Rubber stays on Rubber.out as excess_rate = 14.0!
    testGameData.resources["oil"] = Resource{"Crude Oil", 19};
    testGameData.resources["fuel"] = Resource{"Fuel", 20};
    testGameData.resources["rubber"] = Resource{"Rubber", 21};
    testGameData.resources["plastic"] = Resource{"Plastic", 22};
    testGameData.resources["coke"] = Resource{"Coke", 23};
    testGameData.resources["circuit_board"] = Resource{"Circuit Board", 24};
    testGameData.resources["rotor"] = Resource{"Rotor", 25};
    testGameData.resources["motor"] = Resource{"Electric Motor", 26};

    Recipe extract_oil;
    extract_oil.name = "Extract Oil";
    extract_oil.produced_in_machines_keys.push_back("miner");
    extract_oil.input_ports.push_back(RecipePort{1.0, "nothing"});
    extract_oil.output_ports.push_back(RecipePort{50.0, "oil"});
    extract_oil.time_seconds = 1.0;
    testGameData.recipes["extract_oil"] = extract_oil;

    Recipe ref_rubber;
    ref_rubber.name = "Rubber Refinery";
    ref_rubber.produced_in_machines_keys.push_back("assembler");
    ref_rubber.input_ports.push_back(RecipePort{50.0, "oil"});
    ref_rubber.output_ports.push_back(RecipePort{50.0, "fuel"});
    ref_rubber.output_ports.push_back(RecipePort{50.0, "rubber"});
    ref_rubber.time_seconds = 1.0;
    testGameData.recipes["ref_rubber"] = ref_rubber;

    Recipe recycled_plastic;
    recycled_plastic.name = "Recycled Plastic";
    recycled_plastic.produced_in_machines_keys.push_back("assembler");
    recycled_plastic.input_ports.push_back(RecipePort{30.0, "rubber"});
    recycled_plastic.output_ports.push_back(RecipePort{60.0, "plastic"});
    recycled_plastic.time_seconds = 1.0;
    testGameData.recipes["recycled_plastic"] = recycled_plastic;

    Recipe ref_coke;
    ref_coke.name = "Coke Refinery";
    ref_coke.produced_in_machines_keys.push_back("assembler");
    ref_coke.input_ports.push_back(RecipePort{1.0, "nothing"});
    ref_coke.output_ports.push_back(RecipePort{120.0, "coke"});
    ref_coke.time_seconds = 1.0;
    testGameData.recipes["ref_coke"] = ref_coke;

    Recipe make_cb;
    make_cb.name = "Electrode Circuit Board";
    make_cb.produced_in_machines_keys.push_back("assembler");
    make_cb.input_ports.push_back(RecipePort{4.0, "rubber"});
    make_cb.input_ports.push_back(RecipePort{8.0, "coke"});
    make_cb.output_ports.push_back(RecipePort{1.0, "circuit_board"});
    make_cb.time_seconds = 1.0;
    testGameData.recipes["make_cb"] = make_cb;

    Recipe make_rotor;
    make_rotor.name = "Make Rotor";
    make_rotor.produced_in_machines_keys.push_back("constructor");
    make_rotor.input_ports.push_back(RecipePort{6.0, "iron_ingot"});
    make_rotor.output_ports.push_back(RecipePort{6.0, "rotor"});
    make_rotor.time_seconds = 1.0;
    testGameData.recipes["make_rotor"] = make_rotor;

    Recipe make_motor;
    make_motor.name = "Electric Motor";
    make_motor.produced_in_machines_keys.push_back("assembler");
    make_motor.input_ports.push_back(RecipePort{1.0, "circuit_board"});
    make_motor.input_ports.push_back(RecipePort{4.0, "rotor"});
    make_motor.output_ports.push_back(RecipePort{4.0, "motor"});
    make_motor.time_seconds = 1.0;
    testGameData.recipes["make_motor"] = make_motor;

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_oil = addNode("extract_oil");
    uint64_t n_rub = addNode("ref_rubber");
    uint64_t n_plas = addNode("recycled_plastic");
    uint64_t n_coke = addNode("ref_coke");
    uint64_t n_cb = addNode("make_cb");
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    uint64_t n_rotor = addNode("make_rotor");
    uint64_t n_motor = addNode("make_motor");

    Port* p_oil_out = getPort(n_oil, "oil", false);
    Port* p_rub_oil = getPort(n_rub, "oil", true);
    Port* p_fuel_out = getPort(n_rub, "fuel", false);
    Port* p_rub_out = getPort(n_rub, "rubber", false);
    Port* p_plas_in = getPort(n_plas, "rubber", true);
    Port* p_plas_out = getPort(n_plas, "plastic", false);
    Port* p_coke_out = getPort(n_coke, "coke", false);
    Port* p_cb_rub = getPort(n_cb, "rubber", true);
    Port* p_cb_coke = getPort(n_cb, "coke", true);
    Port* p_cb_out = getPort(n_cb, "circuit_board", false);
    Port* p_miner_out = getPort(n_miner, "iron_ore", false);
    Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
    Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);
    Port* p_rotor_in = getPort(n_rotor, "iron_ingot", true);
    Port* p_rotor_out = getPort(n_rotor, "rotor", false);
    Port* p_motor_cb = getPort(n_motor, "circuit_board", true);
    Port* p_motor_rotor = getPort(n_motor, "rotor", true);
    Port* p_motor_out = getPort(n_motor, "motor", false);

    addConnection(p_oil_out, p_rub_oil);
    addConnection(p_rub_out, p_plas_in);
    addConnection(p_rub_out, p_cb_rub);
    addConnection(p_coke_out, p_cb_coke);
    addConnection(p_cb_out, p_motor_cb);
    addConnection(p_miner_out, p_smelter_in);
    addConnection(p_smelter_out, p_rotor_in);
    addConnection(p_rotor_out, p_motor_rotor);

    graph->setPortConstraint(p_fuel_out->id, 50.0);  // Target(Fuel) = 50 -> 50 Rubber produced
    graph->setPortConstraint(p_plas_out->id, 60.0);  // Target(Plastic) = 60 -> 30 Rubber consumed
    graph->setPortConstraint(p_coke_out->id, 120.0); // Coke limit 120
    graph->setPortConstraint(p_rotor_out->id, 6.0);  // Rotor = 6.0 -> caps Motor at 6.0 (1.5 CB = 6.0 Rubber)

    auto res = solver->solve(*graph);
    ASSERT_EQ(res.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_cb_out->rate, 1.5, 0.001);
    EXPECT_NEAR(p_cb_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_cb_rub->rate, 6.0, 0.001);
    EXPECT_NEAR(p_cb_coke->rate, 12.0, 0.001);
    EXPECT_NEAR(p_motor_out->rate, 6.0, 0.001);
    EXPECT_NEAR(p_rub_out->rate, 50.0, 0.001);
    EXPECT_NEAR(p_rub_out->excess_rate, 14.0, 0.001);
}

TEST_F(FactorySolverTest, V3_SpecTable_Scenario5_TwoDisconnectedIslands_FoundryAndSmelter) {
    // §6 Scenario 5:
    // Island A has Foundry with Target(Steel) = 10 (Iron Miner 240, Coal Miner 30).
    // Island B has only Miner (60) -> Smelter.
    // Island A runs in Demand Mode (10 Steel); Island B runs in Supply Mode (Miner -> PUSH 60, producing 60 Ingots).
    testGameData.resources["coal"] = Resource{"Coal", 10};
    testGameData.resources["steel"] = Resource{"Steel", 11};
    testGameData.resources["co2"] = Resource{"CO2", 12};

    Recipe mine_coal;
    mine_coal.name = "Mine Coal";
    mine_coal.produced_in_machines_keys.push_back("miner");
    mine_coal.input_ports.push_back(RecipePort{1.0, "nothing"});
    mine_coal.output_ports.push_back(RecipePort{30.0, "coal"});
    mine_coal.time_seconds = 1.0;
    testGameData.recipes["mine_coal"] = mine_coal;

    Recipe foundry_steel;
    foundry_steel.name = "Foundry Steel";
    foundry_steel.produced_in_machines_keys.push_back("assembler");
    foundry_steel.input_ports.push_back(RecipePort{1.0, "iron_ore"});
    foundry_steel.input_ports.push_back(RecipePort{1.0, "coal"});
    foundry_steel.output_ports.push_back(RecipePort{1.0, "steel"});
    foundry_steel.output_ports.push_back(RecipePort{1.0, "co2"});
    foundry_steel.time_seconds = 1.0;
    testGameData.recipes["foundry_steel"] = foundry_steel;

    graph = std::make_unique<FactoryGraph>(testGameData);
    // Island A & Island B nodes
    uint64_t n_iron_a = addNode("mine_ore");
    uint64_t n_coal_a = addNode("mine_coal");
    uint64_t n_foundry_a = addNode("foundry_steel");
    uint64_t n_miner_b = addNode("mine_ore");
    uint64_t n_smelter_b = addNode("smelt_ingot");

    Port* p_iron_a = getPort(n_iron_a, "iron_ore", false);
    Port* p_coal_a = getPort(n_coal_a, "coal", false);
    Port* p_steel_a = getPort(n_foundry_a, "steel", false);
    addConnection(p_iron_a, getPort(n_foundry_a, "iron_ore", true));
    addConnection(p_coal_a, getPort(n_foundry_a, "coal", true));
    graph->setPortConstraint(p_iron_a->id, 240.0);
    graph->setPortConstraint(p_coal_a->id, 30.0);
    graph->setPortConstraint(p_steel_a->id, 10.0);

    Port* p_miner_b = getPort(n_miner_b, "iron_ore", false);
    Port* p_ingot_b = getPort(n_smelter_b, "iron_ingot", false);
    addConnection(p_miner_b, getPort(n_smelter_b, "iron_ore", true));
    graph->setPortConstraint(p_miner_b->id, 60.0);

    auto res = solver->solve(*graph);
    ASSERT_EQ(res.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_EQ(solver->getResolvedConstraintMode(p_iron_a->id), ConstraintMode::LIMIT);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_coal_a->id), ConstraintMode::LIMIT);
    EXPECT_EQ(solver->getResolvedConstraintMode(p_steel_a->id), ConstraintMode::TARGET);
    EXPECT_NEAR(p_steel_a->rate, 10.0, 0.001);

    EXPECT_EQ(solver->getResolvedConstraintMode(p_miner_b->id), ConstraintMode::PUSH);
    EXPECT_NEAR(p_miner_b->rate, 60.0, 0.001);
    EXPECT_NEAR(p_ingot_b->rate, 60.0, 0.001);
}

TEST_F(FactorySolverTest, V3_ComplexReconvergingUnratedPushByproductChains) {
    // Two TARGET-driven refineries (Ref A and Ref B) each produce a main product and an unrated PUSH byproduct:
    //  - Ref A (in T_K, Target(Plastic) = 20): 20 Oil -> 20 Plastic + 20 Slag
    //    * 5 Slag recycles back into Ref A's Slag input (in T_K: Cl_{~T} stops at T_K so Plastic is not forced to 0 excess)
    //    * 15 Slag goes to a shared Converter (not in T_K: 1 Slag -> 1 Coke) -> Shared Sink (capped at 25 Coke)
    //  - Ref B (in T_K, Target(Rubber) = 20): 20 Oil -> 20 Rubber + 20 Slag
    //    * Slag (unrated PUSH) goes into the same shared Converter -> Shared Sink
    // Because both Ref A.Slag and Ref B.Slag have Unrated PUSH, Cl_{~T} forces E_Slag = 0 and E_Coke = 0 on the Converter,
    // while stopping at Ref A (in T_K).
    // With 5 Slag recycled into Ref A and Shared Sink capped at 25 Coke, total Slag produced by Ref A + Ref B is capped at 5 + 25 = 30.
    // Max-min water-filling in Stage 1 balances Ref A and Ref B at S = 0.75 (15 Plastic + 15 Rubber, 15 + 15 = 30 Slag)!
    testGameData.resources["oil"] = Resource{"Oil", 30};
    testGameData.resources["plastic"] = Resource{"Plastic", 31};
    testGameData.resources["rubber"] = Resource{"Rubber", 32};
    testGameData.resources["slag"] = Resource{"Slag", 33};
    testGameData.resources["coke"] = Resource{"Coke", 34};

    Recipe extract_oil;
    extract_oil.name = "Extract Oil";
    extract_oil.produced_in_machines_keys.push_back("miner");
    extract_oil.input_ports.push_back(RecipePort{1.0, "nothing"});
    extract_oil.output_ports.push_back(RecipePort{100.0, "oil"});
    extract_oil.time_seconds = 1.0;
    testGameData.recipes["extract_oil"] = extract_oil;

    Recipe ref_a;
    ref_a.name = "Refinery A";
    ref_a.produced_in_machines_keys.push_back("assembler");
    ref_a.input_ports.push_back(RecipePort{20.0, "oil"});
    ref_a.output_ports.push_back(RecipePort{20.0, "plastic"});
    ref_a.output_ports.push_back(RecipePort{20.0, "slag"});
    ref_a.time_seconds = 1.0;
    testGameData.recipes["ref_a"] = ref_a;

    Recipe ref_b;
    ref_b.name = "Refinery B";
    ref_b.produced_in_machines_keys.push_back("assembler");
    ref_b.input_ports.push_back(RecipePort{20.0, "oil"});
    ref_b.output_ports.push_back(RecipePort{20.0, "rubber"});
    ref_b.output_ports.push_back(RecipePort{20.0, "slag"});
    ref_b.time_seconds = 1.0;
    testGameData.recipes["ref_b"] = ref_b;

    Recipe slag_to_oil;
    slag_to_oil.name = "Slag Recycle";
    slag_to_oil.produced_in_machines_keys.push_back("constructor");
    slag_to_oil.input_ports.push_back(RecipePort{5.0, "slag"});
    slag_to_oil.output_ports.push_back(RecipePort{5.0, "oil"});
    slag_to_oil.time_seconds = 1.0;
    testGameData.recipes["slag_to_oil"] = slag_to_oil;

    Recipe slag_to_coke;
    slag_to_coke.name = "Slag Converter";
    slag_to_coke.produced_in_machines_keys.push_back("constructor");
    slag_to_coke.input_ports.push_back(RecipePort{1.0, "slag"});
    slag_to_coke.output_ports.push_back(RecipePort{1.0, "coke"});
    slag_to_coke.time_seconds = 1.0;
    testGameData.recipes["slag_to_coke"] = slag_to_coke;

    Recipe sink_coke;
    sink_coke.name = "Sink Coke";
    sink_coke.produced_in_machines_keys.push_back("sink");
    sink_coke.input_ports.push_back(RecipePort{1.0, "coke"});
    sink_coke.output_ports.push_back(RecipePort{1.0, "nothing"});
    sink_coke.time_seconds = 1.0;
    testGameData.recipes["sink_coke"] = sink_coke;

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_oil = addNode("extract_oil");
    uint64_t n_ra = addNode("ref_a");
    uint64_t n_rb = addNode("ref_b");
    uint64_t n_rec = addNode("slag_to_oil");
    uint64_t n_conv = addNode("slag_to_coke");
    uint64_t n_sink = addNode("sink_coke");

    Port* p_oil = getPort(n_oil, "oil", false);
    Port* p_ra_oil = getPort(n_ra, "oil", true);
    Port* p_ra_plas = getPort(n_ra, "plastic", false);
    Port* p_ra_slag = getPort(n_ra, "slag", false);
    Port* p_rb_oil = getPort(n_rb, "oil", true);
    Port* p_rb_rub = getPort(n_rb, "rubber", false);
    Port* p_rb_slag = getPort(n_rb, "slag", false);
    Port* p_rec_in = getPort(n_rec, "slag", true);
    Port* p_rec_out = getPort(n_rec, "oil", false);
    Port* p_conv_in = getPort(n_conv, "slag", true);
    Port* p_conv_out = getPort(n_conv, "coke", false);
    Port* p_sink_in = getPort(n_sink, "coke", true);

    addConnection(p_oil, p_ra_oil);
    addConnection(p_oil, p_rb_oil);
    addConnection(p_ra_slag, p_rec_in);
    addConnection(p_rec_out, p_ra_oil);
    addConnection(p_ra_slag, p_conv_in);
    addConnection(p_rb_slag, p_conv_in);
    addConnection(p_conv_out, p_sink_in);

    graph->setPortConstraint(p_ra_plas->id, 20.0, ConstraintMode::TARGET);
    graph->setPortConstraint(p_rb_rub->id, 20.0, ConstraintMode::TARGET);
    graph->setPortConstraint(p_ra_slag->id, -1.0, ConstraintMode::PUSH); // Unrated PUSH
    graph->setPortConstraint(p_rb_slag->id, -1.0, ConstraintMode::PUSH); // Unrated PUSH
    graph->setPortConstraint(p_rec_in->id, 5.0, ConstraintMode::LIMIT);  // Max 5 Slag recycled
    graph->setPortConstraint(p_sink_in->id, 25.0, ConstraintMode::LIMIT); // Max 25 Coke sunk

    auto res = solver->solve(*graph);
    ASSERT_EQ(res.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_ra_plas->rate, 15.0, 0.001);
    EXPECT_NEAR(p_rb_rub->rate, 15.0, 0.001);
    EXPECT_NEAR(p_ra_slag->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_rb_slag->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_conv_out->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(p_rec_in->rate, 5.0, 0.001);
    EXPECT_NEAR(p_sink_in->rate, 25.0, 0.001);
    EXPECT_NEAR(solver->getGoalSaturation(p_ra_plas->id), 0.75, 1e-4);
    EXPECT_NEAR(solver->getGoalSaturation(p_rb_rub->id), 0.75, 1e-4);
}

