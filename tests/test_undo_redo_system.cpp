#include <gtest/gtest.h>
#include "services/UndoRedoSystem.h"
#include "core/graph/FactoryGraph.h" // Include the required FactoryGraph
#include "core/data/GameData.h"       // Include GameData for the graph
#include "common/IdUtils.h"
#include <imgui.h>
#include "imgui_node_editor.h"
#include <memory>

// A simple integer value that our commands will modify
struct TestState {
    int value = 0;
};

// A mock command that implements the *correct* Command interface
class ValueChangeCommand : public Command {
public:
    ValueChangeCommand(TestState& state, int newValue, std::string name = "ValueChangeCommand")
        : m_state(state), m_newValue(newValue), m_oldValue(state.value), m_name(std::move(name)) {}

    // Correctly implement the interface with (FactoryGraph& graph)
    void execute(FactoryGraph& graph) override {
        m_state.value = m_newValue;
    }

    void undo(FactoryGraph& graph) override {
        m_state.value = m_oldValue;
    }

    // Correctly implement getDescription
    std::string getDescription() const override { return m_name; }

private:
    TestState& m_state;
    int m_newValue;
    int m_oldValue;
    std::string m_name;
};

// Test fixture for the UndoRedoManager
class UndoRedoTest : public ::testing::Test {
protected:
    TestState state;
    std::unique_ptr<UndoRedoManager> manager;
    const size_t maxHistory = 5;

    // We need a dummy graph and gamedata to pass to the manager
    GameData testGameData;
    std::unique_ptr<FactoryGraph> graph;


    void SetUp() override {
        state.value = 0;
        manager = std::make_unique<UndoRedoManager>(maxHistory);
        // Initialize the graph with empty data
        graph = std::make_unique<FactoryGraph>(testGameData);
    }

    void TearDown() override {
        manager.reset();
        graph.reset();
    }

    // Helper to add and execute a command
    void addCommand(int newValue) {
        auto cmd = std::make_unique<ValueChangeCommand>(state, newValue);
        // Call the correct method: executeCommand
        manager->executeCommand(std::move(cmd), *graph);
    }
};

TEST_F(UndoRedoTest, InitialState) {
    EXPECT_FALSE(manager->canUndo());
    EXPECT_FALSE(manager->canRedo());
    EXPECT_EQ(manager->getUndoStackSize(), 0);
    EXPECT_EQ(manager->getRedoStackSize(), 0);
}

TEST_F(UndoRedoTest, AddCommand) {
    addCommand(10);

    EXPECT_TRUE(manager->canUndo());
    EXPECT_FALSE(manager->canRedo());
    EXPECT_EQ(state.value, 10);
    EXPECT_EQ(manager->getUndoStackSize(), 1);
    EXPECT_EQ(manager->getRedoStackSize(), 0);
}

TEST_F(UndoRedoTest, Undo) {
    addCommand(10);

    manager->undo(*graph); // Pass the graph

    EXPECT_FALSE(manager->canUndo());
    EXPECT_TRUE(manager->canRedo());
    EXPECT_EQ(state.value, 0); // Value is reverted
    EXPECT_EQ(manager->getUndoStackSize(), 0);
    EXPECT_EQ(manager->getRedoStackSize(), 1);
}

TEST_F(UndoRedoTest, Redo) {
    addCommand(10);
    manager->undo(*graph);

    ASSERT_TRUE(manager->canRedo());
    manager->redo(*graph); // Pass the graph

    EXPECT_TRUE(manager->canUndo());
    EXPECT_FALSE(manager->canRedo());
    EXPECT_EQ(state.value, 10); // Value is re-applied
    EXPECT_EQ(manager->getUndoStackSize(), 1);
    EXPECT_EQ(manager->getRedoStackSize(), 0);
}

TEST_F(UndoRedoTest, AddCommandClearsRedoStack) {
    addCommand(10);
    addCommand(20);
    manager->undo(*graph); // state is 10, redo stack has {cmd(20)}

    ASSERT_TRUE(manager->canRedo());
    EXPECT_EQ(state.value, 10);

    addCommand(30); // state is 30

    EXPECT_FALSE(manager->canRedo());
    EXPECT_EQ(manager->getRedoStackSize(), 0);
    EXPECT_EQ(manager->getUndoStackSize(), 2);

    manager->undo(*graph); // state is 10
    EXPECT_EQ(state.value, 10);

    manager->undo(*graph); // state is 0
    EXPECT_EQ(state.value, 0);

    EXPECT_FALSE(manager->canUndo());
}

TEST_F(UndoRedoTest, UndoRedoMultiple) {
    addCommand(10);
    addCommand(20);
    addCommand(30);
    EXPECT_EQ(state.value, 30);

    manager->undo(*graph);
    EXPECT_EQ(state.value, 20);

    manager->undo(*graph);
    EXPECT_EQ(state.value, 10);

    manager->redo(*graph);
    EXPECT_EQ(state.value, 20);

    manager->undo(*graph);
    EXPECT_EQ(state.value, 10);

    manager->undo(*graph);
    EXPECT_EQ(state.value, 0);

    EXPECT_FALSE(manager->canUndo());
    EXPECT_TRUE(manager->canRedo());

    manager->redo(*graph);
    EXPECT_EQ(state.value, 10);

    manager->redo(*graph);
    EXPECT_EQ(state.value, 20);

    manager->redo(*graph);
    EXPECT_EQ(state.value, 30);

    EXPECT_TRUE(manager->canUndo());
    EXPECT_FALSE(manager->canRedo());
}

TEST_F(UndoRedoTest, HistoryLimit) {
    // maxHistory is 5
    addCommand(1); // cmd 1
    addCommand(2); // cmd 2
    addCommand(3); // cmd 3
    addCommand(4); // cmd 4
    addCommand(5); // cmd 5

    EXPECT_EQ(manager->getUndoStackSize(), 5);
    EXPECT_EQ(state.value, 5);

    addCommand(6); // cmd 6, cmd 1 should be dropped

    EXPECT_EQ(manager->getUndoStackSize(), 5);
    EXPECT_EQ(state.value, 6);

    manager->undo(*graph); // state is 5
    manager->undo(*graph); // state is 4
    manager->undo(*graph); // state is 3
    manager->undo(*graph); // state is 2
    manager->undo(*graph); // state is 1

    EXPECT_EQ(state.value, 1);

    EXPECT_FALSE(manager->canUndo());
    EXPECT_EQ(manager->getUndoStackSize(), 0);

    manager->redo(*graph);
    EXPECT_EQ(state.value, 2);
}

TEST_F(UndoRedoTest, UndoRedoOnEmptyStack) {
    manager->undo(*graph);
    manager->redo(*graph);

    EXPECT_FALSE(manager->canUndo());
    EXPECT_FALSE(manager->canRedo());

    addCommand(10);
    manager->undo(*graph);

    EXPECT_TRUE(manager->canRedo());
    manager->redo(*graph);

    EXPECT_FALSE(manager->canRedo());
    manager->redo(*graph); // Should do nothing
    EXPECT_FALSE(manager->canRedo());
}

TEST_F(UndoRedoTest, GetCommandDescriptions) {
    EXPECT_EQ(manager->getUndoDescription(), "");
    EXPECT_EQ(manager->getRedoDescription(), "");

    auto cmd1 = std::make_unique<ValueChangeCommand>(state, 10, "First Command");
    manager->executeCommand(std::move(cmd1), *graph);

    EXPECT_EQ(manager->getUndoDescription(), "First Command");
    EXPECT_EQ(manager->getRedoDescription(), "");

    manager->undo(*graph);

    EXPECT_EQ(manager->getUndoDescription(), "");
    EXPECT_EQ(manager->getRedoDescription(), "First Command");

    manager->redo(*graph);

    EXPECT_EQ(manager->getUndoDescription(), "First Command");
    EXPECT_EQ(manager->getRedoDescription(), "");
}

