#define IMGUI_DEFINE_MATH_OPERATORS
#include <gtest/gtest.h>
#include <filesystem>
#include <cmath>
#include <imgui.h>
#include <imgui_node_editor.h>

#include "core/data/GameData.h"
#include "core/data/GameDataManager.h"
#include "core/data/GameDataScanner.h"
#include "core/graph/FactoryGraph.h"
#include "core/graph/FactorySolver.h"
#include "services/ProjectIo.h"
#include "services/SettingsManager.h"
#include "common/FilesystemUtils.h"
#include "common/IdUtils.h"

namespace {

/**
 * Helper to locate the game data root directory and the Satisfactory .gd file.
 */
std::pair<std::filesystem::path, std::filesystem::path> locateSatisfactoryGameData() {
    std::filesystem::path execDir = getExecutableDirectory().value_or(std::filesystem::current_path());
    for (const auto& candidateDir : {
             std::filesystem::current_path() / "data" / "game_data",
             std::filesystem::current_path().parent_path() / "data" / "game_data",
             std::filesystem::current_path() / "bin" / "game_data",
             std::filesystem::current_path() / "build" / "bin" / "game_data",
             execDir / "game_data",
             execDir.parent_path() / "game_data",
             execDir.parent_path() / "bin" / "game_data",
             execDir.parent_path() / "data" / "game_data",
             execDir.parent_path().parent_path() / "data" / "game_data",
         }) {
        auto candidateFile = candidateDir / "satisfactory" / "game_datas" / "version-1.1.x.x.gd";
        if (std::filesystem::exists(candidateFile)) {
            return {candidateDir, candidateFile};
        }
    }
    return {};
}

} // namespace

class GoldenTest : public ::testing::Test {
protected:
    GameData satisfactoryGameData;
    AppSettings origSettings;
    std::filesystem::path gameDataDir;
    std::filesystem::path gameDataFile;

    void SetUp() override {
        origSettings = SettingsManager::instance().getSettings();

        auto [dir, file] = locateSatisfactoryGameData();
        ASSERT_FALSE(file.empty()) << "Could not locate satisfactory/game_datas/version-1.1.x.x.gd";
        gameDataDir = dir;
        gameDataFile = file;

        AppSettings testSettings = origSettings;
        testSettings.gameDataPath = gameDataDir;
        SettingsManager::instance().setSettings(testSettings);

        GameDataManager gdm;
        std::string error;
        ASSERT_TRUE(gdm.loadFromFile(gameDataFile, error)) << "Failed to load Satisfactory data: " << error;
        satisfactoryGameData = gdm.current();
    }

    void TearDown() override {
        SettingsManager::instance().setSettings(origSettings);
    }
};

// =============================================================================
// Task 1: Project Round-Trip Golden Test
// Programmatically build graphs (multiple nodes, multi-port recipes, fan-out/fan-in,
// constraints, custom clock speed & production multiplier, node positions),
// save with ProjectIO, load again, and assert full equality.
// =============================================================================

