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

TEST_F(FactorySolverTest, ComputeNodeDepths_EmptyAndSingleNode) {
    auto depths = solver->computeNodeDepths(*graph);
    EXPECT_TRUE(depths.empty());

    uint64_t n_miner = addNode("mine_ore");
    depths = solver->computeNodeDepths(*graph);
    ASSERT_EQ(depths.size(), 1);
    EXPECT_EQ(depths[n_miner], 0);
}

TEST_F(FactorySolverTest, ComputeNodeDepths_LinearChain) {
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    uint64_t n_plates = addNode("make_plates");

    addConnection(getPort(n_miner, "iron_ore", false), getPort(n_smelter, "iron_ore", true));
    addConnection(getPort(n_smelter, "iron_ingot", false), getPort(n_plates, "iron_ingot", true));

    auto depths = solver->computeNodeDepths(*graph);
    EXPECT_EQ(depths[n_miner], 0);
    EXPECT_EQ(depths[n_smelter], 1);
    EXPECT_EQ(depths[n_plates], 2);
}

TEST_F(FactorySolverTest, ComputeNodeDepths_DiamondGraph) {
    uint64_t n_miner = addNode("mine_ore");
    uint64_t n_smelter = addNode("smelt_ingot");
    uint64_t n_plates = addNode("make_plates");
    uint64_t n_screws = addNode("make_screws");
    uint64_t n_reinf = addNode("make_reinforced");

    addConnection(getPort(n_miner, "iron_ore", false), getPort(n_smelter, "iron_ore", true));
    addConnection(getPort(n_smelter, "iron_ingot", false), getPort(n_plates, "iron_ingot", true));
    addConnection(getPort(n_smelter, "iron_ingot", false), getPort(n_screws, "iron_ingot", true));
    addConnection(getPort(n_plates, "iron_plate", false), getPort(n_reinf, "iron_plate", true));
    addConnection(getPort(n_screws, "screw", false), getPort(n_reinf, "screw", true));

    auto depths = solver->computeNodeDepths(*graph);
    EXPECT_EQ(depths[n_miner], 0);
    EXPECT_EQ(depths[n_smelter], 1);
    EXPECT_EQ(depths[n_plates], 2);
    EXPECT_EQ(depths[n_screws], 2);
    EXPECT_EQ(depths[n_reinf], 3);
}

TEST_F(FactorySolverTest, ComputeNodeDepths_Cycle) {
    Recipe r1;
    r1.name = "Cycle Recipe 1";
    r1.input_ports.push_back(RecipePort{10.0, "iron_ingot"});
    r1.output_ports.push_back(RecipePort{10.0, "screw"});
    r1.time_seconds = 1.0;
    testGameData.recipes["cycle1"] = r1;

    Recipe r2;
    r2.name = "Cycle Recipe 2";
    r2.input_ports.push_back(RecipePort{10.0, "screw"});
    r2.output_ports.push_back(RecipePort{10.0, "iron_ingot"});
    r2.time_seconds = 1.0;
    testGameData.recipes["cycle2"] = r2;

    graph = std::make_unique<FactoryGraph>(testGameData);

    uint64_t n_c1 = graph->addNode("Cycle 1", "cycle1");
    uint64_t n_c2 = graph->addNode("Cycle 2", "cycle2");

    Port* c1_out = getPort(n_c1, "screw", false);
    Port* c2_in = getPort(n_c2, "screw", true);
    Port* c2_out = getPort(n_c2, "iron_ingot", false);
    Port* c1_in = getPort(n_c1, "iron_ingot", true);

    addConnection(c1_out, c2_in);
    addConnection(c2_out, c1_in);

    auto depths = solver->computeNodeDepths(*graph);
    EXPECT_EQ(depths[n_c1], 0);
    EXPECT_EQ(depths[n_c2], 0);
}

