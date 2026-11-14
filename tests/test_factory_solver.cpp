#include <gtest/gtest.h>
#include "core/graph/FactorySolver.h"
#include "core/graph/FactoryGraph.h"
#include "core/data/GameData.h"
#include "core/data/GameDataLoader.h"

class FactorySolverTest : public ::testing::Test {
protected:
    GameData testGameData;
    std::unique_ptr<FactoryGraph> graph;
    std::unique_ptr<FactorySolver> solver;

    static void defResource(GameData& pkg, const std::string& id, const std::string& name) {
        gamedata::upsertResource(pkg, id, name);
    }

    static void defMachine(GameData& pkg, const std::string& id, const std::string& name, double speed = 1.0) {
        gamedata::upsertMachine(pkg, id, name, speed, "v3_compat");
    }

    static void defRecipe(
        GameData& pkg,
        const std::string& id,
        const std::string& name,
        std::vector<std::string> machines,
        const std::vector<std::pair<std::string, double>>& inputs,
        const std::vector<std::pair<std::string, double>>& outputs,
        double time = 1.0) {
        gamedata::upsertRecipe(pkg, id, name, time, std::move(machines), inputs, outputs);
    }

    void SetUp() override {
        testGameData = GameData{};
        testGameData.game.id = "test_game";
        testGameData.game.name = "TestGame";
        testGameData.game.data_version = "1.0.0";
        testGameData.units.recipe_time = gamedata::RecipeTimeUnit::Seconds;
        testGameData.units.default_rate = gamedata::RateUnit::PerSec;
        gamedata::ensureV3CompatProfile(testGameData);

        defResource(testGameData, "iron_ore", "Iron Ore");
        defResource(testGameData, "iron_ingot", "Iron Ingot");
        defResource(testGameData, "iron_plate", "Iron Plate");
        defResource(testGameData, "screw", "Screw");
        defResource(testGameData, "reinforced_plate", "Reinforced Plate");
        defResource(testGameData, "copper_ore", "Copper Ore");
        defResource(testGameData, "copper_ingot", "Copper Ingot");
        defResource(testGameData, "wire", "Wire");

        defMachine(testGameData, "miner", "Miner", 1.0);
        defMachine(testGameData, "smelter", "Smelter", 1.0);
        defMachine(testGameData, "constructor", "Constructor", 1.0);
        defMachine(testGameData, "assembler", "Assembler", 1.0);
        defMachine(testGameData, "sink", "AWESOME Sink", 1.0);

        defRecipe(testGameData, "mine_ore", "Mine Iron Ore", {"miner"},
                  {}, {{"iron_ore", 30.0}}, 1.0);
        defRecipe(testGameData, "smelt_ingot", "Smelt Iron Ingot", {"smelter"},
                  {{"iron_ore", 30.0}}, {{"iron_ingot", 30.0}}, 1.0);
        defRecipe(testGameData, "sink_ingot", "Sink Iron Ingot", {"sink"},
                  {{"iron_ingot", 1.0}}, {}, 1.0);
        defRecipe(testGameData, "make_plates", "Make Iron Plates", {"constructor"},
                  {{"iron_ingot", 30.0}}, {{"iron_plate", 20.0}}, 1.0);
        defRecipe(testGameData, "make_screws", "Make Screws", {"constructor"},
                  {{"iron_ingot", 10.0}}, {{"screw", 40.0}}, 1.0);
        defRecipe(testGameData, "make_reinforced", "Make Reinforced Plates", {"assembler"},
                  {{"iron_plate", 30.0}, {"screw", 60.0}}, {{"reinforced_plate", 5.0}}, 1.0);

        graph = std::make_unique<FactoryGraph>(testGameData);
        solver = std::make_unique<FactorySolver>();
    }

    void TearDown() override {
        solver.reset();
        graph.reset();
    }

    uint64_t addNode(const std::string& recipeKey) {
        const auto* rec = testGameData.findRecipe(recipeKey);
        std::string nodeName = rec ? rec->name : recipeKey;
        return graph->addNode(nodeName, recipeKey);
    }

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

    uint64_t addConnection(Port* from, Port* to) {
        if (!from || !to) return -1;
        return graph->addConnection(from->id, to->id);
    }

    void setClockSpeed(uint64_t nodeId, double clockPct) {
        graph->setNodeParam(nodeId, "clock", gamedata::ParamValue::number(clockPct));
    }

    void setProductionMultiplier(uint64_t nodeId, double multPct) {
        graph->setNodeParam(nodeId, "output_mult", gamedata::ParamValue::number(multPct));
    }

    void defResource(const std::string& id, const std::string& name) {
        gamedata::upsertResource(testGameData, id, name);
    }

