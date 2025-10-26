#include <gtest/gtest.h>
#include "services/UndoRedoSystem.h"
#include "core/graph/FactoryGraph.h" // Include the required FactoryGraph
#include "core/data/GameData.h"       // Include GameData for the graph
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