TEST_F(UndoRedoTest, CompositeCommandUndo) {
    auto composite = std::make_unique<CompositeCommand>("Batch Change");
    composite->addCommand(std::make_unique<ValueChangeCommand>(state, 10, "Set 10"));
    composite->addCommand(std::make_unique<ValueChangeCommand>(state, 20, "Set 20"));

    EXPECT_FALSE(composite->isEmpty());

    manager->executeCommand(std::move(composite), *graph);

    EXPECT_EQ(state.value, 20);
    EXPECT_TRUE(manager->canUndo());
    EXPECT_EQ(manager->getUndoDescription(), "Batch Change");

    // Undo the composite command
    manager->undo(*graph);

    // Should be back to the original state (0)
    // The undo order is reversed: 20 -> 10, then 10 -> 0
    EXPECT_EQ(state.value, 0);
    EXPECT_TRUE(manager->canRedo());

    // Redo the composite command
    manager->redo(*graph);
    EXPECT_EQ(state.value, 20);
}

TEST_F(UndoRedoTest, EmptyCompositeCommand) {
    auto composite = std::make_unique<CompositeCommand>("Empty");
    EXPECT_TRUE(composite->isEmpty());

    manager->executeCommand(std::move(composite), *graph);

    EXPECT_EQ(state.value, 0);
    EXPECT_TRUE(manager->canUndo());

    manager->undo(*graph);
    EXPECT_EQ(state.value, 0);
    EXPECT_TRUE(manager->canRedo());

    manager->redo(*graph);
    EXPECT_EQ(state.value, 0);
}

// ============================================================================
// Concrete Commands Test Suite (Audit Item 3.10)
// ============================================================================

class ConcreteCommandsTest : public ::testing::Test {
protected:
    ImGuiContext* m_imguiContext = nullptr;
    ax::NodeEditor::EditorContext* m_editorContext = nullptr;
    GameData testGameData;
    std::unique_ptr<FactoryGraph> graph;
    std::unique_ptr<UndoRedoManager> manager;

    void SetUp() override {
        m_imguiContext = ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ax::NodeEditor::Config cfg;
        cfg.SettingsFile = nullptr;
        m_editorContext = ax::NodeEditor::CreateEditor(&cfg);
        ax::NodeEditor::SetCurrentEditor(m_editorContext);

        testGameData.game.id = "test_game";
        testGameData.game.name = "TestGame";
        testGameData.game.data_version = "1.0";
        testGameData.units.recipe_time = gamedata::RecipeTimeUnit::Seconds;
        testGameData.units.default_rate = gamedata::RateUnit::PerSecond;
        gamedata::ensureV3CompatProfile(testGameData);

        gamedata::upsertResource(testGameData, "iron_ore", "Iron Ore");
        gamedata::upsertResource(testGameData, "iron_ingot", "Iron Ingot");
        gamedata::upsertResource(testGameData, "iron_plate", "Iron Plate");
        gamedata::upsertResource(testGameData, "screw", "Screw");

        gamedata::upsertMachine(testGameData, "miner", "Miner", 1.0, "v3_compat");
        gamedata::upsertMachine(testGameData, "smelter", "Smelter", 1.0, "v3_compat");
        gamedata::upsertMachine(testGameData, "constructor", "Constructor", 1.0, "v3_compat");

        gamedata::upsertRecipe(testGameData, "mine_ore", "Mine Iron Ore", 1.0, {"miner"},
                               {}, {{"iron_ore", 30.0}});
        gamedata::upsertRecipe(testGameData, "smelt_ingot", "Smelt Iron Ingot", 1.0, {"smelter"},
                               {{"iron_ore", 30.0}}, {{"iron_ingot", 30.0}});
        gamedata::upsertRecipe(testGameData, "make_plates", "Make Plates", 1.0, {"constructor"},
                               {{"iron_ingot", 30.0}}, {{"iron_plate", 20.0}});

        graph = std::make_unique<FactoryGraph>(testGameData);
        manager = std::make_unique<UndoRedoManager>(10);
    }

    void TearDown() override {
        manager.reset();
        graph.reset();
        ax::NodeEditor::SetCurrentEditor(nullptr);
        if (m_editorContext) {
            ax::NodeEditor::DestroyEditor(m_editorContext);
            m_editorContext = nullptr;
        }
        if (m_imguiContext) {
            ImGui::DestroyContext(m_imguiContext);
            m_imguiContext = nullptr;
        }
    }
};

TEST_F(ConcreteCommandsTest, AddNodeCommand_Standalone_UndoRedo) {
    auto cmd = std::make_unique<AddNodeCommand>("Mine Iron Ore", "mine_ore", (uint64_t)-1, ImVec2(100.0f, 200.0f));
    EXPECT_EQ(cmd->getDescription(), "Add Node: Mine Iron Ore");
    EXPECT_TRUE(cmd->getFlags().needsSolve);
    EXPECT_TRUE(cmd->getFlags().needsRebuild);

    manager->executeCommand(std::move(cmd), *graph);

    ASSERT_EQ(graph->getNodes().size(), 1);
    uint64_t nodeId = graph->getNodes()[0].id;
    const Node* node = graph->getNode(nodeId);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->name, "Mine Iron Ore");
    EXPECT_EQ(node->selected_recipe_key, "mine_ore");
    EXPECT_EQ(node->output_ports.size(), 1);

    // Undo: node and its ports should be removed
    manager->undo(*graph);
    EXPECT_TRUE(graph->getNodes().empty());
    EXPECT_TRUE(graph->getPorts().empty());

    // Redo: node and its ports should be restored with identical ID
    manager->redo(*graph);
    ASSERT_EQ(graph->getNodes().size(), 1);
    EXPECT_NE(graph->getNode(nodeId), nullptr);
    EXPECT_EQ(graph->getNode(nodeId)->output_ports.size(), 1);
    ImVec2 pos = ax::NodeEditor::GetNodePosition(IdUtils::toNodeId(nodeId));
    EXPECT_FLOAT_EQ(pos.x, 100.0f);
    EXPECT_FLOAT_EQ(pos.y, 200.0f);
}

TEST_F(ConcreteCommandsTest, AddNodeCommand_ConnectedToExistingPort_UndoRedo) {
    uint64_t minerId = graph->addNode("Mine Iron Ore", "mine_ore");
    Node* minerNode = graph->getNode(minerId);
    ASSERT_NE(minerNode, nullptr);
    ASSERT_FALSE(minerNode->output_ports.empty());
    uint64_t minerOutPort = minerNode->output_ports[0];

    // Add smelter connected to miner's output port
    auto cmd = std::make_unique<AddNodeCommand>("Smelt Iron Ingot", "smelt_ingot", minerOutPort, ImVec2(350.0f, 200.0f));
    manager->executeCommand(std::move(cmd), *graph);

    ASSERT_EQ(graph->getNodes().size(), 2);
    ASSERT_EQ(graph->getConnections().size(), 1);

    const Connection& conn = graph->getConnections()[0];
    EXPECT_EQ(conn.from_port, minerOutPort);
    uint64_t smelterId = (graph->getNodes()[0].id == minerId) ? graph->getNodes()[1].id : graph->getNodes()[0].id;
    EXPECT_EQ(conn.to_port, graph->getNode(smelterId)->input_ports[0]);
    EXPECT_EQ(conn.resource_key, "iron_ore");

    // Undo: smelter and connection removed, miner remains
    manager->undo(*graph);
    EXPECT_EQ(graph->getNodes().size(), 1);
    EXPECT_NE(graph->getNode(minerId), nullptr);
    EXPECT_TRUE(graph->getConnections().empty());

    // Redo: smelter and connection restored
    manager->redo(*graph);
    EXPECT_EQ(graph->getNodes().size(), 2);
    ASSERT_EQ(graph->getConnections().size(), 1);
    EXPECT_EQ(graph->getConnections()[0].from_port, minerOutPort);
    EXPECT_EQ(graph->getConnections()[0].to_port, graph->getNode(smelterId)->input_ports[0]);
}