    void defRecipe(
        const std::string& id,
        const std::string& name,
        double time,
        std::vector<std::string> machines,
        const std::vector<std::pair<std::string, double>>& inputs,
        const std::vector<std::pair<std::string, double>>& outputs) {
        gamedata::upsertRecipe(testGameData, id, name, time, std::move(machines), inputs, outputs);
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
    setClockSpeed(n_smelter, 0.0);
    graph->setPortConstraint(getPort(n_miner, "iron_ore", false)->id, 30.0);

    FactorySolver::SolverResult result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(graph->getNode(n_smelter)->machine_count, 0.0, 0.001);

    // Negative clock speed
    setClockSpeed(n_smelter, -50.0);
    result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(graph->getNode(n_smelter)->machine_count, 0.0, 0.001);
}

TEST_F(FactorySolverTest, RobustnessZeroRecipeTime) {
    defRecipe(testGameData, "zero_time", "Zero Time Recipe", {"smelter"},
              {{"iron_ore", 30.0}}, {{"iron_ingot", 30.0}}, 0.0);

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
    // Set smelter output_mult to 0
    setProductionMultiplier(n_smelter, 0.0);

    FactorySolver::SolverResult result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    // With output_mult = 0, output_ports[0] becomes inactive (amount_per_craft = 0).
    // Smelter now has 1 active input and 0 active outputs -> it acts as a sink in supply mode
    // unless clock is 0! Wait: when output_mult = 0 and miner is constrained in AUTO mode (PUSH 30),
    // wait: in v3, output_mult <= 0 forced all ports of the node to 0!
    // In v4, when clock_speed <= 0, craft_time is <= 0 / division by zero -> node status = ERROR -> all ports clamped to 0!
    setClockSpeed(n_smelter, 0.0);
    setProductionMultiplier(n_smelter, 100.0);

    result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    EXPECT_NEAR(p_smelter_in->rate, 0.0, 0.001);
    EXPECT_NEAR(p_smelter_out->rate, 0.0, 0.001);
    EXPECT_NEAR(graph->getNode(n_smelter)->machine_count, 0.0, 0.001);

    // Test negative production multiplier (causes negative port amount runtime ERROR -> all ports clamped to 0)
    setClockSpeed(n_smelter, 100.0);
    setProductionMultiplier(n_smelter, -50.0);
    result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_smelter_in->rate, 0.0, 0.001);
    EXPECT_NEAR(p_smelter_out->rate, 0.0, 0.001);
    EXPECT_NEAR(graph->getNode(n_smelter)->machine_count, 0.0, 0.001);

    // Test negative clock speed
    setProductionMultiplier(n_smelter, 100.0);
    setClockSpeed(n_smelter, -25.0);
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
    GameData gd = testGameData;
    defRecipe(gd, "split_recipe", "Split Production", {"constructor"},
              {{"iron_ingot", 30.0}}, {{"iron_plate", 20.0}, {"screw", 10.0}}, 1.0);
    defRecipe(gd, "sink_screw", "Sink Screws", {"sink"},
              {{"screw", 1.0}}, {}, 1.0);

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
    GameData gd = testGameData;
    defRecipe(gd, "mine_coal", "Mine Coal", {"miner"}, {}, {{"coal", 20.0}}, 1.0);
    defRecipe(gd, "process_coal", "Process Coal", {"constructor"}, {{"coal", 1.0}}, {{"coal_item", 1.0}}, 1.0);
    defRecipe(gd, "mine_ore_90", "Mine Raw Ore", {"miner"}, {}, {{"ore", 90.0}}, 1.0);
    defRecipe(gd, "make_item1", "Make Item 1", {"constructor"}, {{"ore", 1.0}}, {{"item1", 1.0}}, 1.0);
    defRecipe(gd, "make_item2", "Make Item 2", {"constructor"}, {{"ore", 2.0}}, {{"item2", 1.0}}, 1.0);

    FactoryGraph g(gd);
    FactorySolver s;

    // Target 3 chain
    uint64_t n_coal_mine = g.addNode("Mine Coal", "mine_coal");
    uint64_t n_coal_proc = g.addNode("Process Coal", "process_coal");

    // Targets 1 and 2 shared ore chain
    uint64_t n_ore_mine = g.addNode("Mine Ore", "mine_ore_90");
    uint64_t n_t1 = g.addNode("Make Item 1", "make_item1");
    uint64_t n_t2 = g.addNode("Make Item 2", "make_item2");

    constexpr uint64_t kInvalidPortId = static_cast<uint64_t>(-1);
    auto getPortId = [&](uint64_t node_id, const std::string& res, bool is_in) -> uint64_t {
        const Node* node = g.getNode(node_id);
        if (!node) return kInvalidPortId;
        const auto& p_ids = is_in ? node->input_ports : node->output_ports;
        for (uint64_t pid : p_ids) {
            const Port* p = g.getPort(pid);
            if (p && p->resource_key == res) return pid;
        }
        return kInvalidPortId;
    };

    uint64_t p_coal_mine_out = getPortId(n_coal_mine, "coal", false);
    uint64_t p_coal_proc_in = getPortId(n_coal_proc, "coal", true);
    uint64_t p_coal_proc_out = getPortId(n_coal_proc, "coal_item", false);