TEST_F(GoldenTest, ProjectRoundTrip_MultiPortFanOutFanInConstraintsParametersPositions) {
    ImGuiContext* imguiCtx = ImGui::CreateContext();
    ax::NodeEditor::Config cfg;
    cfg.SettingsFile = nullptr;
    auto* origEdCtx = ax::NodeEditor::CreateEditor(&cfg);
    ax::NodeEditor::SetCurrentEditor(origEdCtx);

    FactoryGraph origGraph(satisfactoryGameData);

    // Node 1: Miner 1 ("iron-ore", 1 input "nothing", 1 output "iron-ore")
    const auto& recOre = satisfactoryGameData.recipes.at("iron-ore");
    uint64_t n1 = origGraph.addNode(recOre.name, "iron-ore");
    origGraph.getNode(n1)->clock_speed = 125.0;
    origGraph.getNode(n1)->production_multiplier = 100.0;
    ImVec2 pos1(100.0f, 150.0f);
    ax::NodeEditor::SetNodePosition(IdUtils::toNodeId(n1), pos1);
    origGraph.setPortConstraint(origGraph.getNode(n1)->output_ports[0], 120.0);

    // Node 2: Miner 2 ("iron-ore") - for fan-in into Smelter input port
    uint64_t n2 = origGraph.addNode(recOre.name, "iron-ore");
    origGraph.getNode(n2)->clock_speed = 75.0;
    origGraph.getNode(n2)->production_multiplier = 100.0;
    ImVec2 pos2(100.0f, 350.0f);
    ax::NodeEditor::SetNodePosition(IdUtils::toNodeId(n2), pos2);
    origGraph.setPortConstraint(origGraph.getNode(n2)->output_ports[0], 60.0);

    // Node 3: Smelter ("iron-ingot", 1 in "iron-ore", 1 out "iron-ingot")
    // Receives fan-in from Miner 1 and Miner 2; fans out to Plate and Rod constructors
    const auto& recIngot = satisfactoryGameData.recipes.at("iron-ingot");
    uint64_t n3 = origGraph.addNode(recIngot.name, "iron-ingot");
    origGraph.getNode(n3)->clock_speed = 100.0;
    origGraph.getNode(n3)->production_multiplier = 150.0;
    ImVec2 pos3(350.0f, 250.0f);
    ax::NodeEditor::SetNodePosition(IdUtils::toNodeId(n3), pos3);
    origGraph.setPortConstraint(origGraph.getNode(n3)->input_ports[0], 90.0);

    // Node 4: Plate Constructor ("iron-plate", 1 in "iron-ingot", 1 out "iron-plate")
    const auto& recPlate = satisfactoryGameData.recipes.at("iron-plate");
    uint64_t n4 = origGraph.addNode(recPlate.name, "iron-plate");
    origGraph.getNode(n4)->clock_speed = 110.0;
    origGraph.getNode(n4)->production_multiplier = 100.0;
    ImVec2 pos4(600.0f, 150.0f);
    ax::NodeEditor::SetNodePosition(IdUtils::toNodeId(n4), pos4);

    // Node 5: Rod Constructor ("iron-rod", 1 in "iron-ingot", 1 out "iron-rod")
    const auto& recRod = satisfactoryGameData.recipes.at("iron-rod");
    uint64_t n5 = origGraph.addNode(recRod.name, "iron-rod");
    origGraph.getNode(n5)->clock_speed = 90.0;
    origGraph.getNode(n5)->production_multiplier = 100.0;
    ImVec2 pos5(600.0f, 350.0f);
    ax::NodeEditor::SetNodePosition(IdUtils::toNodeId(n5), pos5);

    // Node 6: Screw Constructor ("screw", 1 in "iron-rod", 1 out "screw")
    const auto& recScrew = satisfactoryGameData.recipes.at("screw");
    uint64_t n6 = origGraph.addNode(recScrew.name, "screw");
    origGraph.getNode(n6)->clock_speed = 100.0;
    origGraph.getNode(n6)->production_multiplier = 100.0;
    ImVec2 pos6(850.0f, 350.0f);
    ax::NodeEditor::SetNodePosition(IdUtils::toNodeId(n6), pos6);

    // Node 7: Multi-port Assembler ("reinforced-iron-plate", 2 inputs, 1 output)
    const auto& recRfp = satisfactoryGameData.recipes.at("reinforced-iron-plate");
    uint64_t n7 = origGraph.addNode(recRfp.name, "reinforced-iron-plate");
    ASSERT_EQ(origGraph.getNode(n7)->input_ports.size(), 2);
    ASSERT_EQ(origGraph.getNode(n7)->output_ports.size(), 1);
    origGraph.getNode(n7)->clock_speed = 140.0;
    origGraph.getNode(n7)->production_multiplier = 200.0;
    ImVec2 pos7(1100.0f, 250.0f);
    ax::NodeEditor::SetNodePosition(IdUtils::toNodeId(n7), pos7);
    origGraph.setPortConstraint(origGraph.getNode(n7)->output_ports[0], 25.0);

    // Node 8: Multi-port Refinery ("plastic", 1 input "crude-oil", 2 outputs: "plastic" and "heavy-oil-residue")
    const auto& recPlastic = satisfactoryGameData.recipes.at("plastic");
    uint64_t n8 = origGraph.addNode(recPlastic.name, "plastic");
    ASSERT_EQ(origGraph.getNode(n8)->input_ports.size(), 1);
    ASSERT_EQ(origGraph.getNode(n8)->output_ports.size(), 2);
    origGraph.getNode(n8)->clock_speed = 100.0;
    origGraph.getNode(n8)->production_multiplier = 100.0;
    ImVec2 pos8(350.0f, 500.0f);
    ax::NodeEditor::SetNodePosition(IdUtils::toNodeId(n8), pos8);
    origGraph.setPortConstraint(origGraph.getNode(n8)->output_ports[0], 40.0);
    origGraph.setPortConstraint(origGraph.getNode(n8)->output_ports[1], 20.0);

    // Connections:
    // Fan-in: Miner 1 and Miner 2 both connect to Smelter's single input port
    uint64_t c1 = origGraph.addConnection(origGraph.getNode(n1)->output_ports[0], origGraph.getNode(n3)->input_ports[0]);
    uint64_t c2 = origGraph.addConnection(origGraph.getNode(n2)->output_ports[0], origGraph.getNode(n3)->input_ports[0]);
    ASSERT_NE(c1, (uint64_t)-1);
    ASSERT_NE(c2, (uint64_t)-1);

    // Fan-out: Smelter's single output port connects to Plate Constructor and Rod Constructor
    uint64_t c3 = origGraph.addConnection(origGraph.getNode(n3)->output_ports[0], origGraph.getNode(n4)->input_ports[0]);
    uint64_t c4 = origGraph.addConnection(origGraph.getNode(n3)->output_ports[0], origGraph.getNode(n5)->input_ports[0]);
    ASSERT_NE(c3, (uint64_t)-1);
    ASSERT_NE(c4, (uint64_t)-1);

    // Intermediate wire: Rod Constructor -> Screw Constructor
    uint64_t c5 = origGraph.addConnection(origGraph.getNode(n5)->output_ports[0], origGraph.getNode(n6)->input_ports[0]);
    ASSERT_NE(c5, (uint64_t)-1);

    // Convergence into multi-port Assembler:
    // Plate Constructor -> Assembler input 0 ("iron-plate")
    // Screw Constructor -> Assembler input 1 ("screw")
    uint64_t c6 = origGraph.addConnection(origGraph.getNode(n4)->output_ports[0], origGraph.getNode(n7)->input_ports[0]);
    uint64_t c7 = origGraph.addConnection(origGraph.getNode(n6)->output_ports[0], origGraph.getNode(n7)->input_ports[1]);
    ASSERT_NE(c6, (uint64_t)-1);
    ASSERT_NE(c7, (uint64_t)-1);

    std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "golden_roundtrip_test.fpp";
    bool saveOk = ProjectIO::saveProject(tempPath.string(), origGraph, origEdCtx);
    ASSERT_TRUE(saveOk);

    // Destroy original editor context
    ax::NodeEditor::SetCurrentEditor(nullptr);
    ax::NodeEditor::DestroyEditor(origEdCtx);

    // Create fresh editor context for loading
    auto* loadedEdCtx = ax::NodeEditor::CreateEditor(&cfg);
    ax::NodeEditor::SetCurrentEditor(loadedEdCtx);

    FactoryGraph loadedGraph(satisfactoryGameData);
    bool loadOk = ProjectIO::loadProject(tempPath.string(), loadedGraph);
    ASSERT_TRUE(loadOk);

    // --- Assert Full Graph Equality ---

    // 1. Nodes count and attributes
    const auto& origNodes = origGraph.getNodes();
    const auto& loadedNodes = loadedGraph.getNodes();
    ASSERT_EQ(loadedNodes.size(), origNodes.size());

    for (const auto& oNode : origNodes) {
        const Node* lNode = loadedGraph.getNode(oNode.id);
        ASSERT_NE(lNode, nullptr) << "Node " << oNode.id << " missing after load";
        EXPECT_EQ(lNode->id, oNode.id);
        EXPECT_EQ(lNode->name, oNode.name);
        EXPECT_EQ(lNode->selected_recipe_key, oNode.selected_recipe_key);
        EXPECT_EQ(lNode->machine_key, oNode.machine_key);
        EXPECT_DOUBLE_EQ(lNode->clock_speed, oNode.clock_speed);
        EXPECT_DOUBLE_EQ(lNode->production_multiplier, oNode.production_multiplier);
        EXPECT_EQ(lNode->input_ports, oNode.input_ports);
        EXPECT_EQ(lNode->output_ports, oNode.output_ports);

        // Position verification in editor context
        ImVec2 loadedPos = ax::NodeEditor::GetNodePosition(IdUtils::toNodeId(lNode->id));
        if (oNode.id == n1) {
            EXPECT_FLOAT_EQ(loadedPos.x, pos1.x);
            EXPECT_FLOAT_EQ(loadedPos.y, pos1.y);
        } else if (oNode.id == n2) {
            EXPECT_FLOAT_EQ(loadedPos.x, pos2.x);
            EXPECT_FLOAT_EQ(loadedPos.y, pos2.y);
        } else if (oNode.id == n3) {
            EXPECT_FLOAT_EQ(loadedPos.x, pos3.x);
            EXPECT_FLOAT_EQ(loadedPos.y, pos3.y);
        } else if (oNode.id == n4) {
            EXPECT_FLOAT_EQ(loadedPos.x, pos4.x);
            EXPECT_FLOAT_EQ(loadedPos.y, pos4.y);
        } else if (oNode.id == n5) {
            EXPECT_FLOAT_EQ(loadedPos.x, pos5.x);
            EXPECT_FLOAT_EQ(loadedPos.y, pos5.y);
        } else if (oNode.id == n6) {
            EXPECT_FLOAT_EQ(loadedPos.x, pos6.x);
            EXPECT_FLOAT_EQ(loadedPos.y, pos6.y);
        } else if (oNode.id == n7) {
            EXPECT_FLOAT_EQ(loadedPos.x, pos7.x);
            EXPECT_FLOAT_EQ(loadedPos.y, pos7.y);
        } else if (oNode.id == n8) {
            EXPECT_FLOAT_EQ(loadedPos.x, pos8.x);
            EXPECT_FLOAT_EQ(loadedPos.y, pos8.y);
        }
    }

    // 2. Ports count and constraints
    const auto& origPorts = origGraph.getPorts();
    const auto& loadedPorts = loadedGraph.getPorts();
    ASSERT_EQ(loadedPorts.size(), origPorts.size());

    for (const auto& oPort : origPorts) {
        const Port* lPort = loadedGraph.getPort(oPort.id);
        ASSERT_NE(lPort, nullptr) << "Port " << oPort.id << " missing after load";
        EXPECT_EQ(lPort->id, oPort.id);
        EXPECT_EQ(lPort->node_id, oPort.node_id);
        EXPECT_EQ(lPort->resource_key, oPort.resource_key);
        EXPECT_DOUBLE_EQ(lPort->user_constraint, oPort.user_constraint);
    }

    // 3. Connections count and endpoints
    const auto& origConns = origGraph.getConnections();
    const auto& loadedConns = loadedGraph.getConnections();
    ASSERT_EQ(loadedConns.size(), origConns.size());

    for (const auto& oConn : origConns) {
        const Connection* lConn = loadedGraph.getConnection(oConn.id);
        ASSERT_NE(lConn, nullptr) << "Connection " << oConn.id << " missing after load";
        EXPECT_EQ(lConn->id, oConn.id);
        EXPECT_EQ(lConn->from_port, oConn.from_port);
        EXPECT_EQ(lConn->to_port, oConn.to_port);
        EXPECT_EQ(lConn->resource_key, oConn.resource_key);
    }

    // Cleanup
    ax::NodeEditor::SetCurrentEditor(nullptr);
    ax::NodeEditor::DestroyEditor(loadedEdCtx);
    ImGui::DestroyContext(imguiCtx);
    std::error_code ec;
    std::filesystem::remove(tempPath, ec);
}