TEST_F(ConcreteCommandsTest, RemoveNodeCommand_UndoRedo) {
    uint64_t minerId = graph->addNode("Mine Iron Ore", "mine_ore");
    uint64_t smelterId = graph->addNode("Smelt Iron Ingot", "smelt_ingot");
    uint64_t outPort = graph->getNode(minerId)->output_ports[0];
    uint64_t inPort = graph->getNode(smelterId)->input_ports[0];
    uint64_t connId = graph->addConnection(outPort, inPort);
    ASSERT_NE(connId, (uint64_t)-1);
    ax::NodeEditor::SetNodePosition(IdUtils::toNodeId(minerId), ImVec2(150.0f, 250.0f));

    ASSERT_EQ(graph->getNodes().size(), 2);
    ASSERT_EQ(graph->getConnections().size(), 1);

    auto cmd = std::make_unique<RemoveNodeCommand>(minerId);
    EXPECT_TRUE(cmd->getFlags().needsSolve);
    EXPECT_TRUE(cmd->getFlags().needsRebuild);

    manager->executeCommand(std::move(cmd), *graph);
    EXPECT_EQ(manager->getUndoDescription(), "Remove Node: Mine Iron Ore");

    // Miner and connection should be gone
    EXPECT_EQ(graph->getNodes().size(), 1);
    EXPECT_EQ(graph->getNode(minerId), nullptr);
    EXPECT_NE(graph->getNode(smelterId), nullptr);
    EXPECT_TRUE(graph->getConnections().empty());

    // Undo: miner and connection should be restored
    manager->undo(*graph);
    EXPECT_EQ(graph->getNodes().size(), 2);
    EXPECT_NE(graph->getNode(minerId), nullptr);
    ASSERT_EQ(graph->getConnections().size(), 1);
    EXPECT_EQ(graph->getConnections()[0].from_port, outPort);
    EXPECT_EQ(graph->getConnections()[0].to_port, inPort);
    ImVec2 pos = ax::NodeEditor::GetNodePosition(IdUtils::toNodeId(minerId));
    EXPECT_FLOAT_EQ(pos.x, 150.0f);
    EXPECT_FLOAT_EQ(pos.y, 250.0f);

    // Redo: miner and connection removed again
    manager->redo(*graph);
    EXPECT_EQ(graph->getNodes().size(), 1);
    EXPECT_EQ(graph->getNode(minerId), nullptr);
    EXPECT_TRUE(graph->getConnections().empty());
}

TEST_F(ConcreteCommandsTest, RemoveNodeCommand_MultiPortNode_UndoRedo) {
    // Test removing a middle node that has both input and output ports & connections
    uint64_t minerId = graph->addNode("Miner", "mine_ore");
    uint64_t smelterId = graph->addNode("Smelter", "smelt_ingot");
    uint64_t constructorId = graph->addNode("Constructor", "make_plates");

    uint64_t minerOut = graph->getNode(minerId)->output_ports[0];
    uint64_t smelterIn = graph->getNode(smelterId)->input_ports[0];
    uint64_t smelterOut = graph->getNode(smelterId)->output_ports[0];
    uint64_t constructorIn = graph->getNode(constructorId)->input_ports[0];

    uint64_t conn1 = graph->addConnection(minerOut, smelterIn);
    uint64_t conn2 = graph->addConnection(smelterOut, constructorIn);

    ASSERT_EQ(graph->getNodes().size(), 3);
    ASSERT_EQ(graph->getConnections().size(), 2);

    // Remove the middle node (Smelter) which has both input and output ports & connections
    auto cmd = std::make_unique<RemoveNodeCommand>(smelterId);
    manager->executeCommand(std::move(cmd), *graph);

    EXPECT_EQ(manager->getUndoDescription(), "Remove Node: Smelter");
    EXPECT_EQ(graph->getNodes().size(), 2);
    EXPECT_EQ(graph->getNode(smelterId), nullptr);
    EXPECT_TRUE(graph->getConnections().empty()); // Both connections touching smelter removed

    // Undo: Smelter, its ports, and both connections are restored
    manager->undo(*graph);
    EXPECT_EQ(graph->getNodes().size(), 3);
    EXPECT_NE(graph->getNode(smelterId), nullptr);
    ASSERT_EQ(graph->getConnections().size(), 2);
    EXPECT_EQ(graph->getConnections()[0].from_port, minerOut);
    EXPECT_EQ(graph->getConnections()[0].to_port, smelterIn);
    EXPECT_EQ(graph->getConnections()[1].from_port, smelterOut);
    EXPECT_EQ(graph->getConnections()[1].to_port, constructorIn);

    // Redo: Smelter and connections removed again
    manager->redo(*graph);
    EXPECT_EQ(graph->getNodes().size(), 2);
    EXPECT_EQ(graph->getNode(smelterId), nullptr);
    EXPECT_TRUE(graph->getConnections().empty());
}

TEST_F(ConcreteCommandsTest, PasteCommand_InternalConnections_UndoRedo) {
    CopyBuffer buffer;
    Node n1;
    n1.id = 100;
    n1.name = "Miner Copy";
    n1.selected_recipe_key = "mine_ore";
    n1.output_ports = {1001};
    buffer.nodes[100] = n1;
    buffer.nodePositions[100] = ImVec2(50.0f, 60.0f);

    Port p1;
    p1.id = 1001;
    p1.node_id = 100;
    p1.resource_key = "iron_ore";
    p1.user_constraint = 42.0;
    buffer.ports[1001] = p1;

    Node n2;
    n2.id = 200;
    n2.name = "Smelter Copy";
    n2.selected_recipe_key = "smelt_ingot";
    n2.input_ports = {2001};
    n2.output_ports = {2002};
    buffer.nodes[200] = n2;
    buffer.nodePositions[200] = ImVec2(250.0f, 60.0f);

    Port p2;
    p2.id = 2001;
    p2.node_id = 200;
    p2.resource_key = "iron_ore";
    buffer.ports[2001] = p2;

    Port p3;
    p3.id = 2002;
    p3.node_id = 200;
    p3.resource_key = "iron_ingot";
    buffer.ports[2002] = p3;

    Connection conn;
    conn.id = 3001;
    conn.from_port = 1001;
    conn.to_port = 2001;
    conn.resource_key = "iron_ore";
    buffer.connections.insert(conn);

    auto pasteCmd = std::make_unique<PasteCommand>(buffer, false);
    EXPECT_EQ(pasteCmd->getDescription(), "Pasted 2 nodes");
    EXPECT_TRUE(pasteCmd->getFlags().needsSolve);
    EXPECT_TRUE(pasteCmd->getFlags().needsRebuild);

    manager->executeCommand(std::move(pasteCmd), *graph);

    ASSERT_EQ(graph->getNodes().size(), 2);
    ASSERT_EQ(graph->getConnections().size(), 1);

    // Verify user constraint was preserved on pasted miner's port
    bool constraintPreserved = false;
    for (const auto& port : graph->getPorts()) {
        if (port.resource_key == "iron_ore" && !graph->isInputPort(port.id)) {
            if (port.user_constraint == 42.0) {
                constraintPreserved = true;
            }
        }
    }
    EXPECT_TRUE(constraintPreserved);

    // Undo: all pasted nodes and connections removed
    manager->undo(*graph);
    EXPECT_TRUE(graph->getNodes().empty());
    EXPECT_TRUE(graph->getPorts().empty());
    EXPECT_TRUE(graph->getConnections().empty());

    // Redo: all pasted nodes and connections restored
    manager->redo(*graph);
    ASSERT_EQ(graph->getNodes().size(), 2);
    ASSERT_EQ(graph->getConnections().size(), 1);
}

TEST_F(ConcreteCommandsTest, PasteCommand_EmptyBuffer) {
    CopyBuffer emptyBuffer;
    EXPECT_TRUE(emptyBuffer.isEmpty());

    auto pasteCmd = std::make_unique<PasteCommand>(emptyBuffer, false);
    manager->executeCommand(std::move(pasteCmd), *graph);
    EXPECT_TRUE(graph->getNodes().empty());

    manager->undo(*graph);
    EXPECT_TRUE(graph->getNodes().empty());

    manager->redo(*graph);
    EXPECT_TRUE(graph->getNodes().empty());
}