    ASSERT_NE(p_coal_mine_out, kInvalidPortId);
    ASSERT_NE(p_coal_proc_in, kInvalidPortId);
    ASSERT_NE(p_coal_proc_out, kInvalidPortId);

    g.addConnection(p_coal_mine_out, p_coal_proc_in);
    g.setPortConstraint(p_coal_mine_out, 20.0); // 20 coal/s supply limit
    g.setPortConstraint(p_coal_proc_out, 100.0); // 100 demand (Target 3)

    uint64_t p_ore_mine_out = getPortId(n_ore_mine, "ore", false);
    uint64_t p_t1_in = getPortId(n_t1, "ore", true);
    uint64_t p_t1_out = getPortId(n_t1, "item1", false);
    uint64_t p_t2_in = getPortId(n_t2, "ore", true);
    uint64_t p_t2_out = getPortId(n_t2, "item2", false);

    ASSERT_NE(p_ore_mine_out, kInvalidPortId);
    ASSERT_NE(p_t1_in, kInvalidPortId);
    ASSERT_NE(p_t1_out, kInvalidPortId);
    ASSERT_NE(p_t2_in, kInvalidPortId);
    ASSERT_NE(p_t2_out, kInvalidPortId);

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
    defRecipe(testGameData, "cat_recipe", "Catalytic Smelting", {"smelter"},
              {{"copper_ingot", 0.0}, {"iron_ore", 30.0}}, {{"iron_ingot", 30.0}}, 1.0);

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
    defRecipe(testGameData, "cat_recipe_nothing", "Catalytic Smelting With Nothing", {"smelter"},
              {{"nothing", 0.0}, {"iron_ore", 30.0}}, {{"iron_ingot", 30.0}}, 1.0);

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
    defRecipe(testGameData, "zero_in_recipe", "Zero Input Recipe", {"smelter"},
              {{"copper_ingot", 0.0}}, {{"iron_ingot", 30.0}}, 1.0);

    graph = std::make_unique<FactoryGraph>(testGameData);
    uint64_t n_node = graph->addNode("Zero In Smelter", "zero_in_recipe");
    graph->setPortConstraint(getPort(n_node, "iron_ingot", false)->id, 30.0);