// =============================================================================
// Task 2: Solver Goldens (7 Realistic Satisfactory Chains)
// =============================================================================

/**
 * Chain 1: Simple Linear Production — Iron Plates
 *
 * Topology: Miner Mk1 ("iron-ore") -> Smelter ("iron-ingot") -> Constructor ("iron-plate")
 * Target on iron-plate output: 20.0 items/min.
 *
 * Recipe data & Hand Calculations:
 *  - Time unit U = 60 s/min.
 *  - Recipe "iron-plate" (Constructor, base speed = 1.0):
 *      Inputs: [3.0 iron-ingot], Outputs: [2.0 iron-plate], Time: 6.0 s.
 *      Target plate output = 20.0 items/min.
 *      Required ingot input = 20.0 * (3.0 / 2.0) = 30.0 items/min.
 *      input_per_machine = (3.0 / 6.0) * 1.0 * 60 * 1.0 = 30.0 ingot/min.
 *      machine_count = 30.0 / 30.0 = 1.0.
 *  - Recipe "iron-ingot" (Smelter, base speed = 1.0):
 *      Inputs: [1.0 iron-ore], Outputs: [1.0 iron-ingot], Time: 2.0 s.
 *      Output rate = 30.0 items/min.
 *      Required ore input = 30.0 * (1.0 / 1.0) = 30.0 items/min.
 *      input_per_machine = (1.0 / 2.0) * 1.0 * 60 * 1.0 = 30.0 ore/min.
 *      machine_count = 30.0 / 30.0 = 1.0.
 *  - Recipe "iron-ore" (Miner Mk1, base speed = 1.0, raw producer):
 *      Inputs: [0.0 "nothing"], Outputs: [1.0 iron-ore], Time: 1.0 s.
 *      Output rate = 30.0 items/min.
 *      output_per_machine = (1.0 / 1.0) * 1.0 * 60 * 1.0 = 60.0 ore/min.
 *      machine_count = 30.0 / 60.0 = 0.5.
 */