TEST_F(ConcreteCommandsTest, AddAndRemoveConnectionCommand_UndoRedo) {
    uint64_t n1 = graph->addNode("Miner", "mine_ore");
    uint64_t n2 = graph->addNode("Smelter", "smelt_ingot");
    uint64_t pOut = graph->getNode(n1)->output_ports[0];
    uint64_t pIn = graph->getNode(n2)->input_ports[0];

    // AddConnectionCommand
    auto addConnCmd = std::make_unique<AddConnectionCommand>(pOut, pIn);
    EXPECT_EQ(addConnCmd->getDescription(), "Add Connection: " + std::to_string(pOut) + " -> " + std::to_string(pIn));
    EXPECT_TRUE(addConnCmd->getFlags().needsSolve);
    EXPECT_FALSE(addConnCmd->getFlags().needsRebuild);

    manager->executeCommand(std::move(addConnCmd), *graph);
    ASSERT_EQ(graph->getConnections().size(), 1);
    EXPECT_EQ(graph->getConnections()[0].from_port, pOut);
    EXPECT_EQ(graph->getConnections()[0].to_port, pIn);

    // Undo AddConnection
    manager->undo(*graph);
    EXPECT_TRUE(graph->getConnections().empty());

    // Redo AddConnection
    manager->redo(*graph);
    ASSERT_EQ(graph->getConnections().size(), 1);

    // RemoveConnectionCommand
    auto remConnCmd = std::make_unique<RemoveConnectionCommand>(pOut, pIn);
    EXPECT_EQ(remConnCmd->getDescription(), "Remove Connection: " + std::to_string(pOut) + " -> " + std::to_string(pIn));
    EXPECT_TRUE(remConnCmd->getFlags().needsSolve);
    EXPECT_FALSE(remConnCmd->getFlags().needsRebuild);

    manager->executeCommand(std::move(remConnCmd), *graph);
    EXPECT_TRUE(graph->getConnections().empty());

    // Undo RemoveConnection
    manager->undo(*graph);
    ASSERT_EQ(graph->getConnections().size(), 1);

    // Redo RemoveConnection
    manager->redo(*graph);
    EXPECT_TRUE(graph->getConnections().empty());
}

TEST_F(ConcreteCommandsTest, RemoveNodeCommand_InvalidNodeId_UndoDoesNotCorruptGraph) {
    // Attempting to remove a nonexistent node ID should not mutate the graph,
    // and subsequent undo must be a safe no-op that never creates a dummy node ID 0.
    EXPECT_TRUE(graph->getNodes().empty());

    auto cmd = std::make_unique<RemoveNodeCommand>(99999);
    manager->executeCommand(std::move(cmd), *graph);

    EXPECT_TRUE(graph->getNodes().empty());
    EXPECT_TRUE(graph->getPorts().empty());

    // Undo should be a safe no-op
    manager->undo(*graph);
    EXPECT_TRUE(graph->getNodes().empty());
    EXPECT_TRUE(graph->getPorts().empty());

    // Redo should also be a safe no-op
    manager->redo(*graph);
    EXPECT_TRUE(graph->getNodes().empty());
    EXPECT_TRUE(graph->getPorts().empty());
}

TEST_F(ConcreteCommandsTest, RemoveNodeCommand_MultipleUndoRedoCycles) {
    uint64_t minerId = graph->addNode("Miner", "mine_ore");
    uint64_t smelterId = graph->addNode("Smelter", "smelt_ingot");
    uint64_t minerOut = graph->getNode(minerId)->output_ports[0];
    uint64_t smelterIn = graph->getNode(smelterId)->input_ports[0];
    uint64_t connId = graph->addConnection(minerOut, smelterIn);
    ASSERT_NE(connId, (uint64_t)-1);

    auto cmd = std::make_unique<RemoveNodeCommand>(minerId);
    manager->executeCommand(std::move(cmd), *graph);

    EXPECT_EQ(graph->getNodes().size(), 1);
    EXPECT_TRUE(graph->getConnections().empty());

    // Verify multiple cycles of undo -> redo
    for (int cycle = 0; cycle < 3; ++cycle) {
        manager->undo(*graph);
        EXPECT_EQ(graph->getNodes().size(), 2);
        EXPECT_EQ(graph->getConnections().size(), 1);
        EXPECT_NE(graph->getNode(minerId), nullptr);
        EXPECT_EQ(graph->getConnections()[0].from_port, minerOut);
        EXPECT_EQ(graph->getConnections()[0].to_port, smelterIn);

        manager->redo(*graph);
        EXPECT_EQ(graph->getNodes().size(), 1);
        EXPECT_EQ(graph->getNode(minerId), nullptr);
        EXPECT_TRUE(graph->getConnections().empty());
    }

    // Final undo to confirm state restoration
    manager->undo(*graph);
    EXPECT_EQ(graph->getNodes().size(), 2);
    EXPECT_EQ(graph->getConnections().size(), 1);
}

TEST_F(ConcreteCommandsTest, PasteCommand_ExternalConnections_UndoRedo) {
    // 1. Create upstream Miner in the graph
    uint64_t minerId = graph->addNode("Miner", "mine_ore");
    uint64_t minerOutPort = graph->getNode(minerId)->output_ports[0];

    // 2. Prepare CopyBuffer with Smelter, whose input port is wired to the upstream miner's port
    CopyBuffer buffer;
    Node smelterCopy;
    smelterCopy.id = 200;
    smelterCopy.name = "Smelter";
    smelterCopy.selected_recipe_key = "smelt_ingot";
    smelterCopy.input_ports = {2001};
    smelterCopy.output_ports = {2002};
    buffer.nodes[200] = smelterCopy;
    buffer.nodePositions[200] = ImVec2(300.0f, 100.0f);

    Port pIn;
    pIn.id = 2001;
    pIn.node_id = 200;
    pIn.resource_key = "iron_ore";
    buffer.ports[2001] = pIn;

    Port pOut;
    pOut.id = 2002;
    pOut.node_id = 200;
    pOut.resource_key = "iron_ingot";
    buffer.ports[2002] = pOut;

    // External connection from minerOutPort (not in buffer) to smelterCopy input (in buffer)
    Connection conn;
    conn.id = 5001;
    conn.from_port = minerOutPort;
    conn.to_port = 2001;
    conn.resource_key = "iron_ore";
    buffer.connections.insert(conn);

    // Paste with mapExternalConnections = true
    auto pasteCmd = std::make_unique<PasteCommand>(buffer, true);
    manager->executeCommand(std::move(pasteCmd), *graph);

    // Graph should now have 2 nodes (original miner + pasted smelter) and 1 connection
    ASSERT_EQ(graph->getNodes().size(), 2);
    ASSERT_EQ(graph->getConnections().size(), 1);
    EXPECT_EQ(graph->getConnections()[0].from_port, minerOutPort);

    // Undo: pasted smelter and the external connection are removed; miner remains untouched
    manager->undo(*graph);
    EXPECT_EQ(graph->getNodes().size(), 1);
    EXPECT_NE(graph->getNode(minerId), nullptr);
    EXPECT_TRUE(graph->getConnections().empty());

    // Redo: pasted smelter and external connection restored
    manager->redo(*graph);
    ASSERT_EQ(graph->getNodes().size(), 2);
    ASSERT_EQ(graph->getConnections().size(), 1);
    EXPECT_EQ(graph->getConnections()[0].from_port, minerOutPort);

    // Undo again to confirm multi-cycle stability
    manager->undo(*graph);
    EXPECT_EQ(graph->getNodes().size(), 1);
    EXPECT_TRUE(graph->getConnections().empty());
}

TEST_F(ConcreteCommandsTest, PasteCommand_MultipleUndoRedoCycles) {
    CopyBuffer buffer;
    Node n;
    n.id = 50;
    n.name = "Miner Node";
    n.selected_recipe_key = "mine_ore";
    n.output_ports = {501};
    buffer.nodes[50] = n;
    buffer.nodePositions[50] = ImVec2(10.0f, 20.0f);

    Port p;
    p.id = 501;
    p.node_id = 50;
    p.resource_key = "iron_ore";
    buffer.ports[501] = p;

    auto pasteCmd = std::make_unique<PasteCommand>(buffer, false);
    manager->executeCommand(std::move(pasteCmd), *graph);
    ASSERT_EQ(graph->getNodes().size(), 1);

    for (int i = 0; i < 3; ++i) {
        manager->undo(*graph);
        EXPECT_TRUE(graph->getNodes().empty());
        EXPECT_TRUE(graph->getPorts().empty());

        manager->redo(*graph);
        EXPECT_EQ(graph->getNodes().size(), 1);
        EXPECT_EQ(graph->getPorts().size(), 1);
    }
}