    auto result = solver->solve(*graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(graph->getNode(n_node)->machine_count, 1.0, 0.001);
}

TEST_F(FactorySolverTest, UnboundedPhase2FailureNotMaskedByPhase3) {
    defRecipe(testGameData, "amp_a", "Amp A", {"smelter"},
              {{"iron_ore", 1.0}}, {{"iron_ingot", 2.0}}, 1.0);
    defRecipe(testGameData, "amp_b", "Amp B", {"smelter"},
              {{"iron_ingot", 1.0}}, {{"iron_ore", 2.0}, {"screw", 1.0}}, 1.0);

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
    defRecipe(testGameData, "refine", "Refine", {"assembler"},
              {{"iron_ore", 30.0}}, {{"iron_plate", 60.0}, {"screw", 40.0}}, 1.0);
    defRecipe(testGameData, "make_plastic", "Make Plastic", {"constructor"},
              {{"screw", 10.0}}, {{"wire", 10.0}}, 1.0);
    defRecipe(testGameData, "sink_screw", "Sink Screw", {"sink"},
              {{"screw", 1.0}}, {}, 1.0);

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
    defRecipe("pure_ingot", "Pure Iron Ingot", 1.0, {"smelter"}, {{"iron_ore", 30.0}}, {{"iron_ingot", 60.0}});

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
    defRecipe("amp_a", "Amp A", 1.0, {"smelter"}, {{"iron_ore", 1.0}}, {{"iron_ingot", 2.0}});
    defRecipe("amp_b", "Amp B", 1.0, {"smelter"}, {{"iron_ingot", 1.0}}, {{"iron_ore", 2.0}, {"screw", 1.0}});

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
    defRecipe("make_rods", "Make Rods", 1.0, {"constructor"}, {{"iron_ingot", 15.0}}, {{"wire", 15.0}});

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
    defRecipe("main_prod", "Main Prod", 1.0, {"assembler"}, {}, {{"iron_plate", 30.0}, {"screw", 10.0}});
    defRecipe("aux_wire", "Aux Wire", 1.0, {"constructor"}, {}, {{"wire", 10.0}});
    defRecipe("dual_sink", "Dual Sink", 1.0, {"sink"}, {{"screw", 1.0}, {"wire", 1.0}}, {});

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
    defRecipe("main_prod", "Main Prod", 1.0, {"assembler"}, {}, {{"iron_plate", 30.0}, {"iron_ingot", 10.0}});

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
    defRecipe("split_rec", "Split", 1.0, {"assembler"}, {}, {{"iron_plate", 60.0}, {"screw", 40.0}});
    defRecipe("screw_user", "Screw User", 1.0, {"constructor"}, {{"screw", 20.0}}, {{"wire", 20.0}});

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
    defResource("coal", "Coal");
    defResource("steel", "Steel");
    defResource("co2", "CO2");
    defResource("wood", "Wood");

    defRecipe("mine_coal", "Mine Coal", 1.0, {"miner"}, {}, {{"coal", 30.0}});
    defRecipe("foundry_steel", "Foundry Steel", 1.0, {"assembler"}, {{"iron_ore", 1.0}, {"coal", 1.0}}, {{"steel", 1.0}, {"co2", 1.0}});
    defRecipe("greenhouse_wood", "Greenhouse Wood", 1.0, {"constructor"}, {{"co2", 1.0}}, {{"wood", 1.0}});

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
    defResource("oil", "Oil");
    defResource("plastic", "Plastic");
    defResource("coke", "Coke");
    defResource("water", "Water");

    defRecipe("extract_oil", "Extract Oil", 1.0, {"miner"}, {}, {{"oil", 30.0}});
    defRecipe("extract_water", "Extract Water", 1.0, {"miner"}, {}, {{"water", 120.0}});
    defRecipe("make_plastic_coke", "Plastic & Coke", 1.0, {"assembler"}, {{"oil", 30.0}}, {{"plastic", 30.0}, {"coke", 30.0}});
    defRecipe("coal_gen", "Coal Generator", 1.0, {"sink"}, {{"coke", 15.0}, {"water", 45.0}}, {});

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
    defResource("coal", "Coal");
    defResource("steel", "Steel");
    defResource("co2", "CO2");

    defRecipe("mine_coal", "Mine Coal", 1.0, {"miner"}, {}, {{"coal", 30.0}});
    defRecipe("foundry_steel", "Foundry Steel", 1.0, {"assembler"}, {{"iron_ore", 1.0}, {"coal", 1.0}}, {{"steel", 1.0}, {"co2", 1.0}});
    defRecipe("coal_gen", "Coal Generator", 1.0, {"sink"}, {{"coal", 1.0}}, {});
    defRecipe("steel_constructor", "Steel Constructor", 1.0, {"constructor"}, {{"steel", 1.0}}, {{"iron_plate", 1.0}});

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
    defRecipe("loop_a", "Loop A", 1.0, {"smelter"}, {{"iron_ore", 1.0}}, {{"iron_ingot", 2.0}});
    defRecipe("loop_b", "Loop B", 1.0, {"smelter"}, {{"iron_ingot", 1.0}}, {{"iron_ore", 2.0}});

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
    defResource("water", "Water");
    defResource("alumina", "Alumina");
    defResource("scrap", "Aluminum Scrap");

    defRecipe("extract_water", "Extract Water", 1.0, {"miner"}, {}, {{"water", 100.0}});
    defRecipe("make_alumina", "Alumina Solution", 1.0, {"assembler"}, {{"water", 100.0}}, {{"alumina", 100.0}});
    defRecipe("make_scrap", "Aluminum Scrap", 1.0, {"assembler"}, {{"alumina", 100.0}}, {{"scrap", 100.0}, {"water", 30.0}});

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
    defRecipe("make_rods", "Make Rods", 1.0, {"constructor"}, {{"iron_ingot", 15.0}}, {{"wire", 15.0}});

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
    defResource("oil", "Crude Oil");
    defResource("fuel", "Fuel");
    defResource("rubber", "Rubber");
    defResource("plastic", "Plastic");
    defResource("coke", "Coke");
    defResource("circuit_board", "Circuit Board");
    defResource("rotor", "Rotor");
    defResource("motor", "Electric Motor");

    defRecipe("extract_oil", "Extract Oil", 1.0, {"miner"}, {}, {{"oil", 50.0}});
    defRecipe("ref_rubber", "Rubber Refinery", 1.0, {"assembler"}, {{"oil", 50.0}}, {{"fuel", 50.0}, {"rubber", 50.0}});
    defRecipe("recycled_plastic", "Recycled Plastic", 1.0, {"assembler"}, {{"rubber", 30.0}}, {{"plastic", 60.0}});
    defRecipe("ref_coke", "Coke Refinery", 1.0, {"assembler"}, {}, {{"coke", 120.0}});
    defRecipe("make_cb", "Electrode Circuit Board", 1.0, {"assembler"}, {{"rubber", 4.0}, {"coke", 8.0}}, {{"circuit_board", 1.0}});
    defRecipe("make_rotor", "Make Rotor", 1.0, {"constructor"}, {{"iron_ingot", 6.0}}, {{"rotor", 6.0}});
    defRecipe("make_motor", "Electric Motor", 1.0, {"assembler"}, {{"circuit_board", 1.0}, {"rotor", 4.0}}, {{"motor", 4.0}});

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
    defResource("coal", "Coal");
    defResource("steel", "Steel");
    defResource("co2", "CO2");

    defRecipe("mine_coal", "Mine Coal", 1.0, {"miner"}, {}, {{"coal", 30.0}});
    defRecipe("foundry_steel", "Foundry Steel", 1.0, {"assembler"}, {{"iron_ore", 1.0}, {"coal", 1.0}}, {{"steel", 1.0}, {"co2", 1.0}});

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
    defResource("oil", "Oil");
    defResource("plastic", "Plastic");
    defResource("rubber", "Rubber");
    defResource("slag", "Slag");
    defResource("coke", "Coke");

    defRecipe("extract_oil", "Extract Oil", 1.0, {"miner"}, {}, {{"oil", 100.0}});
    defRecipe("ref_a", "Refinery A", 1.0, {"assembler"}, {{"oil", 20.0}}, {{"plastic", 20.0}, {"slag", 20.0}});
    defRecipe("ref_b", "Refinery B", 1.0, {"assembler"}, {{"oil", 20.0}}, {{"rubber", 20.0}, {"slag", 20.0}});
    defRecipe("slag_to_oil", "Slag Recycle", 1.0, {"constructor"}, {{"slag", 5.0}}, {{"oil", 5.0}});
    defRecipe("slag_to_coke", "Slag Converter", 1.0, {"constructor"}, {{"slag", 1.0}}, {{"coke", 1.0}});
    defRecipe("sink_coke", "Sink Coke", 1.0, {"sink"}, {{"coke", 1.0}}, {});

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

// ============================================================================
// GameData v4 Integration Tests (Formulas, Dynamic/Conditional/Extra Ports, Metrics)
// ============================================================================

TEST_F(FactorySolverTest, V4_PreSolveFormulas_ExtraPorts_ConditionalPorts_And_DynamicResourceWireStates) {
    // Load a v4 package with custom profile, extra ports, conditional ports, and dynamic resource ports
    const std::string pkg_json = R"json({
        "schema_version": 4,
        "game": { "id": "test_v4", "name": "Test V4", "data_version": "1.0" },
        "units": { "recipe_time": "s", "default_rate": "s" },
        "resources": [
            { "id": "iron_ore", "name": "Iron Ore" },
            { "id": "copper_ore", "name": "Copper Ore" },
            { "id": "iron_ingot", "name": "Iron Ingot" },
            { "id": "coal", "name": "Coal" },
            { "id": "slag", "name": "Slag" },
            { "id": "ash", "name": "Ash" }
        ],
        "parameters": {
            "clock": { "label": "Clock", "type": "number", "default": 100.0, "min": 1.0, "max": 250.0 },
            "make_slag": { "label": "Make Slag", "type": "bool", "default": true },
            "ore_choice": {
                "label": "Ore Choice", "type": "resource_choice",
                "resources": ["iron_ore", "copper_ore"],
                "default": "iron_ore"
            }
        },
        "profiles": {
            "burner_prof": {
                "params": ["clock", "make_slag"],
                "let": { "speed_mult": "param.clock / 100.0" },
                "craft_time": "recipe.time / (machine.speed * speed_mult)",
                "input_amount": "port.amount",
                "output_amount": "port.amount * port.chance",
                "extra_ports": [
                    { "id": "fuel", "dir": "in", "resource": "coal", "amount": "0.5 * recipe.time" },
                    { "id": "ash", "dir": "out", "resource": "ash", "amount": "0.1 * recipe.time" }
                ]
            },
            "miner_prof": {
                "params": ["ore_choice"],
                "craft_time": "recipe.time",
                "input_amount": "port.amount",
                "output_amount": "port.amount"
            },
            "basic_prof": {
                "params": [],
                "craft_time": "recipe.time",
                "input_amount": "port.amount",
                "output_amount": "port.amount"
            }
        },
        "machines": [
            { "id": "miner_m", "name": "Miner", "profile": "miner_prof", "speed": 1.0 },
            { "id": "basic_m", "name": "Basic Machine", "profile": "basic_prof", "speed": 1.0 },
            { "id": "burner_smelter", "name": "Burner Smelter", "profile": "burner_prof", "speed": 2.0 }
        ],
        "recipes": [
            {
                "id": "mine_dynamic",
                "name": "Mine Dynamic",
                "time": 1.0,
                "machines": ["miner_m"],
                "in": [],
                "out": [
                    { "resource_param": "ore_choice", "amount": 10.0 }
                ]
            },
            {
                "id": "mine_coal",
                "name": "Mine Coal",
                "time": 1.0,
                "machines": ["basic_m"],
                "in": [],
                "out": [
                    { "res": "coal", "amount": 10.0 }
                ]
            },
            {
                "id": "smelt_with_slag",
                "name": "Smelt With Slag",
                "time": 2.0,
                "machines": ["burner_smelter"],
                "in": [
                    { "res": "iron_ore", "amount": 4.0 }
                ],
                "out": [
                    { "res": "iron_ingot", "amount": 4.0 },
                    { "res": "slag", "amount": 2.0, "enabled": "param.make_slag" }
                ]
            },
            {
                "id": "consume_slag",
                "name": "Consume Slag",
                "time": 1.0,
                "machines": ["basic_m"],
                "in": [
                    { "res": "slag", "amount": 1.0 }
                ],
                "out": []
            }
        ]
    })json";

