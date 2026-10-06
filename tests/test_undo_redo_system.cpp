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
        ax::NodeEditor::Config cfg;
        cfg.SettingsFile = nullptr;
        m_editorContext = ax::NodeEditor::CreateEditor(&cfg);
        ax::NodeEditor::SetCurrentEditor(m_editorContext);

        testGameData.gameName = "TestGame";
        testGameData.time_unit = "seconds";

        testGameData.resources["iron_ore"] = Resource{"Iron Ore", 1};
        testGameData.resources["iron_ingot"] = Resource{"Iron Ingot", 2};
        testGameData.resources["iron_plate"] = Resource{"Iron Plate", 3};
        testGameData.resources["screw"] = Resource{"Screw", 4};

        testGameData.machines["miner"] = Machine{"Miner", 1, 0};
        testGameData.machines["smelter"] = Machine{"Smelter", 1, 1};
        testGameData.machines["constructor"] = Machine{"Constructor", 1, 2};

        Recipe mine_ore;
        mine_ore.name = "Mine Iron Ore";
        mine_ore.produced_in_machines_keys.push_back("miner");
        mine_ore.output_ports.push_back(RecipePort{30.0, "iron_ore"});
        testGameData.recipes["mine_ore"] = mine_ore;

        Recipe smelt_ingot;
        smelt_ingot.name = "Smelt Iron Ingot";
        smelt_ingot.produced_in_machines_keys.push_back("smelter");
        smelt_ingot.input_ports.push_back(RecipePort{30.0, "iron_ore"});
        smelt_ingot.output_ports.push_back(RecipePort{30.0, "iron_ingot"});
        testGameData.recipes["smelt_ingot"] = smelt_ingot;

        Recipe make_plates;
        make_plates.name = "Make Plates";
        make_plates.produced_in_machines_keys.push_back("constructor");
        make_plates.input_ports.push_back(RecipePort{30.0, "iron_ingot"});
        make_plates.output_ports.push_back(RecipePort{20.0, "iron_plate"});
        testGameData.recipes["make_plates"] = make_plates;

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