TEST_F(ConcreteCommandsTest, AddNodeCommand_MultipleUndoRedoCycles) {
    auto cmd = std::make_unique<AddNodeCommand>("Mine Iron Ore", "mine_ore", (uint64_t)-1, ImVec2(100.0f, 200.0f));
    manager->executeCommand(std::move(cmd), *graph);
    ASSERT_EQ(graph->getNodes().size(), 1);
    uint64_t nodeId = graph->getNodes()[0].id;

    for (int i = 0; i < 3; ++i) {
        manager->undo(*graph);
        EXPECT_TRUE(graph->getNodes().empty());

        manager->redo(*graph);
        ASSERT_EQ(graph->getNodes().size(), 1);
        EXPECT_EQ(graph->getNodes()[0].id, nodeId);
    }
}

TEST_F(ConcreteCommandsTest, AddAndRemoveConnectionCommand_MultipleUndoRedoCycles) {
    uint64_t n1 = graph->addNode("Miner", "mine_ore");
    uint64_t n2 = graph->addNode("Smelter", "smelt_ingot");
    uint64_t pOut = graph->getNode(n1)->output_ports[0];
    uint64_t pIn = graph->getNode(n2)->input_ports[0];

    auto addCmd = std::make_unique<AddConnectionCommand>(pOut, pIn);
    manager->executeCommand(std::move(addCmd), *graph);
    ASSERT_EQ(graph->getConnections().size(), 1);

    for (int i = 0; i < 3; ++i) {
        manager->undo(*graph);
        EXPECT_TRUE(graph->getConnections().empty());

        manager->redo(*graph);
        ASSERT_EQ(graph->getConnections().size(), 1);
    }
}

TEST_F(ConcreteCommandsTest, PasteCommand_PreservesMachineClockMultiplier_UndoRedo) {
    CopyBuffer buffer;
    Node n;
    n.id = 500;
    n.name = "Plates Node";
    n.selected_recipe_key = "make_plates";
    n.machine_key = "constructor";
    n.params["clock"] = gamedata::ParamValue{175.0};
    n.params["output_mult"] = gamedata::ParamValue{250.0};
    buffer.nodes[500] = n;
    buffer.nodePositions[500] = ImVec2(100.0f, 150.0f);

    auto pasteCmd = std::make_unique<PasteCommand>(buffer, false);
    manager->executeCommand(std::move(pasteCmd), *graph);

    ASSERT_EQ(graph->getNodes().size(), 1);
    const Node& pasted = graph->getNodes()[0];
    EXPECT_EQ(pasted.machine_key, "constructor");
    ASSERT_TRUE(pasted.params.count("clock"));
    ASSERT_TRUE(pasted.params.count("output_mult"));
    EXPECT_DOUBLE_EQ(pasted.params.at("clock").asNumber(), 175.0);
    EXPECT_DOUBLE_EQ(pasted.params.at("output_mult").asNumber(), 250.0);

    // Undo
    manager->undo(*graph);
    EXPECT_TRUE(graph->getNodes().empty());

    // Redo
    manager->redo(*graph);
    ASSERT_EQ(graph->getNodes().size(), 1);
    const Node& redone = graph->getNodes()[0];
    EXPECT_EQ(redone.machine_key, "constructor");
    ASSERT_TRUE(redone.params.count("clock"));
    ASSERT_TRUE(redone.params.count("output_mult"));
    EXPECT_DOUBLE_EQ(redone.params.at("clock").asNumber(), 175.0);
    EXPECT_DOUBLE_EQ(redone.params.at("output_mult").asNumber(), 250.0);
}

TEST_F(ConcreteCommandsTest, PasteCommand_NodeWithMorePortsThanClipboard_RestoresAllPortsOnRedo) {
    CopyBuffer buffer;
    Node n;
    n.id = 700;
    n.name = "Plates Node";
    n.selected_recipe_key = "make_plates"; // has 1 input port and 1 output port
    // Clipboard node has 0 input and 0 output ports (fewer than the recipe has)
    buffer.nodes[700] = n;
    buffer.nodePositions[700] = ImVec2(100.0f, 150.0f);

    auto pasteCmd = std::make_unique<PasteCommand>(buffer, false);
    manager->executeCommand(std::move(pasteCmd), *graph);

    ASSERT_EQ(graph->getNodes().size(), 1);
    const Node& pasted = graph->getNodes()[0];
    ASSERT_EQ(pasted.input_ports.size(), 1);
    ASSERT_EQ(pasted.output_ports.size(), 1);
    uint64_t inPortId = pasted.input_ports[0];
    uint64_t outPortId = pasted.output_ports[0];
    EXPECT_NE(graph->getPort(inPortId), nullptr);
    EXPECT_NE(graph->getPort(outPortId), nullptr);

    // Undo removes node and ports
    manager->undo(*graph);
    EXPECT_TRUE(graph->getNodes().empty());
    EXPECT_TRUE(graph->getPorts().empty());

    // Redo must restore all ports in graph->m_ports
    manager->redo(*graph);
    ASSERT_EQ(graph->getNodes().size(), 1);
    EXPECT_EQ(graph->getPorts().size(), 2);
    EXPECT_NE(graph->getPort(inPortId), nullptr);
    EXPECT_NE(graph->getPort(outPortId), nullptr);
}

TEST_F(ConcreteCommandsTest, PasteCommand_MissingNodePosition_DoesNotThrow) {
    CopyBuffer buffer;
    Node n;
    n.id = 600;
    n.name = "Plates Node";
    n.selected_recipe_key = "make_plates";
    buffer.nodes[600] = n;
    // Notice: buffer.nodePositions intentionally omits ID 600

    auto pasteCmd = std::make_unique<PasteCommand>(buffer, false);
    EXPECT_NO_THROW(manager->executeCommand(std::move(pasteCmd), *graph));
    ASSERT_EQ(graph->getNodes().size(), 1);
}

// ============================================================================
// Headless Execution (No ax::NodeEditor Context) Tests - Bug Audit Run 1 Finding 1
// ============================================================================

class HeadlessUndoRedoTest : public ::testing::Test {
protected:
    GameData testGameData;
    std::unique_ptr<FactoryGraph> graph;
    std::unique_ptr<UndoRedoManager> manager;

    void SetUp() override {
        // Ensure no active ax::NodeEditor context exists
        ax::NodeEditor::SetCurrentEditor(nullptr);

        testGameData.game.id = "test_headless";
        testGameData.game.name = "TestGame";
        testGameData.units.recipe_time = gamedata::RecipeTimeUnit::Seconds;
        gamedata::ensureV3CompatProfile(testGameData);

        gamedata::upsertResource(testGameData, "iron_ore", "Iron Ore");
        gamedata::upsertResource(testGameData, "iron_ingot", "Iron Ingot");
        gamedata::upsertResource(testGameData, "iron_plate", "Iron Plate");

        gamedata::upsertMachine(testGameData, "miner", "Miner", 1.0, "v3_compat");
        gamedata::upsertMachine(testGameData, "smelter", "Smelter", 1.0, "v3_compat");
        gamedata::upsertMachine(testGameData, "constructor", "Constructor", 1.0, "v3_compat");

        gamedata::upsertRecipe(testGameData, "mine_ore", "Mine Iron Ore", 1.0, {"miner"},
                               {}, {{"iron_ore", 30.0}});
        gamedata::upsertRecipe(testGameData, "smelt_ingot", "Smelt Iron Ingot", 1.0, {"smelter"},
                               {{"iron_ore", 30.0}}, {{"iron_ingot", 30.0}});

        graph = std::make_unique<FactoryGraph>(testGameData);
        manager = std::make_unique<UndoRedoManager>(10);
    }

    void TearDown() override {
        manager.reset();
        graph.reset();
        ax::NodeEditor::SetCurrentEditor(nullptr);
    }
};

TEST_F(HeadlessUndoRedoTest, AddNodeCommand_WithoutEditorContext_DoesNotCrash) {
    auto cmd = std::make_unique<AddNodeCommand>("Mine Iron Ore", "mine_ore", (uint64_t)-1, ImVec2(100.0f, 200.0f));
    manager->executeCommand(std::move(cmd), *graph);
    EXPECT_EQ(graph->getNodes().size(), 1);

    manager->undo(*graph);
    EXPECT_EQ(graph->getNodes().size(), 0);

    manager->redo(*graph);
    EXPECT_EQ(graph->getNodes().size(), 1);
}