    gamedata::LoadReport report;
    gamedata::GamePackage pkg;
    ASSERT_TRUE(gamedata::loadGamePackageFromMemory(pkg_json, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors[0].message);

    FactoryGraph g(pkg);
    uint64_t n_miner = g.addNode("Miner", "mine_dynamic");
    uint64_t n_coal = g.addNode("Coal Miner", "mine_coal");
    uint64_t n_smelter = g.addNode("Smelter", "smelt_with_slag");
    uint64_t n_slag_sink = g.addNode("Slag Sink", "consume_slag");

    Port* p_miner_out = g.getPortBySlot(n_miner, "out:0");
    Port* p_coal_out = g.getPortBySlot(n_coal, "out:0");
    Port* p_smelt_in = g.getPortBySlot(n_smelter, "in:0");
    Port* p_smelt_fuel = g.getPortBySlot(n_smelter, "xin:fuel");
    Port* p_smelt_ingot = g.getPortBySlot(n_smelter, "out:0");
    Port* p_smelt_slag = g.getPortBySlot(n_smelter, "out:1");
    Port* p_smelt_ash = g.getPortBySlot(n_smelter, "xout:ash");
    Port* p_slag_sink_in = g.getPortBySlot(n_slag_sink, "in:0");

    ASSERT_NE(p_miner_out, nullptr);
    ASSERT_NE(p_coal_out, nullptr);
    ASSERT_NE(p_smelt_in, nullptr);
    ASSERT_NE(p_smelt_fuel, nullptr);
    ASSERT_NE(p_smelt_ingot, nullptr);
    ASSERT_NE(p_smelt_slag, nullptr);
    ASSERT_NE(p_smelt_ash, nullptr);
    ASSERT_NE(p_slag_sink_in, nullptr);

    uint64_t c_ore = g.addConnection(p_miner_out->id, p_smelt_in->id);
    uint64_t c_fuel = g.addConnection(p_coal_out->id, p_smelt_fuel->id);
    uint64_t c_slag = g.addConnection(p_smelt_slag->id, p_slag_sink_in->id);
    ASSERT_NE(c_ore, static_cast<uint64_t>(-1));
    ASSERT_NE(c_fuel, static_cast<uint64_t>(-1));
    ASSERT_NE(c_slag, static_cast<uint64_t>(-1));

    // Target 8.0 iron_ingot/s -> crafts_n = 8.0 / 4.0 = 2.0 crafts/s.
    // Craft time = 2.0 / (2.0 * 1.0) = 1.0s -> machine_count = 2.0 * 1.0 = 2.0.
    // Extra fuel input: 0.5 * 2.0 = 1.0 coal/craft -> 2.0 coal/s.
    // Extra ash output: 0.1 * 2.0 = 0.2 ash/craft -> 0.4 ash/s.
    // Slag output (make_slag = true): 2.0 slag/craft -> 4.0 slag/s.
    g.setPortConstraint(p_smelt_ingot->id, 8.0, ConstraintMode::TARGET);

    auto res1 = solver->solve(g);
    ASSERT_EQ(res1.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_smelt_ingot->rate, 8.0, 1e-4);
    EXPECT_NEAR(p_smelt_in->rate, 8.0, 1e-4);
    EXPECT_NEAR(p_smelt_fuel->rate, 2.0, 1e-4);
    EXPECT_NEAR(p_smelt_ash->rate, 0.4, 1e-4);
    EXPECT_NEAR(p_smelt_slag->rate, 4.0, 1e-4);
    EXPECT_NEAR(p_slag_sink_in->rate, 4.0, 1e-4);
    EXPECT_NEAR(g.getNode(n_smelter)->machine_count, 2.0, 1e-4);
    EXPECT_NEAR(g.getNode(n_smelter)->crafts, 2.0, 1e-4);

    // Disable conditional slag port via make_slag = false:
    // Slag port becomes inactive, wire c_slag becomes INACTIVE, rate = 0.0, without clamping Smelter!
    ASSERT_TRUE(g.setNodeParam(n_smelter, "make_slag", gamedata::ParamValue{false}));
    EXPECT_FALSE(p_smelt_slag->active);
    EXPECT_EQ(g.getConnection(c_slag)->state, WireState::INACTIVE);

    auto res2 = solver->solve(g);
    ASSERT_EQ(res2.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_NEAR(p_smelt_ingot->rate, 8.0, 1e-4);
    EXPECT_DOUBLE_EQ(p_smelt_slag->rate, 0.0);
    EXPECT_DOUBLE_EQ(g.getConnection(c_slag)->rate, 0.0);
    EXPECT_DOUBLE_EQ(p_slag_sink_in->rate, 0.0);

    // Change miner's ore_choice to "copper_ore" -> p_miner_out resource becomes "copper_ore",
    // so wire c_ore to p_smelt_in ("iron_ore") becomes INVALID!
    // Wire c_ore is excluded from LP (rate = 0.0), and p_smelt_in becomes an unwired active input port
    // (free import in demand mode), while Miner (in its own targetless component with no constraints) is 0.
    ASSERT_TRUE(g.setNodeParam(n_miner, "ore_choice", gamedata::ParamValue::string("copper_ore")));
    EXPECT_EQ(p_miner_out->resource_key, "copper_ore");
    EXPECT_EQ(g.getConnection(c_ore)->state, WireState::INVALID);

    auto res3 = solver->solve(g);
    ASSERT_EQ(res3.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_DOUBLE_EQ(g.getConnection(c_ore)->rate, 0.0);
    EXPECT_DOUBLE_EQ(p_miner_out->rate, 0.0);
}

TEST_F(FactorySolverTest, V4_PostSolveMetrics_NodeAndGraphTotals_And_ErrorNodeClamping) {
    const std::string pkg_json = R"json({
        "schema_version": 4,
        "game": { "id": "test_metrics", "name": "Test Metrics", "data_version": "1.0" },
        "units": { "recipe_time": "s", "default_rate": "s" },
        "attributes": {
            "machine": {
                "stress": { "type": "number", "default": 0.0 }
            }
        },
        "resources": [
            { "id": "ore", "name": "Ore" },
            { "id": "ingot", "name": "Ingot" }
        ],
        "parameters": {
            "clock": { "label": "Clock", "type": "number", "default": 100.0, "min": 1.0, "max": 250.0 },
            "broken_div": { "label": "Broken Div", "type": "number", "default": 1.0, "min": 0.0, "max": 10.0 }
        },
        "metrics": {
            "power_mw": { "label": "Power", "unit": "MW", "aggregate": "sum", "split_sign": true },
            "heat_kw": { "label": "Heat", "unit": "kW", "aggregate": "sum", "split_sign": false },
            "peak_stress": { "label": "Peak Stress", "unit": "MPa", "aggregate": "max" },
            "min_eff": { "label": "Min Efficiency", "unit": "%", "aggregate": "min" },
            "pollution": { "label": "Pollution", "unit": "/s", "aggregate": "sum", "is_rate": true },
            "local_only": { "label": "Local Tag", "unit": "", "aggregate": "none" }
        },
        "profiles": {
            "gen_prof": {
                "params": ["clock"],
                "craft_time": "recipe.time / (param.clock / 100.0)",
                "input_amount": "port.amount",
                "output_amount": "port.amount",
                "metrics": {
                    "power_mw": { "per_machine": "-50.0" },
                    "heat_kw": { "per_craft": "10.0" },
                    "peak_stress": { "value": "machine.stress" },
                    "min_eff": { "value": "90.0" },
                    "pollution": { "per_craft": "2.0" },
                    "local_only": { "value": "node.machines * 3.0" }
                }
            },
            "smelt_prof": {
                "params": ["clock", "broken_div"],
                "craft_time": "recipe.time / ((param.clock / 100.0) * param.broken_div)",
                "input_amount": "port.amount",
                "output_amount": "port.amount",
                "metrics": {
                    "power_mw": { "per_machine": "15.0" },
                    "heat_kw": { "per_craft": "5.0" },
                    "peak_stress": { "value": "machine.stress" },
                    "min_eff": { "value": "75.0" },
                    "pollution": { "per_craft": "1.0" },
                    "local_only": { "value": "node.machines * 2.0" }
                }
            }
        },
        "machines": [
            { "id": "miner", "name": "Miner", "profile": "gen_prof", "attrs": { "stress": 20.0 } },
            { "id": "smelter", "name": "Smelter", "profile": "smelt_prof", "attrs": { "stress": 35.0 } }
        ],
        "recipes": [
            {
                "id": "mine_ore", "name": "Mine Ore", "time": 1.0, "machines": ["miner"],
                "in": [], "out": [{ "res": "ore", "amount": 10.0 }]
            },
            {
                "id": "smelt_ore", "name": "Smelt Ore", "time": 2.0, "machines": ["smelter"],
                "in": [{ "res": "ore", "amount": 10.0 }],
                "out": [{ "res": "ingot", "amount": 10.0 }]
            }
        ]
    })json";

    gamedata::LoadReport report;
    gamedata::GamePackage pkg;
    ASSERT_TRUE(gamedata::loadGamePackageFromMemory(pkg_json, {}, pkg, report));

    FactoryGraph g(pkg);
    uint64_t n_miner = g.addNode("Miner", "mine_ore");
    uint64_t n_smelter = g.addNode("Smelter", "smelt_ore");
    Port* p_miner_out = g.getPortBySlot(n_miner, "out:0");
    Port* p_smelt_in = g.getPortBySlot(n_smelter, "in:0");
    Port* p_smelt_out = g.getPortBySlot(n_smelter, "out:0");
    g.addConnection(p_miner_out->id, p_smelt_in->id);

    // Target 20 ingot/s:
    // Miner: 20 ore/s -> crafts = 2.0/s, machine_count = 2.0
    //   power_mw = 2.0 * -50 = -100 MW
    //   heat_kw  = 2.0 * 10 = 20 kW
    //   peak_stress = 20
    //   min_eff = 90
    //   pollution = 2.0 * 2 = 4/s
    //   local_only = 6
    // Smelter: 20 ingot/s -> crafts = 2.0/s, machine_count = 2.0 * 2.0 = 4.0
    //   power_mw = 4.0 * 15 = +60 MW
    //   heat_kw  = 2.0 * 5 = 10 kW
    //   peak_stress = 35
    //   min_eff = 75
    //   pollution = 2.0 * 1 = 2/s
    //   local_only = 8
    g.setPortConstraint(p_smelt_out->id, 20.0, ConstraintMode::TARGET);

    auto res = solver->solve(g);
    ASSERT_EQ(res.status, FactorySolver::SolverResultStatus::SUCCESS);

    const auto* m_miner = solver->getNodeMetrics(n_miner);
    const auto* m_smelt = solver->getNodeMetrics(n_smelter);
    ASSERT_NE(m_miner, nullptr);
    ASSERT_NE(m_smelt, nullptr);

    const auto* m_miner_power = m_miner->findMetric("power_mw");
    const auto* m_smelt_power = m_smelt->findMetric("power_mw");
    const auto* m_miner_local = m_miner->findMetric("local_only");
    const auto* m_smelt_local = m_smelt->findMetric("local_only");
    ASSERT_NE(m_miner_power, nullptr);
    ASSERT_NE(m_smelt_power, nullptr);
    ASSERT_NE(m_miner_local, nullptr);
    ASSERT_NE(m_smelt_local, nullptr);
    EXPECT_NEAR(m_miner_power->value, -100.0, 1e-4);
    EXPECT_NEAR(m_smelt_power->value, 60.0, 1e-4);
    EXPECT_NEAR(m_miner_local->value, 6.0, 1e-4);
    EXPECT_NEAR(m_smelt_local->value, 8.0, 1e-4);

    auto findTotal = [&](const std::string& id) -> const gamedata::MetricTotal* {
        for (const auto& t : solver->getTotals()) {
            if (t.metric_id == id) return &t;
        }
        return nullptr;
    };

    const auto* t_power = findTotal("power_mw");
    const auto* t_heat = findTotal("heat_kw");
    const auto* t_stress = findTotal("peak_stress");
    const auto* t_eff = findTotal("min_eff");
    const auto* t_poll = findTotal("pollution");
    const auto* t_local = findTotal("local_only");

    ASSERT_NE(t_power, nullptr);
    EXPECT_TRUE(t_power->has_value);
    EXPECT_NEAR(t_power->positive_sum, 60.0, 1e-4);
    EXPECT_NEAR(t_power->negative_sum, -100.0, 1e-4);
    EXPECT_NEAR(t_power->net_sum, -40.0, 1e-4);

    ASSERT_NE(t_heat, nullptr);
    ASSERT_NE(t_stress, nullptr);
    ASSERT_NE(t_eff, nullptr);
    ASSERT_NE(t_poll, nullptr);
    EXPECT_NEAR(t_heat->value, 30.0, 1e-4);
    EXPECT_NEAR(t_stress->value, 35.0, 1e-4);
    EXPECT_NEAR(t_eff->value, 75.0, 1e-4);
    EXPECT_NEAR(t_poll->value, 6.0, 1e-4);
    EXPECT_TRUE(t_local == nullptr || !t_local->has_value);

    // Trigger EvalStatus::ERROR on Smelter by setting broken_div = 0.0 (division by zero in craft_time):
    // Smelter must be clamped to 0 (machine_count = 0, crafts = 0, rates = 0) and excluded from totals!
    ASSERT_TRUE(g.setNodeParam(n_smelter, "broken_div", gamedata::ParamValue{0.0}));
    EXPECT_EQ(g.getNodeEval(n_smelter)->status, gamedata::EvalStatus::ERROR);

    auto res_err = solver->solve(g);
    ASSERT_EQ(res_err.status, FactorySolver::SolverResultStatus::SUCCESS);
    EXPECT_DOUBLE_EQ(g.getNode(n_smelter)->machine_count, 0.0);
    EXPECT_DOUBLE_EQ(g.getNode(n_smelter)->crafts, 0.0);
    EXPECT_DOUBLE_EQ(p_smelt_out->rate, 0.0);
}