TEST_F(FactorySolverTest, ComputeNodeDepths_CycleWithRootAndLeaf) {
    Recipe r_root;
    r_root.name = "Root Recipe";
    r_root.input_ports.push_back(RecipePort{1.0, "nothing"});
    r_root.output_ports.push_back(RecipePort{10.0, "iron_ingot"});
    r_root.time_seconds = 1.0;
    testGameData.recipes["r_root"] = r_root;

    Recipe r1;
    r1.name = "C1";
    r1.input_ports.push_back(RecipePort{10.0, "iron_ingot"});
    r1.output_ports.push_back(RecipePort{10.0, "screw"});
    r1.time_seconds = 1.0;
    testGameData.recipes["c1"] = r1;

    Recipe r2;
    r2.name = "C2";
    r2.input_ports.push_back(RecipePort{10.0, "screw"});
    r2.output_ports.push_back(RecipePort{10.0, "iron_ingot"});
    r2.output_ports.push_back(RecipePort{10.0, "wire"});
    r2.time_seconds = 1.0;
    testGameData.recipes["c2"] = r2;

    Recipe r_leaf;
    r_leaf.name = "Leaf Recipe";
    r_leaf.input_ports.push_back(RecipePort{10.0, "wire"});
    r_leaf.output_ports.push_back(RecipePort{1.0, "nothing"});
    r_leaf.time_seconds = 1.0;
    testGameData.recipes["r_leaf"] = r_leaf;

    graph = std::make_unique<FactoryGraph>(testGameData);

    uint64_t n_root = graph->addNode("Root", "r_root");
    uint64_t n_c1 = graph->addNode("Cycle1", "c1");
    uint64_t n_c2 = graph->addNode("Cycle2", "c2");
    uint64_t n_leaf = graph->addNode("Leaf", "r_leaf");

    addConnection(getPort(n_root, "iron_ingot", false), getPort(n_c1, "iron_ingot", true));
    addConnection(getPort(n_c1, "screw", false), getPort(n_c2, "screw", true));
    addConnection(getPort(n_c2, "iron_ingot", false), getPort(n_c1, "iron_ingot", true));
    addConnection(getPort(n_c2, "wire", false), getPort(n_leaf, "wire", true));

    auto depths = solver->computeNodeDepths(*graph);
    EXPECT_EQ(depths[n_root], 0);
    EXPECT_EQ(depths[n_c1], 1);
    EXPECT_EQ(depths[n_c2], 1);
    EXPECT_EQ(depths[n_leaf], 2);
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

TEST_F(FactorySolverTest, ComputeNodeDepths_DeepChain) {
    // 1000 nodes in a linear chain
    Recipe chain_step;
    chain_step.name = "Step";
    chain_step.produced_in_machines_keys.push_back("constructor");
    chain_step.input_ports.push_back(RecipePort{1.0, "iron_ingot"});
    chain_step.output_ports.push_back(RecipePort{1.0, "iron_ingot"});
    chain_step.time_seconds = 1.0;
    testGameData.recipes["chain_step"] = chain_step;

    graph = std::make_unique<FactoryGraph>(testGameData);
    const int kChainLen = 1000;
    std::vector<uint64_t> chain_nodes;
    chain_nodes.reserve(kChainLen);

    for (int i = 0; i < kChainLen; ++i) {
        chain_nodes.push_back(graph->addNode("Step_" + std::to_string(i), "chain_step"));
    }

    for (int i = 0; i < kChainLen - 1; ++i) {
        Node* curr = graph->getNode(chain_nodes[i]);
        Node* next = graph->getNode(chain_nodes[i + 1]);
        ASSERT_NE(curr, nullptr);
        ASSERT_NE(next, nullptr);
        addConnection(graph->getPort(curr->output_ports[0]), graph->getPort(next->input_ports[0]));
    }

    auto depths = solver->computeNodeDepths(*graph);
    ASSERT_EQ(depths.size(), static_cast<size_t>(kChainLen));
    EXPECT_EQ(depths[chain_nodes[0]], 0);
    EXPECT_EQ(depths[chain_nodes[500]], 500);
    EXPECT_EQ(depths[chain_nodes[kChainLen - 1]], kChainLen - 1);
}

TEST_F(FactorySolverTest, ComputeNodeDepths_ComplexSCCCondensationWithBypasses) {
    // S -> Cycle1 -> Cycle2 -> Cycle3 -> T
    // With parallel bypasses: S -> Cycle2, Cycle1 -> Cycle3, and S -> T
    Recipe r_source;
    r_source.name = "Source";
    r_source.input_ports.push_back(RecipePort{1.0, "nothing"});
    r_source.output_ports.push_back(RecipePort{10.0, "iron_ingot"});
    r_source.output_ports.push_back(RecipePort{10.0, "copper_ingot"});
    r_source.output_ports.push_back(RecipePort{10.0, "screw"});
    r_source.time_seconds = 1.0;
    testGameData.recipes["r_source"] = r_source;

    Recipe r_c1_a;
    r_c1_a.name = "C1_A";
    r_c1_a.input_ports.push_back(RecipePort{10.0, "iron_ingot"});
    r_c1_a.input_ports.push_back(RecipePort{10.0, "wire"});
    r_c1_a.output_ports.push_back(RecipePort{10.0, "iron_plate"});
    r_c1_a.time_seconds = 1.0;
    testGameData.recipes["r_c1_a"] = r_c1_a;

    Recipe r_c1_b;
    r_c1_b.name = "C1_B";
    r_c1_b.input_ports.push_back(RecipePort{10.0, "iron_plate"});
    r_c1_b.output_ports.push_back(RecipePort{10.0, "wire"});
    r_c1_b.output_ports.push_back(RecipePort{10.0, "copper_ore"});
    r_c1_b.output_ports.push_back(RecipePort{10.0, "iron_ore"});
    r_c1_b.time_seconds = 1.0;
    testGameData.recipes["r_c1_b"] = r_c1_b;

    Recipe r_c2_a;
    r_c2_a.name = "C2_A";
    r_c2_a.input_ports.push_back(RecipePort{10.0, "copper_ore"});
    r_c2_a.input_ports.push_back(RecipePort{10.0, "copper_ingot"});
    r_c2_a.input_ports.push_back(RecipePort{10.0, "reinforced_plate"});
    r_c2_a.output_ports.push_back(RecipePort{10.0, "screw"});
    r_c2_a.time_seconds = 1.0;
    testGameData.recipes["r_c2_a"] = r_c2_a;

    Recipe r_c2_b;
    r_c2_b.name = "C2_B";
    r_c2_b.input_ports.push_back(RecipePort{10.0, "screw"});
    r_c2_b.output_ports.push_back(RecipePort{10.0, "reinforced_plate"});
    r_c2_b.output_ports.push_back(RecipePort{10.0, "iron_ingot"});
    r_c2_b.time_seconds = 1.0;
    testGameData.recipes["r_c2_b"] = r_c2_b;

    Recipe r_c3_a;
    r_c3_a.name = "C3_A";
    r_c3_a.input_ports.push_back(RecipePort{10.0, "iron_ingot"});
    r_c3_a.input_ports.push_back(RecipePort{10.0, "iron_ore"});
    r_c3_a.input_ports.push_back(RecipePort{10.0, "wire"});
    r_c3_a.output_ports.push_back(RecipePort{10.0, "copper_ingot"});
    r_c3_a.time_seconds = 1.0;
    testGameData.recipes["r_c3_a"] = r_c3_a;

    Recipe r_c3_b;
    r_c3_b.name = "C3_B";
    r_c3_b.input_ports.push_back(RecipePort{10.0, "copper_ingot"});
    r_c3_b.output_ports.push_back(RecipePort{10.0, "wire"});
    r_c3_b.output_ports.push_back(RecipePort{10.0, "screw"});
    r_c3_b.time_seconds = 1.0;
    testGameData.recipes["r_c3_b"] = r_c3_b;

    Recipe r_sink;
    r_sink.name = "Sink";
    r_sink.input_ports.push_back(RecipePort{10.0, "screw"});
    r_sink.output_ports.push_back(RecipePort{1.0, "nothing"});
    r_sink.time_seconds = 1.0;
    testGameData.recipes["r_sink"] = r_sink;

    graph = std::make_unique<FactoryGraph>(testGameData);

    uint64_t n_source = graph->addNode("Source", "r_source");
    uint64_t n_c1_a = graph->addNode("C1_A", "r_c1_a");
    uint64_t n_c1_b = graph->addNode("C1_B", "r_c1_b");
    uint64_t n_c2_a = graph->addNode("C2_A", "r_c2_a");
    uint64_t n_c2_b = graph->addNode("C2_B", "r_c2_b");
    uint64_t n_c3_a = graph->addNode("C3_A", "r_c3_a");
    uint64_t n_c3_b = graph->addNode("C3_B", "r_c3_b");
    uint64_t n_sink = graph->addNode("Sink", "r_sink");

    // Cycle 1 internal edges
    addConnection(getPort(n_c1_a, "iron_plate", false), getPort(n_c1_b, "iron_plate", true));
    addConnection(getPort(n_c1_b, "wire", false), getPort(n_c1_a, "wire", true));

    // Cycle 2 internal edges
    addConnection(getPort(n_c2_a, "screw", false), getPort(n_c2_b, "screw", true));
    addConnection(getPort(n_c2_b, "reinforced_plate", false), getPort(n_c2_a, "reinforced_plate", true));

    // Cycle 3 internal edges
    addConnection(getPort(n_c3_a, "copper_ingot", false), getPort(n_c3_b, "copper_ingot", true));
    addConnection(getPort(n_c3_b, "wire", false), getPort(n_c3_a, "wire", true));

    // Feed S -> C1
    addConnection(getPort(n_source, "iron_ingot", false), getPort(n_c1_a, "iron_ingot", true));

    // Bypass 1: S -> C2
    addConnection(getPort(n_source, "copper_ingot", false), getPort(n_c2_a, "copper_ingot", true));

    // Feed C1 -> C2
    addConnection(getPort(n_c1_b, "copper_ore", false), getPort(n_c2_a, "copper_ore", true));

    // Feed C2 -> C3
    addConnection(getPort(n_c2_b, "iron_ingot", false), getPort(n_c3_a, "iron_ingot", true));

    // Bypass 2: C1 -> C3
    addConnection(getPort(n_c1_b, "iron_ore", false), getPort(n_c3_a, "iron_ore", true));

    // Feed C3 -> Sink
    addConnection(getPort(n_c3_b, "screw", false), getPort(n_sink, "screw", true));

    auto depths = solver->computeNodeDepths(*graph);
    EXPECT_EQ(depths[n_source], 0);
    EXPECT_EQ(depths[n_c1_a], 1);
    EXPECT_EQ(depths[n_c1_b], 1);
    EXPECT_EQ(depths[n_c2_a], 2);
    EXPECT_EQ(depths[n_c2_b], 2);
    EXPECT_EQ(depths[n_c3_a], 3);
    EXPECT_EQ(depths[n_c3_b], 3);
    EXPECT_EQ(depths[n_sink], 4);
}


#include <fstream>
#include "core/data/GameDataManager.h"
TEST(TMP_RealProject, Factory1) {
    GameDataManager gdm; std::string err;
    ASSERT_TRUE(gdm.loadFromFile("/home/hagoel/Projects/factory-planner/build/bin/game_data/satisfactory/game_datas/version-1.1.x.x.gd", err)) << err;
    std::ifstream f("/home/hagoel/Projects/factory-planner/build/bin/projects/Factory_1.fpp");
    nlohmann::json j; f >> j;
    for (double screwA : {0.0, -1.0, 40.0}) {
        FactoryGraph g(gdm.current());
        g.deserialize(j["graph_data"], gdm.current());
        g.setPortConstraint(13, screwA);
        FactorySolver s;
        auto r = s.solve(g);
        printf("=== screwA input constraint = %g  status=%s\n", screwA, FactorySolver::toString(r.status).c_str());
        for (const auto &p : g.getPorts())
            printf("  node %-2lu %-28s port %-2lu %-12s rate %8.3f excess %8.3f kind=%s\n", p.node_id, g.getNode(p.node_id)->selected_recipe_key.c_str(), p.id, p.resource_key.c_str(), p.rate, p.excess_rate,
                   p.user_constraint >= 0 ? (FactorySolver::classifyConstraint(g, p) == FactorySolver::ConstraintKind::LIMIT ? "LIMIT" : "TARGET") : "-");
    }
}