TEST_F(HeadlessUndoRedoTest, RemoveNodeCommand_WithoutEditorContext_DoesNotCrash) {
    uint64_t nodeId = graph->addNode("Mine Iron Ore", "mine_ore");
    auto cmd = std::make_unique<RemoveNodeCommand>(nodeId);
    manager->executeCommand(std::move(cmd), *graph);
    EXPECT_EQ(graph->getNodes().size(), 0);

    manager->undo(*graph);
    EXPECT_EQ(graph->getNodes().size(), 1);
}

TEST_F(HeadlessUndoRedoTest, MoveNodeCommand_WithoutEditorContext_DoesNotCrash) {
    uint64_t nodeId = graph->addNode("Mine Iron Ore", "mine_ore");
    auto cmd = std::make_unique<MoveNodeCommand>(nodeId, ImVec2(0.0f, 0.0f), ImVec2(100.0f, 200.0f));
    manager->executeCommand(std::move(cmd), *graph);
    manager->undo(*graph);
    manager->redo(*graph);
}

TEST_F(HeadlessUndoRedoTest, PasteCommand_WithoutEditorContext_DoesNotCrash) {
    CopyBuffer buffer;
    Node n;
    n.id = 100;
    n.name = "Mine Iron Ore";
    n.selected_recipe_key = "mine_ore";
    buffer.nodes[100] = n;
    buffer.nodePositions[100] = ImVec2(50.0f, 50.0f);

    auto cmd = std::make_unique<PasteCommand>(buffer, false);
    manager->executeCommand(std::move(cmd), *graph);
    EXPECT_EQ(graph->getNodes().size(), 1);

    manager->undo(*graph);
    EXPECT_EQ(graph->getNodes().size(), 0);

    manager->redo(*graph);
    EXPECT_EQ(graph->getNodes().size(), 1);
}

TEST_F(ConcreteCommandsTest, SetPortConstraintModeCommand_UndoRedo) {
    uint64_t n1 = graph->addNode("Miner", "mine_ore");
    uint64_t portId = graph->getNode(n1)->output_ports[0];
    ASSERT_EQ(graph->getPort(portId)->constraint_mode, ConstraintMode::AUTO);

    auto cmd = std::make_unique<SetPortConstraintModeCommand>(portId, ConstraintMode::AUTO, ConstraintMode::PUSH);
    manager->executeCommand(std::move(cmd), *graph);
    EXPECT_EQ(graph->getPort(portId)->constraint_mode, ConstraintMode::PUSH);

    manager->undo(*graph);
    EXPECT_EQ(graph->getPort(portId)->constraint_mode, ConstraintMode::AUTO);

    manager->redo(*graph);
    EXPECT_EQ(graph->getPort(portId)->constraint_mode, ConstraintMode::PUSH);
}

TEST_F(ConcreteCommandsTest, PasteCommand_PreservesPortConstraintAndMode_UndoRedo) {
    uint64_t n1 = graph->addNode("Smelter", "smelt_ingot");
    Node* origNode = graph->getNode(n1);
    ASSERT_NE(origNode, nullptr);
    uint64_t inPortId = origNode->input_ports[0];
    uint64_t outPortId = origNode->output_ports[0];
    graph->setPortConstraint(inPortId, 45.0, ConstraintMode::PUSH);
    graph->setPortConstraint(outPortId, 30.0, ConstraintMode::LIMIT);

    CopyBuffer buffer;
    buffer.nodes[n1] = *origNode;
    buffer.nodePositions[n1] = ImVec2(10.0f, 20.0f);
    buffer.ports[inPortId] = *graph->getPort(inPortId);
    buffer.ports[outPortId] = *graph->getPort(outPortId);

    auto pasteCmd = std::make_unique<PasteCommand>(buffer, false);
    manager->executeCommand(std::move(pasteCmd), *graph);
    ASSERT_EQ(graph->getNodes().size(), 2);

    const Node& pastedNode = graph->getNodes()[1];
    const Port* pastedIn = graph->getPort(pastedNode.input_ports[0]);
    const Port* pastedOut = graph->getPort(pastedNode.output_ports[0]);
    ASSERT_NE(pastedIn, nullptr);
    ASSERT_NE(pastedOut, nullptr);
    EXPECT_DOUBLE_EQ(pastedIn->user_constraint, 45.0);
    EXPECT_EQ(pastedIn->constraint_mode, ConstraintMode::PUSH);
    EXPECT_DOUBLE_EQ(pastedOut->user_constraint, 30.0);
    EXPECT_EQ(pastedOut->constraint_mode, ConstraintMode::LIMIT);

    manager->undo(*graph);
    ASSERT_EQ(graph->getNodes().size(), 1);

    manager->redo(*graph);
    ASSERT_EQ(graph->getNodes().size(), 2);
    const Node& redoneNode = graph->getNodes()[1];
    EXPECT_EQ(graph->getPort(redoneNode.input_ports[0])->constraint_mode, ConstraintMode::PUSH);
    EXPECT_EQ(graph->getPort(redoneNode.output_ports[0])->constraint_mode, ConstraintMode::LIMIT);
}

// ============================================================================
// GameData v4 Undo/Redo Command Tests (Params, Machine Extra Ports, Recipe, Globals, RateUnit)
// ============================================================================