TEST_F(GoldenTest, SolverGolden_Chain1_IronPlates_LinearProduction) {
    FactoryGraph graph(satisfactoryGameData);

    uint64_t n_miner = graph.addNode("Miner", "iron-ore");
    uint64_t n_smelter = graph.addNode("Smelter", "iron-ingot");
    uint64_t n_const = graph.addNode("Constructor", "iron-plate");

    Node* miner = graph.getNode(n_miner);
    Node* smelter = graph.getNode(n_smelter);
    Node* plate_const = graph.getNode(n_const);
    ASSERT_NE(miner, nullptr);
    ASSERT_NE(smelter, nullptr);
    ASSERT_NE(plate_const, nullptr);

    uint64_t c1 = graph.addConnection(miner->output_ports[0], smelter->input_ports[0]);
    uint64_t c2 = graph.addConnection(smelter->output_ports[0], plate_const->input_ports[0]);
    ASSERT_NE(c1, (uint64_t)-1);
    ASSERT_NE(c2, (uint64_t)-1);

    // Target 20.0 plates/min
    graph.setPortConstraint(plate_const->output_ports[0], 20.0);

    FactorySolver solver;
    auto result = solver.solve(graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    // Port rate assertions
    EXPECT_NEAR(graph.getPort(plate_const->output_ports[0])->rate, 20.0, 0.001);
    EXPECT_NEAR(graph.getPort(plate_const->input_ports[0])->rate, 30.0, 0.001);
    EXPECT_NEAR(graph.getPort(smelter->output_ports[0])->rate, 30.0, 0.001);
    EXPECT_NEAR(graph.getPort(smelter->input_ports[0])->rate, 30.0, 0.001);
    EXPECT_NEAR(graph.getPort(miner->output_ports[0])->rate, 30.0, 0.001);

    // Machine count assertions
    EXPECT_NEAR(plate_const->machine_count, 1.0, 0.001);
    EXPECT_NEAR(smelter->machine_count, 1.0, 0.001);
    EXPECT_NEAR(miner->machine_count, 0.5, 0.001);

    // Connection flow assertions
    EXPECT_NEAR(graph.getConnection(c1)->rate, 30.0, 0.001);
    EXPECT_NEAR(graph.getConnection(c2)->rate, 30.0, 0.001);

    // Excess rate assertions (all zero)
    EXPECT_NEAR(graph.getPort(miner->output_ports[0])->excess_rate, 0.0, 0.001);
    EXPECT_NEAR(graph.getPort(smelter->output_ports[0])->excess_rate, 0.0, 0.001);
}

/**
 * Chain 2: Multi-Input Fan-In Assembly — Reinforced Iron Plates
 *
 * Topology:
 *   Miner ("iron-ore") -> Smelter ("iron-ingot") ->
 *     Branch A: Constructor ("iron-plate")
 *     Branch B: Constructor ("iron-rod") -> Constructor ("screw")
 *   Both branches feed into: Assembler ("reinforced-iron-plate").
 * Target on reinforced-iron-plate output: 10.0 items/min.
 *
 * Recipe data & Hand Calculations:
 *  - Recipe "reinforced-iron-plate" (Assembler, speed = 1.0):
 *      Inputs: [6.0 iron-plate, 12.0 screw], Outputs: [1.0 reinforced-iron-plate], Time: 12.0 s.
 *      Target RFP output = 10.0 / min.
 *      Required iron-plate input = 10.0 * 6.0 = 60.0 / min.
 *      Required screw input = 10.0 * 12.0 = 120.0 / min.
 *      input_per_machine = (6.0 / 12.0) * 1.0 * 60 = 30.0 plate/min.
 *      machine_count = 60.0 / 30.0 = 2.0.
 *  - Recipe "iron-plate" (Constructor, speed = 1.0):
 *      Inputs: [3.0 iron-ingot], Outputs: [2.0 iron-plate], Time: 6.0 s.
 *      Output = 60.0 / min.
 *      Required ingot = 60.0 * (3.0 / 2.0) = 90.0 / min.
 *      input_per_machine = (3.0 / 6.0) * 60 = 30.0 ingot/min.
 *      machine_count = 90.0 / 30.0 = 3.0.
 *  - Recipe "screw" (Constructor, speed = 1.0):
 *      Inputs: [1.0 iron-rod], Outputs: [4.0 screw], Time: 6.0 s.
 *      Output = 120.0 / min.
 *      Required rod = 120.0 * (1.0 / 4.0) = 30.0 / min.
 *      input_per_machine = (1.0 / 6.0) * 60 = 10.0 rod/min.
 *      machine_count = 30.0 / 10.0 = 3.0.
 *  - Recipe "iron-rod" (Constructor, speed = 1.0):
 *      Inputs: [1.0 iron-ingot], Outputs: [1.0 iron-rod], Time: 4.0 s.
 *      Output = 30.0 / min.
 *      Required ingot = 30.0 * (1.0 / 1.0) = 30.0 / min.
 *      input_per_machine = (1.0 / 4.0) * 60 = 15.0 ingot/min.
 *      machine_count = 30.0 / 15.0 = 2.0.
 *  - Recipe "iron-ingot" (Smelter, speed = 1.0):
 *      Inputs: [1.0 iron-ore], Outputs: [1.0 iron-ingot], Time: 2.0 s.
 *      Total ingot output = 90.0 (plates) + 30.0 (rods) = 120.0 / min.
 *      Required ore input = 120.0 / min.
 *      input_per_machine = (1.0 / 2.0) * 60 = 30.0 ore/min.
 *      machine_count = 120.0 / 30.0 = 4.0.
 *  - Recipe "iron-ore" (Miner Mk1, speed = 1.0):
 *      Output = 120.0 / min.
 *      output_per_machine = (1.0 / 1.0) * 60 = 60.0 ore/min.
 *      machine_count = 120.0 / 60.0 = 2.0.
 */
TEST_F(GoldenTest, SolverGolden_Chain2_ReinforcedIronPlates_MultiInputFanIn) {
    FactoryGraph graph(satisfactoryGameData);

    uint64_t n_miner = graph.addNode("Miner", "iron-ore");
    uint64_t n_smelter = graph.addNode("Smelter", "iron-ingot");
    uint64_t n_plate_const = graph.addNode("Plate Constructor", "iron-plate");
    uint64_t n_rod_const = graph.addNode("Rod Constructor", "iron-rod");
    uint64_t n_screw_const = graph.addNode("Screw Constructor", "screw");
    uint64_t n_assembler = graph.addNode("Assembler", "reinforced-iron-plate");

    Node* miner = graph.getNode(n_miner);
    Node* smelter = graph.getNode(n_smelter);
    Node* plate_const = graph.getNode(n_plate_const);
    Node* rod_const = graph.getNode(n_rod_const);
    Node* screw_const = graph.getNode(n_screw_const);
    Node* assembler = graph.getNode(n_assembler);

    // Wiring
    uint64_t c_mine_smelt = graph.addConnection(miner->output_ports[0], smelter->input_ports[0]);
    uint64_t c_smelt_plate = graph.addConnection(smelter->output_ports[0], plate_const->input_ports[0]);
    uint64_t c_smelt_rod = graph.addConnection(smelter->output_ports[0], rod_const->input_ports[0]);
    uint64_t c_plate_asm = graph.addConnection(plate_const->output_ports[0], assembler->input_ports[0]);
    uint64_t c_rod_screw = graph.addConnection(rod_const->output_ports[0], screw_const->input_ports[0]);
    uint64_t c_screw_asm = graph.addConnection(screw_const->output_ports[0], assembler->input_ports[1]);

    // Target 10.0 reinforced iron plates/min
    graph.setPortConstraint(assembler->output_ports[0], 10.0);

    FactorySolver solver;
    auto result = solver.solve(graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    // Port rates
    EXPECT_NEAR(graph.getPort(assembler->output_ports[0])->rate, 10.0, 0.001);
    EXPECT_NEAR(graph.getPort(assembler->input_ports[0])->rate, 60.0, 0.001);  // iron-plate
    EXPECT_NEAR(graph.getPort(assembler->input_ports[1])->rate, 120.0, 0.001); // screw
    EXPECT_NEAR(graph.getPort(screw_const->output_ports[0])->rate, 120.0, 0.001);
    EXPECT_NEAR(graph.getPort(screw_const->input_ports[0])->rate, 30.0, 0.001);
    EXPECT_NEAR(graph.getPort(rod_const->output_ports[0])->rate, 30.0, 0.001);
    EXPECT_NEAR(graph.getPort(rod_const->input_ports[0])->rate, 30.0, 0.001);
    EXPECT_NEAR(graph.getPort(plate_const->output_ports[0])->rate, 60.0, 0.001);
    EXPECT_NEAR(graph.getPort(plate_const->input_ports[0])->rate, 90.0, 0.001);
    EXPECT_NEAR(graph.getPort(smelter->output_ports[0])->rate, 120.0, 0.001);
    EXPECT_NEAR(graph.getPort(smelter->input_ports[0])->rate, 120.0, 0.001);
    EXPECT_NEAR(graph.getPort(miner->output_ports[0])->rate, 120.0, 0.001);

    // Machine counts
    EXPECT_NEAR(assembler->machine_count, 2.0, 0.001);
    EXPECT_NEAR(plate_const->machine_count, 3.0, 0.001);
    EXPECT_NEAR(rod_const->machine_count, 2.0, 0.001);
    EXPECT_NEAR(screw_const->machine_count, 3.0, 0.001);
    EXPECT_NEAR(smelter->machine_count, 4.0, 0.001);
    EXPECT_NEAR(miner->machine_count, 2.0, 0.001);

    // Connection flow rates
    EXPECT_NEAR(graph.getConnection(c_mine_smelt)->rate, 120.0, 0.001);
    EXPECT_NEAR(graph.getConnection(c_smelt_plate)->rate, 90.0, 0.001);
    EXPECT_NEAR(graph.getConnection(c_smelt_rod)->rate, 30.0, 0.001);
    EXPECT_NEAR(graph.getConnection(c_plate_asm)->rate, 60.0, 0.001);
    EXPECT_NEAR(graph.getConnection(c_rod_screw)->rate, 30.0, 0.001);
    EXPECT_NEAR(graph.getConnection(c_screw_asm)->rate, 120.0, 0.001);
}

/**
 * Chain 3: Byproduct Production & Routing with Unconsumed Excess
 *
 * Topology:
 *   Oil Extractor ("crude-oil") -> Plastic Refinery ("plastic": out0 plastic, out1 heavy-oil-residue)
 *   Byproduct heavy-oil-residue wire connects to Residual Fuel Refinery ("residual-fuel": in heavy-oil-residue, out fuel).
 * Constraints:
 *   - Target on plastic output: 60.0 / min.
 *   - Target on fuel output: 10.0 / min.
 *
 * Recipe data & Hand Calculations:
 *  - Recipe "plastic" (Refinery, speed = 1.0):
 *      Inputs: [3.0 crude-oil], Outputs: [2.0 plastic, 1.0 heavy-oil-residue], Time: 6.0 s.
 *      Target plastic output = 60.0 / min.
 *      Required crude-oil input = 60.0 * (3.0 / 2.0) = 90.0 / min.
 *      Byproduct heavy-oil-residue output = 60.0 * (1.0 / 2.0) = 30.0 / min.
 *      input_per_machine = (3.0 / 6.0) * 1.0 * 60 = 30.0 crude/min.
 *      machine_count = 90.0 / 30.0 = 3.0.
 *  - Recipe "residual-fuel" (Refinery, speed = 1.0):
 *      Inputs: [6.0 heavy-oil-residue], Outputs: [4.0 fuel], Time: 6.0 s.
 *      Target fuel output = 10.0 / min.
 *      Required heavy-oil-residue input = 10.0 * (6.0 / 4.0) = 15.0 / min.
 *      input_per_machine = (6.0 / 6.0) * 1.0 * 60 = 60.0 hor/min.
 *      machine_count = 15.0 / 60.0 = 0.25.
 *  - Excess Byproduct on Plastic Refinery out[1] ("heavy-oil-residue"):
 *      Total produced = 30.0 / min.
 *      Traversing connection to Fuel Refinery = 15.0 / min.
 *      Unconsumed excess Ep = 30.0 - 15.0 = 15.0 / min.
 *  - Recipe "crude-oil" (Resource Well / Oil Extractor, speed = 1.0):
 *      Output = 90.0 / min.
 *      output_per_machine = (1.0 / 1.0) * 60 = 60.0 crude/min.
 *      machine_count = 90.0 / 60.0 = 1.5.
 */
TEST_F(GoldenTest, SolverGolden_Chain3_OilRefining_ByproductAndExcess) {
    FactoryGraph graph(satisfactoryGameData);

    uint64_t n_oil = graph.addNode("Oil Extractor", "crude-oil");
    uint64_t n_plastic = graph.addNode("Plastic Refinery", "plastic");
    uint64_t n_fuel = graph.addNode("Fuel Refinery", "residual-fuel");

    Node* oil = graph.getNode(n_oil);
    Node* plastic = graph.getNode(n_plastic);
    Node* fuel = graph.getNode(n_fuel);
    ASSERT_NE(oil, nullptr);
    ASSERT_NE(plastic, nullptr);
    ASSERT_NE(fuel, nullptr);

    // plastic->output_ports[0] is "plastic", plastic->output_ports[1] is "heavy-oil-residue"
    uint64_t c_oil = graph.addConnection(oil->output_ports[0], plastic->input_ports[0]);
    uint64_t c_hor = graph.addConnection(plastic->output_ports[1], fuel->input_ports[0]);
    ASSERT_NE(c_oil, (uint64_t)-1);
    ASSERT_NE(c_hor, (uint64_t)-1);

    graph.setPortConstraint(plastic->output_ports[0], 60.0);
    graph.setPortConstraint(fuel->output_ports[0], 10.0);

    FactorySolver solver;
    auto result = solver.solve(graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    // Port rates
    EXPECT_NEAR(graph.getPort(plastic->output_ports[0])->rate, 60.0, 0.001); // plastic
    EXPECT_NEAR(graph.getPort(plastic->output_ports[1])->rate, 30.0, 0.001); // heavy-oil-residue
    EXPECT_NEAR(graph.getPort(plastic->input_ports[0])->rate, 90.0, 0.001);  // crude-oil
    EXPECT_NEAR(graph.getPort(fuel->output_ports[0])->rate, 10.0, 0.001);    // fuel
    EXPECT_NEAR(graph.getPort(fuel->input_ports[0])->rate, 15.0, 0.001);     // heavy-oil-residue
    EXPECT_NEAR(graph.getPort(oil->output_ports[0])->rate, 90.0, 0.001);

    // Excess rate on heavy-oil-residue port
    EXPECT_NEAR(graph.getPort(plastic->output_ports[1])->excess_rate, 15.0, 0.001);
    EXPECT_NEAR(graph.getPort(plastic->output_ports[0])->excess_rate, 0.0, 0.001);

    // Machine counts
    EXPECT_NEAR(plastic->machine_count, 3.0, 0.001);
    EXPECT_NEAR(fuel->machine_count, 0.25, 0.001);
    EXPECT_NEAR(oil->machine_count, 1.5, 0.001);

    // Connection rates
    EXPECT_NEAR(graph.getConnection(c_oil)->rate, 90.0, 0.001);
    EXPECT_NEAR(graph.getConnection(c_hor)->rate, 15.0, 0.001);
}

/**
 * Chain 4: Split & Merge with Fair Branch Balancing
 *
 * Topology:
 *   1 Miner ("iron-ore") splits to 2 identical Smelters (A and B, "iron-ingot").
 *   Both smelters merge into 1 Constructor ("iron-plate").
 *   Target on Constructor output: 20.0 Iron Plate / min.
 *
 * Recipe data & Hand Calculations:
 *  - Constructor ("iron-plate"):
 *      Target = 20.0 / min.
 *      Required ingot input = 20.0 * (3.0 / 2.0) = 30.0 / min.
 *      machine_count = 30.0 / 30.0 = 1.0.
 *  - Fair branch balancing (Phase 3 objective 1e-4 * (hi - lo)):
 *      Because Smelter A and Smelter B are identical with symmetrical topologies,
 *      Phase 3 penalizes branch differences, forcing:
 *        Smelter A -> Constructor = 15.0 / min.
 *        Smelter B -> Constructor = 15.0 / min.
 *        Miner -> Smelter A = 15.0 / min.
 *        Miner -> Smelter B = 15.0 / min.
 *  - Smelter A & B:
 *      Output = 15.0 / min, Input = 15.0 / min.
 *      machine_count = 15.0 / 30.0 = 0.5 each.
 *  - Miner:
 *      Output = 30.0 / min.
 *      machine_count = 30.0 / 60.0 = 0.5.
 */
TEST_F(GoldenTest, SolverGolden_Chain4_SplitAndMerge_FairBalancing) {
    FactoryGraph graph(satisfactoryGameData);

    uint64_t n_miner = graph.addNode("Miner", "iron-ore");
    uint64_t n_smelter_a = graph.addNode("Smelter A", "iron-ingot");
    uint64_t n_smelter_b = graph.addNode("Smelter B", "iron-ingot");
    uint64_t n_const = graph.addNode("Constructor", "iron-plate");

    Node* miner = graph.getNode(n_miner);
    Node* smelter_a = graph.getNode(n_smelter_a);
    Node* smelter_b = graph.getNode(n_smelter_b);
    Node* plate_const = graph.getNode(n_const);

    // Split from miner
    uint64_t c_split_a = graph.addConnection(miner->output_ports[0], smelter_a->input_ports[0]);
    uint64_t c_split_b = graph.addConnection(miner->output_ports[0], smelter_b->input_ports[0]);

    // Merge into constructor
    uint64_t c_merge_a = graph.addConnection(smelter_a->output_ports[0], plate_const->input_ports[0]);
    uint64_t c_merge_b = graph.addConnection(smelter_b->output_ports[0], plate_const->input_ports[0]);

    // Demand 20.0 plates/min
    graph.setPortConstraint(plate_const->output_ports[0], 20.0);

    FactorySolver solver;
    auto result = solver.solve(graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    // Constructor rates & machines
    EXPECT_NEAR(graph.getPort(plate_const->output_ports[0])->rate, 20.0, 0.001);
    EXPECT_NEAR(graph.getPort(plate_const->input_ports[0])->rate, 30.0, 0.001);
    EXPECT_NEAR(plate_const->machine_count, 1.0, 0.001);

    // Symmetrical split and merge branch flow rates
    EXPECT_NEAR(graph.getConnection(c_split_a)->rate, 15.0, 0.001);
    EXPECT_NEAR(graph.getConnection(c_split_b)->rate, 15.0, 0.001);
    EXPECT_NEAR(graph.getConnection(c_merge_a)->rate, 15.0, 0.001);
    EXPECT_NEAR(graph.getConnection(c_merge_b)->rate, 15.0, 0.001);

    // Smelter rates & machine counts
    EXPECT_NEAR(graph.getPort(smelter_a->output_ports[0])->rate, 15.0, 0.001);
    EXPECT_NEAR(graph.getPort(smelter_a->input_ports[0])->rate, 15.0, 0.001);
    EXPECT_NEAR(smelter_a->machine_count, 0.5, 0.001);

    EXPECT_NEAR(graph.getPort(smelter_b->output_ports[0])->rate, 15.0, 0.001);
    EXPECT_NEAR(graph.getPort(smelter_b->input_ports[0])->rate, 15.0, 0.001);
    EXPECT_NEAR(smelter_b->machine_count, 0.5, 0.001);

    // Miner rates & machine count
    EXPECT_NEAR(graph.getPort(miner->output_ports[0])->rate, 30.0, 0.001);
    EXPECT_NEAR(miner->machine_count, 0.5, 0.001);
}

/**
 * Chain 5: Constrained Middle Stage (Bottleneck / Capacity Limit Propagation)
 *
 * Topology: Miner -> Smelter -> Constructor ("iron-plate").
 * Downstream Constructor wants TARGET = 40.0 Iron Plate / min (demanding 60.0 ingot).
 * Smelter input port has LIMIT = 30.0 Iron Ore / min.
 *
 * Recipe data & Hand Calculations:
 *  - Smelter input is bounded by user_constraint <= 30.0.
 *  - Ingot output cannot exceed 30.0 / min (1:1 recipe).
 *  - Plate Constructor takes 1.5 ingot per plate:
 *      Maximum plate output = 30.0 / 1.5 = 20.0 plates/min.
 *      Saturation S = 20.0 / 40.0 = 0.5.
 *  - Machine counts:
 *      Constructor: 30.0 / 30.0 = 1.0.
 *      Smelter: 30.0 / 30.0 = 1.0.
 *      Miner: 30.0 / 60.0 = 0.5.
 */
TEST_F(GoldenTest, SolverGolden_Chain5_ConstrainedMiddleStage_BottleneckPropagation) {
    FactoryGraph graph(satisfactoryGameData);

    uint64_t n_miner = graph.addNode("Miner", "iron-ore");
    uint64_t n_smelter = graph.addNode("Smelter", "iron-ingot");
    uint64_t n_const = graph.addNode("Constructor", "iron-plate");

    Node* miner = graph.getNode(n_miner);
    Node* smelter = graph.getNode(n_smelter);
    Node* plate_const = graph.getNode(n_const);

    uint64_t c1 = graph.addConnection(miner->output_ports[0], smelter->input_ports[0]);
    uint64_t c2 = graph.addConnection(smelter->output_ports[0], plate_const->input_ports[0]);
    ASSERT_NE(c1, (uint64_t)-1);
    ASSERT_NE(c2, (uint64_t)-1);

    // Middle stage capacity limit: Smelter input <= 30.0
    graph.setPortConstraint(smelter->input_ports[0], 30.0);

    // High downstream target demand: 40.0 plates/min
    graph.setPortConstraint(plate_const->output_ports[0], 40.0);

    FactorySolver solver;
    auto result = solver.solve(graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    // Throughput throttled by the 30.0 ingot bottleneck
    EXPECT_NEAR(graph.getPort(plate_const->output_ports[0])->rate, 20.0, 0.001);
    EXPECT_NEAR(graph.getPort(plate_const->input_ports[0])->rate, 30.0, 0.001);
    EXPECT_NEAR(graph.getPort(smelter->output_ports[0])->rate, 30.0, 0.001);
    EXPECT_NEAR(graph.getPort(smelter->input_ports[0])->rate, 30.0, 0.001);
    EXPECT_NEAR(graph.getPort(miner->output_ports[0])->rate, 30.0, 0.001);

    EXPECT_NEAR(plate_const->machine_count, 1.0, 0.001);
    EXPECT_NEAR(smelter->machine_count, 1.0, 0.001);
    EXPECT_NEAR(miner->machine_count, 0.5, 0.001);

    EXPECT_NEAR(graph.getConnection(c1)->rate, 30.0, 0.001);
    EXPECT_NEAR(graph.getConnection(c2)->rate, 30.0, 0.001);
}

/**
 * Chain 6: Overclocking, Somersloop Multiplier, and Disabled (0% Clock) Node
 *
 * Topology:
 *   - Miner ("iron-ore"): clock_speed = 150.0% (overclocked).
 *   - Smelter ("iron-ingot"): production_multiplier = 200.0% (Somersloop doubling).
 *   - Constructor Active ("iron-plate"): clock = 100%, prod = 100%, Target = 20.0 plate/min.
 *   - Constructor Disabled ("iron-plate"): clock_speed = 0.0% (disabled node).
 *     Connected to Smelter output in parallel with Active Constructor.
 *
 * Recipe data & Hand Calculations:
 *  - Disabled Constructor:
 *      clock_speed <= 0.0 forces all port rates to 0.0 in addRecipeConstraints.
 *      machine_count = 0.0.
 *      Incoming connection flow = 0.0.
 *  - Active Constructor:
 *      Target plate output = 20.0 / min.
 *      Required ingot input = 20.0 * 1.5 = 30.0 / min.
 *      machine_count = 30.0 / 30.0 = 1.0.
 *  - Smelter (production_multiplier = 200.0%):
 *      Total ingot output = 30.0 / min.
 *      Recipe ratio with Somersloop:
 *        R_in * (recipe_out * prod_mult / 100) = R_out * recipe_in
 *        R_in * (1.0 * 2.0) = R_out * 1.0  ==>  R_in = 30.0 / 2.0 = 15.0 ore/min!
 *      Smelter machine count (Section 6.3 Branch 2):
 *        input_per_machine = (1.0 / 2.0) * 1.0 * 60 * (100 / 100) = 30.0 ore/min.
 *        machine_count = 15.0 / 30.0 = 0.5.
 *  - Miner (clock_speed = 150.0%):
 *      Output rate = 15.0 ore/min.
 *      Miner machine count (Section 6.3 Branch 1):
 *        output_per_machine = (1.0 / 1.0) * 1.0 * 60 * (150.0 / 100.0) = 90.0 ore/min.
 *        machine_count = 15.0 / 90.0 = 1/6 ~= 0.1666667.
 */
TEST_F(GoldenTest, SolverGolden_Chain6_OverclockSomersloopAndDisabledNode) {
    FactoryGraph graph(satisfactoryGameData);

    uint64_t n_miner = graph.addNode("Miner", "iron-ore");
    uint64_t n_smelter = graph.addNode("Smelter", "iron-ingot");
    uint64_t n_active = graph.addNode("Active Constructor", "iron-plate");
    uint64_t n_disabled = graph.addNode("Disabled Constructor", "iron-plate");

    Node* miner = graph.getNode(n_miner);
    Node* smelter = graph.getNode(n_smelter);
    Node* active = graph.getNode(n_active);
    Node* disabled = graph.getNode(n_disabled);

    miner->clock_speed = 150.0;
    smelter->production_multiplier = 200.0;
    disabled->clock_speed = 0.0;

    uint64_t c_mine = graph.addConnection(miner->output_ports[0], smelter->input_ports[0]);
    uint64_t c_active = graph.addConnection(smelter->output_ports[0], active->input_ports[0]);
    uint64_t c_disabled = graph.addConnection(smelter->output_ports[0], disabled->input_ports[0]);
    ASSERT_NE(c_mine, (uint64_t)-1);
    ASSERT_NE(c_active, (uint64_t)-1);
    ASSERT_NE(c_disabled, (uint64_t)-1);

    graph.setPortConstraint(active->output_ports[0], 20.0);

    FactorySolver solver;
    auto result = solver.solve(graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    // Disabled node rates and machine count are strictly zero
    EXPECT_NEAR(graph.getPort(disabled->input_ports[0])->rate, 0.0, 0.001);
    EXPECT_NEAR(graph.getPort(disabled->output_ports[0])->rate, 0.0, 0.001);
    EXPECT_NEAR(disabled->machine_count, 0.0, 0.001);
    EXPECT_NEAR(graph.getConnection(c_disabled)->rate, 0.0, 0.001);

    // Active constructor
    EXPECT_NEAR(graph.getPort(active->output_ports[0])->rate, 20.0, 0.001);
    EXPECT_NEAR(graph.getPort(active->input_ports[0])->rate, 30.0, 0.001);
    EXPECT_NEAR(active->machine_count, 1.0, 0.001);
    EXPECT_NEAR(graph.getConnection(c_active)->rate, 30.0, 0.001);

    // Smelter with 200% production multiplier takes half the ore (15.0 ore -> 30.0 ingot)
    EXPECT_NEAR(graph.getPort(smelter->output_ports[0])->rate, 30.0, 0.001);
    EXPECT_NEAR(graph.getPort(smelter->input_ports[0])->rate, 15.0, 0.001);
    EXPECT_NEAR(smelter->machine_count, 0.5, 0.001);
    EXPECT_NEAR(graph.getConnection(c_mine)->rate, 15.0, 0.001);

    // Miner at 150% clock speed
    EXPECT_NEAR(graph.getPort(miner->output_ports[0])->rate, 15.0, 0.001);
    EXPECT_NEAR(miner->machine_count, 15.0 / 90.0, 0.001); // 1/6
}

/**
 * Chain 7: Complex Closed-Loop Byproduct Recycling — Aluminum Production with Water Loop
 *
 * Topology:
 *   - Bauxite Miner ("bauxite") -> Alumina Refinery (in0: bauxite)
 *   - Fresh Water Extractor ("water") -> Alumina Refinery (in1: water)
 *   - Coal Miner ("coal") -> Scrap Refinery (in1: coal)
 *   - Alumina Refinery (out0: alumina-solution) -> Scrap Refinery (in0: alumina-solution)
 *   - Scrap Refinery (out1: byproduct water) -> Alumina Refinery (in1: water) [RECYCLED FEEDBACK LOOP!]
 * Target: 360.0 Aluminum Scrap / min on Scrap Refinery out0.
 *
 * Recipe data & Hand Calculations:
 *  - Recipe "aluminum-scrap" (Refinery, speed = 1.0):
 *      Inputs: [4.0 alumina-solution, 2.0 coal], Outputs: [6.0 aluminum-scrap, 2.0 water], Time: 1.0 s.
 *      Target scrap rate = 360.0 / min.
 *      Required alumina-solution = 360.0 * (4.0 / 6.0) = 240.0 / min.
 *      Required coal = 360.0 * (2.0 / 6.0) = 120.0 / min.
 *      Byproduct water produced = 360.0 * (2.0 / 6.0) = 120.0 / min.
 *      input_per_machine = (4.0 / 1.0) * 1.0 * 60 = 240.0 alumina/min.
 *      machine_count = 240.0 / 240.0 = 1.0.
 *  - Recipe "alumina-solution" (Refinery, speed = 1.0):
 *      Inputs: [12.0 bauxite, 18.0 water], Outputs: [12.0 alumina-solution, 5.0 silica], Time: 6.0 s.
 *      Alumina-solution output = 240.0 / min.
 *      Required bauxite = 240.0 * (12.0 / 12.0) = 240.0 / min.
 *      Total water needed = 240.0 * (18.0 / 12.0) = 360.0 / min.
 *      Silica byproduct produced = 240.0 * (5.0 / 12.0) = 100.0 / min.
 *      input_per_machine = (12.0 / 6.0) * 1.0 * 60 = 120.0 bauxite/min.
 *      machine_count = 240.0 / 120.0 = 2.0.
 *  - Water conservation at Alumina Refinery in[1]:
 *      Total water needed = 360.0 / min.
 *      Recycled water from Scrap Refinery out[1] = 120.0 / min (prioritized to eliminate excess Ep waste).
 *      Fresh water needed from Water Extractor = 360.0 - 120.0 = 240.0 / min.
 *  - Fresh Water Extractor (speed = 1.0):
 *      Output = 240.0 / min.
 *      output_per_machine = (1.0 / 1.0) * 60 = 60.0 water/min.
 *      machine_count = 240.0 / 60.0 = 4.0.
 *  - Bauxite Miner (Miner Mk1, speed = 1.0):
 *      Output = 240.0 / min.
 *      output_per_machine = 60.0 ore/min.
 *      machine_count = 240.0 / 60.0 = 4.0.
 *  - Coal Miner (Miner Mk1, speed = 1.0):
 *      Output = 120.0 / min.
 *      output_per_machine = 60.0 coal/min.
 *      machine_count = 120.0 / 60.0 = 2.0.
 */
TEST_F(GoldenTest, SolverGolden_Chain7_Aluminum_ClosedLoopWaterRecycling) {
    FactoryGraph graph(satisfactoryGameData);

    uint64_t n_bauxite_miner = graph.addNode("Bauxite Miner", "bauxite");
    uint64_t n_water_ext = graph.addNode("Water Extractor", "water");
    uint64_t n_coal_miner = graph.addNode("Coal Miner", "coal");
    uint64_t n_alumina_ref = graph.addNode("Alumina Refinery", "alumina-solution");
    uint64_t n_scrap_ref = graph.addNode("Scrap Refinery", "aluminum-scrap");

    Node* bauxite_miner = graph.getNode(n_bauxite_miner);
    Node* water_ext = graph.getNode(n_water_ext);
    Node* coal_miner = graph.getNode(n_coal_miner);
    Node* alumina_ref = graph.getNode(n_alumina_ref);
    Node* scrap_ref = graph.getNode(n_scrap_ref);

    // alumina_ref: in[0] is bauxite, in[1] is water; out[0] is alumina-solution, out[1] is silica
    // scrap_ref: in[0] is alumina-solution, in[1] is coal; out[0] is aluminum-scrap, out[1] is water
    uint64_t c_bauxite = graph.addConnection(bauxite_miner->output_ports[0], alumina_ref->input_ports[0]);
    uint64_t c_fresh_water = graph.addConnection(water_ext->output_ports[0], alumina_ref->input_ports[1]);
    uint64_t c_alumina = graph.addConnection(alumina_ref->output_ports[0], scrap_ref->input_ports[0]);
    uint64_t c_coal = graph.addConnection(coal_miner->output_ports[0], scrap_ref->input_ports[1]);

    // Recycled water feedback loop: scrap_ref out[1] -> alumina_ref in[1]
    uint64_t c_recycled_water = graph.addConnection(scrap_ref->output_ports[1], alumina_ref->input_ports[1]);

    ASSERT_NE(c_bauxite, (uint64_t)-1);
    ASSERT_NE(c_fresh_water, (uint64_t)-1);
    ASSERT_NE(c_alumina, (uint64_t)-1);
    ASSERT_NE(c_coal, (uint64_t)-1);
    ASSERT_NE(c_recycled_water, (uint64_t)-1);

    // Target 360.0 aluminum scrap / min
    graph.setPortConstraint(scrap_ref->output_ports[0], 360.0);

    FactorySolver solver;
    auto result = solver.solve(graph);
    ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);

    // Scrap refinery rates
    EXPECT_NEAR(graph.getPort(scrap_ref->output_ports[0])->rate, 360.0, 0.001); // scrap
    EXPECT_NEAR(graph.getPort(scrap_ref->output_ports[1])->rate, 120.0, 0.001); // byproduct water
    EXPECT_NEAR(graph.getPort(scrap_ref->input_ports[0])->rate, 240.0, 0.001);  // alumina-solution
    EXPECT_NEAR(graph.getPort(scrap_ref->input_ports[1])->rate, 120.0, 0.001);  // coal
    EXPECT_NEAR(scrap_ref->machine_count, 1.0, 0.001);

    // Alumina refinery rates
    EXPECT_NEAR(graph.getPort(alumina_ref->output_ports[0])->rate, 240.0, 0.001); // alumina-solution
    EXPECT_NEAR(graph.getPort(alumina_ref->output_ports[1])->rate, 100.0, 0.001); // byproduct silica
    EXPECT_NEAR(graph.getPort(alumina_ref->input_ports[0])->rate, 240.0, 0.001);  // bauxite
    EXPECT_NEAR(graph.getPort(alumina_ref->input_ports[1])->rate, 360.0, 0.001);  // total water
    EXPECT_NEAR(alumina_ref->machine_count, 2.0, 0.001);

    // Water flow rates: recycled vs fresh
    EXPECT_NEAR(graph.getConnection(c_recycled_water)->rate, 120.0, 0.001);
    EXPECT_NEAR(graph.getConnection(c_fresh_water)->rate, 240.0, 0.001);
    EXPECT_NEAR(graph.getPort(scrap_ref->output_ports[1])->excess_rate, 0.0, 0.001); // 100% recycled

    // Upstream raw producers
    EXPECT_NEAR(graph.getPort(water_ext->output_ports[0])->rate, 240.0, 0.001);
    EXPECT_NEAR(water_ext->machine_count, 4.0, 0.001);

    EXPECT_NEAR(graph.getPort(bauxite_miner->output_ports[0])->rate, 240.0, 0.001);
    EXPECT_NEAR(bauxite_miner->machine_count, 4.0, 0.001);

    EXPECT_NEAR(graph.getPort(coal_miner->output_ports[0])->rate, 120.0, 0.001);
    EXPECT_NEAR(coal_miner->machine_count, 2.0, 0.001);

    EXPECT_NEAR(graph.getConnection(c_bauxite)->rate, 240.0, 0.001);
    EXPECT_NEAR(graph.getConnection(c_coal)->rate, 120.0, 0.001);
    EXPECT_NEAR(graph.getConnection(c_alumina)->rate, 240.0, 0.001);
}