TEST(UndoRedoV4CommandsTest, ChangeNodeParam_ChangeMachine_ChangeRecipe_SetGlobal_SetRateUnit) {
    const std::string pkg_json = R"json({
        "schema_version": 4,
        "game": { "id": "test_undo_v4", "name": "Test Undo V4", "data_version": "1.0" },
        "units": { "recipe_time": "s", "default_rate": "s" },
        "resources": [
            { "id": "iron_ore", "name": "Iron Ore" },
            { "id": "iron_ingot", "name": "Iron Ingot" },
            { "id": "coal", "name": "Coal" }
        ],
        "globals": {
            "belt_tier": { "label": "Belt Tier", "type": "int", "default": 1, "min": 1, "max": 5 }
        },
        "parameters": {
            "clock": { "label": "Clock", "type": "number", "default": 100.0, "min": 1.0, "max": 250.0 },
            "enable_out": { "label": "Enable Out", "type": "bool", "default": true }
        },
        "profiles": {
            "burner_prof": {
                "params": ["clock", "enable_out"],
                "craft_time": "recipe.time / (param.clock / 100.0)",
                "input_amount": "port.amount",
                "output_amount": "port.amount",
                "extra_ports": [
                    { "id": "fuel", "dir": "in", "resource": "coal", "amount": "1.0" }
                ]
            },
            "elec_prof": {
                "params": ["clock", "enable_out"],
                "craft_time": "recipe.time / (param.clock / 100.0)",
                "input_amount": "port.amount",
                "output_amount": "port.amount"
            }
        },
        "machines": [
            { "id": "burner_smelter", "name": "Burner Smelter", "profile": "burner_prof" },
            { "id": "elec_smelter", "name": "Elec Smelter", "profile": "elec_prof" }
        ],
        "recipes": [
            {
                "id": "mine_coal", "name": "Mine Coal", "time": 1.0, "machines": ["elec_smelter"],
                "in": [], "out": [{ "res": "coal", "amount": 10.0 }]
            },
            {
                "id": "smelt", "name": "Smelt", "time": 1.0, "machines": ["burner_smelter", "elec_smelter"],
                "in": [{ "res": "iron_ore", "amount": 1.0 }],
                "out": [{ "res": "iron_ingot", "amount": 1.0, "enabled": "param.enable_out" }]
            },
            {
                "id": "alt_smelt", "name": "Alt Smelt", "time": 1.0, "machines": ["elec_smelter"],
                "in": [{ "res": "iron_ore", "amount": 2.0 }],
                "out": [{ "res": "iron_ingot", "amount": 3.0 }]
            }
        ]
    })json";

    gamedata::LoadReport report;
    gamedata::GamePackage pkg;
    ASSERT_TRUE(gamedata::loadGamePackageFromMemory(pkg_json, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors[0].message);

    FactoryGraph g(pkg);
    UndoRedoManager mgr(20);

    uint64_t n_coal = g.addNode("Coal", "mine_coal");
    uint64_t n_smelt = g.addNode("Smelt", "smelt");
    uint64_t n_consumer = g.addNode("Consumer", "smelt");

    Port* p_coal_out = g.getPortBySlot(n_coal, "out:0");
    Port* p_smelt_fuel = g.getPortBySlot(n_smelt, "xin:fuel");
    Port* p_smelt_out = g.getPortBySlot(n_smelt, "out:0");
    ASSERT_NE(p_coal_out, nullptr);
    ASSERT_NE(p_smelt_fuel, nullptr);
    ASSERT_NE(p_smelt_out, nullptr);

    uint64_t fuel_port_id = p_smelt_fuel->id;
    g.setPortConstraint(fuel_port_id, 25.0, ConstraintMode::LIMIT);
    uint64_t c_fuel = g.addConnection(p_coal_out->id, fuel_port_id);
    ASSERT_NE(c_fuel, static_cast<uint64_t>(-1));

    // 1. ChangeNodeParamCommand: toggle enable_out -> false, then undo/redo
    auto paramCmd = std::make_unique<ChangeNodeParamCommand>(
        n_smelt, "enable_out", gamedata::ParamValue{true}, gamedata::ParamValue{false});
    mgr.executeCommand(std::move(paramCmd), g);
    EXPECT_FALSE(p_smelt_out->active);

    mgr.undo(g);
    EXPECT_TRUE(p_smelt_out->active);

    mgr.redo(g);
    EXPECT_FALSE(p_smelt_out->active);

    mgr.undo(g);
    EXPECT_TRUE(p_smelt_out->active);

    // 2. ChangeMachineCommand: switch burner_smelter -> elec_smelter (removes xin:fuel and c_fuel),
    //    then undo (restores xin:fuel with same port ID, constraint 25.0, and c_fuel wire!)
    auto macCmd = std::make_unique<ChangeMachineCommand>(n_smelt, "burner_smelter", "elec_smelter");
    mgr.executeCommand(std::move(macCmd), g);
    EXPECT_EQ(g.getNode(n_smelt)->machine_key, "elec_smelter");
    EXPECT_EQ(g.getPort(fuel_port_id), nullptr);
    EXPECT_EQ(g.getConnection(c_fuel), nullptr);

    mgr.undo(g);
    EXPECT_EQ(g.getNode(n_smelt)->machine_key, "burner_smelter");
    ASSERT_NE(g.getPort(fuel_port_id), nullptr);
    EXPECT_DOUBLE_EQ(g.getPort(fuel_port_id)->user_constraint, 25.0);
    EXPECT_EQ(g.getPort(fuel_port_id)->constraint_mode, ConstraintMode::LIMIT);
    ASSERT_NE(g.getConnection(c_fuel), nullptr);
    EXPECT_EQ(g.getConnection(c_fuel)->to_port, fuel_port_id);

    mgr.redo(g);
    EXPECT_EQ(g.getNode(n_smelt)->machine_key, "elec_smelter");
    EXPECT_EQ(g.getPort(fuel_port_id), nullptr);
    EXPECT_EQ(g.getConnection(c_fuel), nullptr);

    mgr.undo(g);
    ASSERT_NE(g.getPort(fuel_port_id), nullptr);
    ASSERT_NE(g.getConnection(c_fuel), nullptr);

    // 3. ChangeRecipeCommand: switch smelt -> alt_smelt, then undo/redo
    auto recCmd = std::make_unique<ChangeRecipeCommand>(n_smelt, "smelt", "alt_smelt");
    mgr.executeCommand(std::move(recCmd), g);
    EXPECT_EQ(g.getNode(n_smelt)->selected_recipe_key, "alt_smelt");
    EXPECT_EQ(g.getConnection(c_fuel), nullptr);

    mgr.undo(g);
    EXPECT_EQ(g.getNode(n_smelt)->selected_recipe_key, "smelt");
    ASSERT_NE(g.getPort(fuel_port_id), nullptr);
    ASSERT_NE(g.getConnection(c_fuel), nullptr);

    // 4. SetGlobalParamCommand: belt_tier 1 -> 4, then undo/redo
    auto globCmd = std::make_unique<SetGlobalParamCommand>(
        "belt_tier", gamedata::ParamValue{int64_t(1)}, gamedata::ParamValue{int64_t(4)});
    mgr.executeCommand(std::move(globCmd), g);
    ASSERT_TRUE(g.getGlobalParam("belt_tier").has_value());
    EXPECT_DOUBLE_EQ(g.getGlobalParam("belt_tier")->asNumber(), 4.0);

    mgr.undo(g);
    ASSERT_TRUE(g.getGlobalParam("belt_tier").has_value());
    EXPECT_DOUBLE_EQ(g.getGlobalParam("belt_tier")->asNumber(), 1.0);

    mgr.redo(g);
    ASSERT_TRUE(g.getGlobalParam("belt_tier").has_value());
    EXPECT_DOUBLE_EQ(g.getGlobalParam("belt_tier")->asNumber(), 4.0);

    // 5. SetRateUnitCommand: PerSecond -> PerMinute, then undo/redo
    auto unitCmd = std::make_unique<SetRateUnitCommand>(
        gamedata::RateUnit::PerSecond, gamedata::RateUnit::PerMinute);
    mgr.executeCommand(std::move(unitCmd), g);
    EXPECT_EQ(g.getRateUnit(), gamedata::RateUnit::PerMinute);

    mgr.undo(g);
    EXPECT_EQ(g.getRateUnit(), gamedata::RateUnit::PerSecond);

    mgr.redo(g);
    EXPECT_EQ(g.getRateUnit(), gamedata::RateUnit::PerMinute);
    (void)n_consumer;
}

TEST(UndoRedoV4Test, ChangeRecipeCommand_UndoRestoresUnresolvedNodeFallbackState) {
    const std::string pkg_json = R"json({
        "schema_version": 4,
        "game": { "id": "unresolved_undo_test", "name": "Unresolved Undo Test" },
        "units": { "recipe_time": "s", "default_rate": "s" },
        "resources": [
            { "id": "iron_ore", "name": "Iron Ore" },
            { "id": "iron_ingot", "name": "Iron Ingot" }
        ],
        "profiles": {
            "std": {}
        },
        "machines": [
            { "id": "smelter", "name": "Smelter", "profile": "std" }
        ],
        "recipes": [
            {
                "id": "valid_smelt", "name": "Valid Smelt", "time": 1.0, "machines": ["smelter"],
                "in": [{ "res": "iron_ore", "amount": 1.0 }],
                "out": [{ "res": "iron_ingot", "amount": 1.0 }]
            }
        ]
    })json";

    gamedata::LoadReport report;
    gamedata::GamePackage pkg;
    ASSERT_TRUE(gamedata::loadGamePackageFromMemory(pkg_json, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    nlohmann::json projectJson = {
        {"game", {{"id", "unresolved_undo_test"}}},
        {"rate_unit", "s"},
        {"nodes", nlohmann::json::array({
            {
                {"id", 1},
                {"name", "Removed Node"},
                {"recipe", "removed_recipe"},
                {"machine", "removed_machine"},
                {"params", {{"old_param", 7}}},
                {"ports", {{"out:0", 77}}},
                {"custom_tag", "keep_me_verbatim"}
            },
            {
                {"id", 2},
                {"name", "Valid Consumer"},
                {"recipe", "valid_smelt"},
                {"machine", "smelter"},
                {"ports", {{"in:0", 88}, {"out:0", 89}}}
            }
        })},
        {"connections", nlohmann::json::array({
            {{"id", 10}, {"from_port", 77}, {"to_port", 88}}
        })},
        {"next_node_id", 3},
        {"next_port_id", 90},
        {"next_connection_id", 11}
    };

    FactoryGraph g(pkg);
    ASSERT_TRUE(g.deserialize(projectJson, pkg));
    ASSERT_TRUE(g.getNode(1)->is_unresolved);
    ASSERT_EQ(g.getNode(1)->unresolved_json["custom_tag"], "keep_me_verbatim");
    ASSERT_EQ(g.getConnection(10)->state, WireState::INACTIVE);

    UndoRedoManager mgr;
    auto recCmd = std::make_unique<ChangeRecipeCommand>(1, "removed_recipe", "valid_smelt");
    mgr.executeCommand(std::move(recCmd), g);

    EXPECT_FALSE(g.getNode(1)->is_unresolved);
    EXPECT_EQ(g.getNode(1)->selected_recipe_key, "valid_smelt");
    EXPECT_EQ(g.getNodeEval(1)->status, gamedata::EvalStatus::OK);
    EXPECT_EQ(g.getConnection(10), nullptr);

    // Undo must restore is_unresolved, unresolved_json, unresolved port, and INACTIVE connection.
    mgr.undo(g);
    const Node* restored = g.getNode(1);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->selected_recipe_key, "removed_recipe");
    EXPECT_EQ(restored->machine_key, "removed_machine");
    EXPECT_TRUE(restored->is_unresolved);
    EXPECT_EQ(restored->unresolved_json.value("custom_tag", ""), "keep_me_verbatim");
    EXPECT_EQ(g.getNodeEval(1)->status, gamedata::EvalStatus::ERROR);
    ASSERT_NE(g.getPort(77), nullptr);
    EXPECT_TRUE(g.getPort(77)->is_unresolved);
    ASSERT_NE(g.getConnection(10), nullptr);
    EXPECT_EQ(g.getConnection(10)->state, WireState::INACTIVE);

    nlohmann::json saved = g.serialize();
    bool foundNode1 = false;
    for (const auto& nj : saved["nodes"]) {
        if (nj["id"] == 1) {
            foundNode1 = true;
            EXPECT_EQ(nj.value("recipe", ""), "removed_recipe");
            EXPECT_EQ(nj.value("custom_tag", ""), "keep_me_verbatim");
        }
    }
    EXPECT_TRUE(foundNode1);

    // Redo switches cleanly back to valid_smelt.
    mgr.redo(g);
    EXPECT_FALSE(g.getNode(1)->is_unresolved);
    EXPECT_EQ(g.getNode(1)->selected_recipe_key, "valid_smelt");
    EXPECT_EQ(g.getNodeEval(1)->status, gamedata::EvalStatus::OK);
}

TEST(UndoRedoV4Test, SetGlobalParamCommand_UndoWhenPreviouslyUnset) {
    const std::string pkg_json = R"json({
        "schema_version": 4,
        "game": { "id": "unset_global_undo_test", "name": "Unset Global Undo Test" },
        "units": { "recipe_time": "s", "default_rate": "s" },
        "resources": [
            { "id": "iron", "name": "Iron" }
        ],
        "globals": {
            "difficulty": { "type": "int", "default": 1, "min": 1, "max": 5 }
        },
        "profiles": {
            "p": {
                "craft_time": "recipe.time",
                "output_amount": "port.amount * global.difficulty"
            }
        },
        "machines": [
            { "id": "m", "name": "M", "profile": "p" }
        ],
        "recipes": [
            {
                "id": "r", "name": "R", "time": 1.0, "machines": ["m"],
                "in": [],
                "out": [{ "res": "iron", "amount": 10.0 }]
            }
        ]
    })json";

    gamedata::LoadReport report;
    gamedata::GamePackage pkg;
    ASSERT_TRUE(gamedata::loadGamePackageFromMemory(pkg_json, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    FactoryGraph g(pkg);
    uint64_t nid = g.addNode("Node", "r");
    ASSERT_NE(nid, static_cast<uint64_t>(-1));
    ASSERT_EQ(g.getGlobals().count("difficulty"), 0u);
    ASSERT_TRUE(g.getGlobalParam("difficulty").has_value());
    EXPECT_DOUBLE_EQ(g.getGlobalParam("difficulty")->asNumber(), 1.0);
    EXPECT_DOUBLE_EQ(g.getPortBySlot(nid, "out:0")->amount_per_craft, 10.0);

    UndoRedoManager mgr;
    auto cmd = std::make_unique<SetGlobalParamCommand>(
        "difficulty", std::optional<gamedata::ParamValue>{}, gamedata::ParamValue{int64_t(3)});
    mgr.executeCommand(std::move(cmd), g);

    EXPECT_EQ(g.getGlobals().count("difficulty"), 1u);
    ASSERT_TRUE(g.getGlobalParam("difficulty").has_value());
    EXPECT_DOUBLE_EQ(g.getGlobalParam("difficulty")->asNumber(), 3.0);
    EXPECT_DOUBLE_EQ(g.getPortBySlot(nid, "out:0")->amount_per_craft, 30.0);
    EXPECT_TRUE(g.serialize()["globals"].contains("difficulty"));

    // Undo must remove the previously unset global and restore schema-default evaluation.
    mgr.undo(g);
    EXPECT_EQ(g.getGlobals().count("difficulty"), 0u);
    ASSERT_TRUE(g.getGlobalParam("difficulty").has_value());
    EXPECT_DOUBLE_EQ(g.getGlobalParam("difficulty")->asNumber(), 1.0);
    EXPECT_DOUBLE_EQ(g.getPortBySlot(nid, "out:0")->amount_per_craft, 10.0);
    EXPECT_FALSE(g.serialize()["globals"].contains("difficulty"));

    // Redo re-applies the explicit global value.
    mgr.redo(g);
    EXPECT_EQ(g.getGlobals().count("difficulty"), 1u);
    EXPECT_DOUBLE_EQ(g.getGlobalParam("difficulty")->asNumber(), 3.0);
    EXPECT_DOUBLE_EQ(g.getPortBySlot(nid, "out:0")->amount_per_craft, 30.0);
}

TEST(UndoRedoV4Test, SetGlobalParamCommand_CreateClearAndUndoRedo) {
    const std::string pkg_json = R"({
        "schema_version": 4,
        "game": { "id": "clear_global_undo_test", "name": "Clear Global Undo Test" },
        "units": { "recipe_time": "s", "default_rate": "s" },
        "resources": [
            { "id": "ore", "name": "Ore" },
            { "id": "plate", "name": "Plate" }
        ],
        "globals": {
            "difficulty": { "type": "int", "default": 1, "min": 1, "max": 5 }
        },
        "profiles": {
            "p": {
                "output_amount": "port.amount * global.difficulty"
            }
        },
        "machines": [
            { "id": "m", "name": "M", "profile": "p", "categories": ["c"] }
        ],
        "recipes": [
            { "id": "r", "name": "R", "category": "c", "time": 1, "in": [["ore", 1]], "out": [["plate", 10]] }
        ]
    })";

    gamedata::LoadReport report;
    gamedata::GamePackage pkg;
    ASSERT_TRUE(gamedata::loadGamePackageFromMemory(pkg_json, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    FactoryGraph g(pkg);
    uint64_t nid = g.addNode("Node", "r");
    ASSERT_NE(nid, static_cast<uint64_t>(-1));

    UndoRedoManager mgr;
    mgr.executeCommand(
        std::make_unique<SetGlobalParamCommand>("difficulty", std::optional<gamedata::ParamValue>{}, gamedata::ParamValue{int64_t(4)}),
        g);
    ASSERT_EQ(g.getGlobals().count("difficulty"), 1u);
    EXPECT_DOUBLE_EQ(g.getPortBySlot(nid, "out:0")->amount_per_craft, 40.0);

    mgr.executeCommand(
        SetGlobalParamCommand::createClear("difficulty", g.getGlobals().at("difficulty")),
        g);
    EXPECT_EQ(g.getGlobals().count("difficulty"), 0u);
    ASSERT_TRUE(g.getGlobalParam("difficulty").has_value());
    EXPECT_DOUBLE_EQ(g.getGlobalParam("difficulty")->asNumber(), 1.0);
    EXPECT_DOUBLE_EQ(g.getPortBySlot(nid, "out:0")->amount_per_craft, 10.0);
    EXPECT_FALSE(g.serialize()["globals"].contains("difficulty"));

    mgr.undo(g);
    ASSERT_EQ(g.getGlobals().count("difficulty"), 1u);
    EXPECT_DOUBLE_EQ(g.getGlobalParam("difficulty")->asNumber(), 4.0);
    EXPECT_DOUBLE_EQ(g.getPortBySlot(nid, "out:0")->amount_per_craft, 40.0);
    EXPECT_TRUE(g.serialize()["globals"].contains("difficulty"));

    mgr.redo(g);
    EXPECT_EQ(g.getGlobals().count("difficulty"), 0u);
    EXPECT_DOUBLE_EQ(g.getGlobalParam("difficulty")->asNumber(), 1.0);
    EXPECT_DOUBLE_EQ(g.getPortBySlot(nid, "out:0")->amount_per_craft, 10.0);
    EXPECT_FALSE(g.serialize()["globals"].contains("difficulty"));
}




