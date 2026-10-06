#include <gtest/gtest.h>
#include <core/data/GameData.h>
#include "core/graph/FactoryGraph.h"
#include "core/data/GameDataManager.h"
#include "core/data/GameDataScanner.h"
#include "services/ProjectIo.h"
#include "services/SettingsManager.h"
#include "services/SessionManager.h"
#include "services/RecentFiles.h"
#include "services/TextureManager.h"
#include "common/FilesystemUtils.h"
#include "common/StringUtils.h"
#include "common/TextureUtils.h"
#include "common/CopyBuffer.h"
#include "services/NotificationManager.h"
#include <fstream>
#include <filesystem>

class FactoryGraphTest : public ::testing::Test {
protected:
    GameData testGameData;
    std::unique_ptr<FactoryGraph> graph;

void SetUp() override {
        testGameData.gameName = "TestGame";
        testGameData.time_unit = "seconds";

        // --- 1. Define Resources ---
        testGameData.resources["nothing"] = Resource{"Nothing", 0};
        testGameData.resources["iron_ore"] = Resource{"Iron Ore", 1};
        testGameData.resources["iron_ingot"] = Resource{"Iron Ingot", 2};
        testGameData.resources["iron_plate"] = Resource{"Iron Plate", 3};
        testGameData.resources["screw"] = Resource{"Screw", 4};
        testGameData.resources["reinforced_plate"] = Resource{"Reinforced Plate", 5};
        testGameData.resources["copper_ore"] = Resource{"Copper Ore", 6};
        testGameData.resources["copper_ingot"] = Resource{"Copper Ingot", 7};
        testGameData.resources["wire"] = Resource{"Wire", 8};

        // --- 2. Define Machines ---
        // (Machine keys are used by setNodeRecipe)
        testGameData.machines["miner"] = Machine{"Miner", 1, 0};
        testGameData.machines["smelter"] = Machine{"Smelter", 1, 1};
        testGameData.machines["constructor"] = Machine{"Constructor", 1, 2};
        testGameData.machines["assembler"] = Machine{"Assembler", 1, 3};
        testGameData.machines["sink"] = Machine{"AWESOME Sink", 1, 4};

        // --- 3. Define Recipes ---

        // Producer (Miner)
        Recipe mine_ore;
        mine_ore.name = "Mine Iron Ore";
        mine_ore.produced_in_machines_keys.push_back("miner");
        mine_ore.input_ports.push_back(RecipePort{1.0, "nothing"});
        mine_ore.output_ports.push_back(RecipePort{30.0, "iron_ore"});
        testGameData.recipes["mine_ore"] = mine_ore;

        // Processor (Smelter)
        Recipe smelt_ingot;
        smelt_ingot.name = "Smelt Iron Ingot";
        smelt_ingot.produced_in_machines_keys.push_back("smelter");
        smelt_ingot.input_ports.push_back(RecipePort{30.0, "iron_ore"});
        smelt_ingot.output_ports.push_back(RecipePort{30.0, "iron_ingot"});
        testGameData.recipes["smelt_ingot"] = smelt_ingot;

        // Processor (Constructor)
        Recipe make_plates;
        make_plates.name = "Make Iron Plates";
        make_plates.produced_in_machines_keys.push_back("constructor");
        make_plates.input_ports.push_back(RecipePort{30.0, "iron_ingot"});
        make_plates.output_ports.push_back(RecipePort{20.0, "iron_plate"});
        testGameData.recipes["make_plates"] = make_plates;

        // Processor (Constructor)
        Recipe make_screws;
        make_screws.name = "Make Screws";
        make_screws.produced_in_machines_keys.push_back("constructor");
        make_screws.input_ports.push_back(RecipePort{10.0, "iron_ingot"});
        make_screws.output_ports.push_back(RecipePort{40.0, "screw"});
        testGameData.recipes["make_screws"] = make_screws;

        // Assembler (Multi-Input)
        Recipe make_reinforced;
        make_reinforced.name = "Make Reinforced Plates";
        make_reinforced.produced_in_machines_keys.push_back("assembler");
        make_reinforced.input_ports.push_back(RecipePort{30.0, "iron_plate"});
        make_reinforced.input_ports.push_back(RecipePort{60.0, "screw"});
        make_reinforced.output_ports.push_back(RecipePort{5.0, "reinforced_plate"});
        testGameData.recipes["make_reinforced"] = make_reinforced;

        // Consumer (Sink)
        Recipe sink_plates;
        sink_plates.name = "Sink Iron Plates";
        sink_plates.produced_in_machines_keys.push_back("sink");
        sink_plates.input_ports.push_back(RecipePort{1.0, "iron_plate"});
        sink_plates.output_ports.push_back(RecipePort{1.0, "nothing"});
        testGameData.recipes["sink_plates"] = sink_plates;

        // Processor (Constructor - Wire)
        Recipe make_wire;
        make_wire.name = "Make Wire";
        make_wire.produced_in_machines_keys.push_back("constructor");
        make_wire.input_ports.push_back(RecipePort{15.0, "copper_ingot"});
        make_wire.output_ports.push_back(RecipePort{30.0, "wire"});
        testGameData.recipes["make_wire"] = make_wire;

        // Multi-Input Multi-Output (Assembler)
        Recipe multi_io;
        multi_io.name = "Multi IO Recipe";
        multi_io.produced_in_machines_keys.push_back("assembler");
        multi_io.input_ports.push_back(RecipePort{30.0, "iron_plate"});
        multi_io.input_ports.push_back(RecipePort{60.0, "screw"});
        multi_io.input_ports.push_back(RecipePort{30.0, "wire"});
        multi_io.output_ports.push_back(RecipePort{5.0, "reinforced_plate"});
        multi_io.output_ports.push_back(RecipePort{15.0, "copper_ingot"});
        testGameData.recipes["multi_io"] = multi_io;

        // Consumer (Sink Reinforced)
        Recipe sink_reinforced;
        sink_reinforced.name = "Sink Reinforced";
        sink_reinforced.produced_in_machines_keys.push_back("sink");
        sink_reinforced.input_ports.push_back(RecipePort{1.0, "reinforced_plate"});
        sink_reinforced.output_ports.push_back(RecipePort{1.0, "nothing"});
        testGameData.recipes["sink_reinforced"] = sink_reinforced;

        // --- 4. Initialize Graph ---
        graph = std::make_unique<FactoryGraph>(testGameData);
    }

    void TearDown() override {
        graph.reset();
    }

    /**
     * @brief Helper to add a node and verify its creation.
     * @param recipeKey The key of the recipe to use.
     * @return The ID of the newly created node.
     */
    uint64_t addNode(const std::string& recipeKey) {
    std::string nodeName = testGameData.recipes.at(recipeKey).name;
    uint64_t nodeId = graph->addNode(nodeName, recipeKey);
    return nodeId;
}

    /**
     * @brief Helper to get a specific port from a node.
     * @param nodeId The ID of the node.
     * @param resourceKey The resource key of the port.
     * @param isInput True to search input ports, false to search output ports.
     * @return A pointer to the port, or nullptr if not found.
     */
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
};

TEST_F(FactoryGraphTest, InitialState) {
    // A graph created in SetUp should be empty
    EXPECT_EQ(graph->getNodes().size(), 0);
    EXPECT_EQ(graph->getPorts().size(), 0);
    EXPECT_EQ(graph->getConnections().size(), 0);
}

TEST_F(FactoryGraphTest, AddNode) {
    size_t initialPortCount = graph->getPorts().size();
    uint64_t nodeId = addNode("mine_ore");

    // Check node
    ASSERT_EQ(graph->getNodes().size(), 1);
    Node* node = graph->getNode(nodeId);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->id, nodeId);
    EXPECT_EQ(node->selected_recipe_key, "mine_ore");
    EXPECT_EQ(node->machine_key, "miner");

    // Check ports
    const Recipe& recipe = testGameData.recipes.at("mine_ore");
    size_t expectedPortCount = recipe.input_ports.size() + recipe.output_ports.size();
    EXPECT_EQ(graph->getPorts().size(), initialPortCount + expectedPortCount);
    EXPECT_EQ(node->input_ports.size(), recipe.input_ports.size());
    EXPECT_EQ(node->output_ports.size(), recipe.output_ports.size());

    // Check specific ports
    Port* inPort = graph->getPort(node->input_ports[0]);
    ASSERT_NE(inPort, nullptr);
    EXPECT_EQ(inPort->resource_key, "nothing");
    EXPECT_EQ(inPort->node_id, nodeId);

    Port* outPort = graph->getPort(node->output_ports[0]);
    ASSERT_NE(outPort, nullptr);
    EXPECT_EQ(outPort->resource_key, "iron_ore");
    EXPECT_EQ(outPort->node_id, nodeId);
}

TEST_F(FactoryGraphTest, AddMultiInputNode) {
    uint64_t nodeId = addNode("make_reinforced");

    ASSERT_EQ(graph->getNodes().size(), 1);
    Node* node = graph->getNode(nodeId);
    ASSERT_NE(node, nullptr);

    // Check ports
    EXPECT_EQ(node->input_ports.size(), 2);
    EXPECT_EQ(node->output_ports.size(), 1);

    Port* inPort1 = getPort(nodeId, "iron_plate", true);
    ASSERT_NE(inPort1, nullptr);
    EXPECT_EQ(inPort1->resource_key, "iron_plate");

    Port* inPort2 = getPort(nodeId, "screw", true);
    ASSERT_NE(inPort2, nullptr);
    EXPECT_EQ(inPort2->resource_key, "screw");
}

TEST_F(FactoryGraphTest, AddNodeFailure) {
    // This tests the branch in addNode -> setNodeRecipe where the recipe is not found
    // The current implementation adds the node but fails to add ports.
    uint64_t nodeId = graph->addNode("Bad Node", "non_existent_recipe");

    Node* node = graph->getNode(nodeId);
    ASSERT_NE(node, nullptr); // Node is still created
    EXPECT_EQ(node->input_ports.size(), 0); // But has no ports
    EXPECT_EQ(node->output_ports.size(), 0);
}

TEST_F(FactoryGraphTest, GetNodeFailure) {
    EXPECT_EQ(graph->getNode(999), nullptr);
    const FactoryGraph* constGraph = graph.get();
    EXPECT_EQ(constGraph->getNode(999), nullptr);
}

TEST_F(FactoryGraphTest, IsInputPort) {
    uint64_t nodeId = addNode("mine_ore");
    Node* node = graph->getNode(nodeId);

    ASSERT_FALSE(node->input_ports.empty());
    ASSERT_FALSE(node->output_ports.empty());

    EXPECT_TRUE(graph->isInputPort(node->input_ports[0]));
    EXPECT_FALSE(graph->isInputPort(node->output_ports[0]));
}

TEST_F(FactoryGraphTest, IsInputPortFailure) {
    // Test with a port ID that doesn't exist
    EXPECT_FALSE(graph->isInputPort(999));

    // Test with a port ID that exists but its node doesn't (shouldn't happen, but good to check)
    // This is hard to test without manual graph manipulation.
    // The current `isInputPort` logic handles `node == nullptr` by returning false.
}

TEST_F(FactoryGraphTest, PortConstraint) {
    uint64_t nodeId = addNode("mine_ore");
    Port* port = getPort(nodeId, "iron_ore", false); // Get output port
    ASSERT_NE(port, nullptr);

    EXPECT_EQ(port->user_constraint, -1.0); // Default value

    ASSERT_TRUE(graph->setPortConstraint(port->id, 120.0));
    EXPECT_EQ(port->user_constraint, 120.0);
    EXPECT_EQ(graph->getPort(port->id)->user_constraint, 120.0);
}

TEST_F(FactoryGraphTest, PortConstraintFailure) {
    // Test setting constraint on a port that doesn't exist
    EXPECT_FALSE(graph->setPortConstraint(999, 100.0));
}

TEST_F(FactoryGraphTest, ValidConnection) {
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    Port* fromPort = getPort(n1, "iron_ore", false);
    Port* toPort = getPort(n2, "iron_ore", true);

    ASSERT_NE(fromPort, nullptr);
    ASSERT_NE(toPort, nullptr);
    EXPECT_TRUE(graph->isValidConnection(fromPort->id, toPort->id));
}

TEST_F(FactoryGraphTest, InvalidConnectionBadResource) {
    uint64_t n1 = addNode("mine_ore"); // Outputs iron_ore
    uint64_t n2 = addNode("make_plates"); // Inputs iron_ingot
    Port* fromPort = getPort(n1, "iron_ore", false);
    Port* toPort = getPort(n2, "iron_ingot", true);

    EXPECT_FALSE(graph->isValidConnection(fromPort->id, toPort->id));
}

TEST_F(FactoryGraphTest, InvalidConnectionBadDirection) {
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    Port* out1 = getPort(n1, "iron_ore", false);
    Port* in1 = getPort(n1, "nothing", true);
    Port* out2 = getPort(n2, "iron_ingot", false);
    Port* in2 = getPort(n2, "iron_ore", true);

    EXPECT_FALSE(graph->isValidConnection(in2->id, out1->id)); // Input to Output
    EXPECT_FALSE(graph->isValidConnection(in1->id, in2->id));   // Input to Input
    EXPECT_FALSE(graph->isValidConnection(out1->id, out2->id)); // Output to Output
}

TEST_F(FactoryGraphTest, InvalidConnectionBadPorts) {
    uint64_t n1 = addNode("mine_ore");
    Port* out1 = getPort(n1, "iron_ore", false);

    EXPECT_FALSE(graph->isValidConnection(999, 998)); // Both invalid
    EXPECT_FALSE(graph->isValidConnection(out1->id, 999)); // toPort invalid
    EXPECT_FALSE(graph->isValidConnection(999, out1->id)); // fromPort invalid (and wrong direction)
}

TEST_F(FactoryGraphTest, AddAndGetConnection) {
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    Port* fromPort = getPort(n1, "iron_ore", false);
    Port* toPort = getPort(n2, "iron_ore", true);

    uint64_t connId = graph->addConnection(fromPort->id, toPort->id);
    ASSERT_NE(connId, -1);
    EXPECT_EQ(graph->getConnections().size(), 1);

    // Test connectionExists
    EXPECT_TRUE(graph->connectionExists(fromPort->id, toPort->id));
    EXPECT_FALSE(graph->connectionExists(toPort->id, fromPort->id)); // Check reverse

    // Test getConnection(id)
    Connection* conn = graph->getConnection(connId);
    ASSERT_NE(conn, nullptr);
    EXPECT_EQ(conn->id, connId);
    EXPECT_EQ(conn->from_port, fromPort->id);
    EXPECT_EQ(conn->to_port, toPort->id);
    EXPECT_EQ(conn->resource_key, "iron_ore");

    // Test getConnection(from, to)
    Connection* conn2 = graph->getConnection(fromPort->id, toPort->id);
    ASSERT_EQ(conn, conn2);
}

TEST_F(FactoryGraphTest, AddConnectionFailure) {
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("make_plates");
    Port* fromPort = getPort(n1, "iron_ore", false);
    Port* toPort = getPort(n2, "iron_ingot", true);

    // Invalid resource
    EXPECT_EQ(graph->addConnection(fromPort->id, toPort->id), -1);
    EXPECT_EQ(graph->getConnections().size(), 0);
}

TEST_F(FactoryGraphTest, GetConnectionFailure) {
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    Port* fromPort = getPort(n1, "iron_ore", false);
    Port* toPort = getPort(n2, "iron_ore", true);

    // By ID
    EXPECT_EQ(graph->getConnection(999), nullptr);
    const FactoryGraph* constGraph = graph.get();
    EXPECT_EQ(constGraph->getConnection(999), nullptr);

    // By ports
    EXPECT_EQ(graph->getConnection(fromPort->id, toPort->id), nullptr);
}

TEST_F(FactoryGraphTest, GetConnectionsForPort) {
    uint64_t n_screws = addNode("make_screws");
    uint64_t n_rfp1 = addNode("make_reinforced");
    uint64_t n_rfp2 = addNode("make_reinforced");

    Port* fromPort = getPort(n_screws, "screw", false);
    Port* toPort1 = getPort(n_rfp1, "screw", true);
    Port* toPort2 = getPort(n_rfp2, "screw", true);

    uint64_t connId1 = graph->addConnection(fromPort->id, toPort1->id);
    uint64_t connId2 = graph->addConnection(fromPort->id, toPort2->id);

    std::vector<Connection*> conns = graph->getConnectionsForPort(fromPort->id);
    ASSERT_EQ(conns.size(), 2);
    // Check that we got the right IDs (order not guaranteed)
    bool found1 = (conns[0]->id == connId1 || conns[1]->id == connId1);
    bool found2 = (conns[0]->id == connId2 || conns[1]->id == connId2);
    EXPECT_TRUE(found1 && found2);

    // Check incoming ports
    EXPECT_EQ(graph->getConnectionsForPort(toPort1->id).size(), 1);
    EXPECT_EQ(graph->getConnectionsForPort(toPort1->id)[0]->id, connId1);

    EXPECT_EQ(graph->getConnectionsForPort(toPort2->id).size(), 1);
    EXPECT_EQ(graph->getConnectionsForPort(toPort2->id)[0]->id, connId2);

    // Check port with no connections
    Port* otherPort = getPort(n_screws, "iron_ingot", true);
    EXPECT_EQ(graph->getConnectionsForPort(otherPort->id).size(), 0);

    // Check invalid port
    EXPECT_EQ(graph->getConnectionsForPort(999).size(), 0);
}

TEST_F(FactoryGraphTest, RemoveConnection) {
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    Port* fromPort = getPort(n1, "iron_ore", false);
    Port* toPort = getPort(n2, "iron_ore", true);
    uint64_t connId = graph->addConnection(fromPort->id, toPort->id);

    ASSERT_EQ(graph->getConnections().size(), 1);
    ASSERT_TRUE(graph->removeConnection(fromPort->id, toPort->id));

    // Check that it's gone
    EXPECT_EQ(graph->getConnections().size(), 0);
    EXPECT_FALSE(graph->connectionExists(fromPort->id, toPort->id));
    EXPECT_EQ(graph->getConnection(connId), nullptr);
    EXPECT_EQ(graph->getConnectionsForPort(fromPort->id).size(), 0);
    EXPECT_EQ(graph->getConnectionsForPort(toPort->id).size(), 0);
}

TEST_F(FactoryGraphTest, RemoveConnectionFailure) {
    // Try removing a connection that doesn't exist
    EXPECT_FALSE(graph->removeConnection(999, 998));

    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    Port* fromPort = getPort(n1, "iron_ore", false);
    Port* toPort = getPort(n2, "iron_ore", true);
    EXPECT_FALSE(graph->removeConnection(fromPort->id, toPort->id));
}

TEST_F(FactoryGraphTest, RemoveConnectionSwapAndPop) {
    // This test covers the swap-and-pop logic in removeConnection
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    uint64_t n3 = addNode("make_plates");
    uint64_t n4 = addNode("sink_plates");
    uint64_t n5 = addNode("make_screws");
    uint64_t n6 = addNode("make_reinforced");

    Port* p1 = getPort(n1, "iron_ore", false);
    Port* p2 = getPort(n2, "iron_ore", true);
    Port* p3 = getPort(n3, "iron_plate", false);
    Port* p4 = getPort(n4, "iron_plate", true);
    Port* p5 = getPort(n5, "screw", false);
    Port* p6 = getPort(n6, "screw", true);

    uint64_t connId1 = graph->addConnection(p1->id, p2->id); // Index 0
    uint64_t connId2 = graph->addConnection(p3->id, p4->id); // Index 1
    uint64_t connId3 = graph->addConnection(p5->id, p6->id); // Index 2

    ASSERT_EQ(graph->getConnections().size(), 3);
    Connection* conn3_ptr = graph->getConnection(connId3); // Get pointer to last element

    // Remove the middle connection (connId2)
    ASSERT_TRUE(graph->removeConnection(p3->id, p4->id));
    ASSERT_EQ(graph->getConnections().size(), 2);

    // Check that connId2 is gone, but others remain
    EXPECT_EQ(graph->getConnection(connId2), nullptr);
    EXPECT_NE(graph->getConnection(connId1), nullptr);
    EXPECT_NE(graph->getConnection(connId3), nullptr);

    // Check that connId3 (last) was swapped into index 1
    EXPECT_EQ(graph->getConnections()[1].id, connId3);

    // Now remove the first connection (connId1)
    ASSERT_TRUE(graph->removeConnection(p1->id, p2->id));
    ASSERT_EQ(graph->getConnections().size(), 1);
    EXPECT_EQ(graph->getConnection(connId1), nullptr);
    EXPECT_NE(graph->getConnection(connId3), nullptr);

    // Check that connId3 is now at index 0
    EXPECT_EQ(graph->getConnections()[0].id, connId3);
}

TEST_F(FactoryGraphTest, RemovePort) {
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    Port* fromPort = getPort(n1, "iron_ore", false);
    Port* toPort = getPort(n2, "iron_ore", true);
    uint64_t connId = graph->addConnection(fromPort->id, toPort->id);

    uint64_t fromPortId = fromPort->id;
    size_t initialPortCount = graph->getPorts().size();
    ASSERT_EQ(graph->getConnections().size(), 1);

    // Remove the port
    ASSERT_TRUE(graph->removePort(fromPortId));

    // Check that port is gone
    EXPECT_EQ(graph->getPort(fromPortId), nullptr);
    EXPECT_EQ(graph->getPorts().size(), initialPortCount - 1);

    // Check that the connection was also removed
    EXPECT_EQ(graph->getConnections().size(), 0);
    EXPECT_EQ(graph->getConnection(connId), nullptr);

    // Check that the port is removed from its node's list
    Node* node1 = graph->getNode(n1);
    ASSERT_NE(node1, nullptr);
    for (uint64_t pId : node1->output_ports) {
        EXPECT_NE(pId, fromPortId);
    }
}

TEST_F(FactoryGraphTest, RemovePortSwapAndPop) {
    // This test covers the swap-and-pop logic in removePort
    uint64_t n1 = addNode("mine_ore"); // 2 ports
    uint64_t n2 = addNode("smelt_ingot"); // 2 ports

    ASSERT_EQ(graph->getPorts().size(), 4);

    Port* p1 = getPort(n1, "nothing", true);    // Index 0
    Port* p2 = getPort(n1, "iron_ore", false); // Index 1
    Port* p3 = getPort(n2, "iron_ore", true);  // Index 2
    Port* p4 = getPort(n2, "iron_ingot", false); // Index 3

    uint64_t p2_id = p2->id;
    uint64_t p4_id = p4->id;

    // Remove p2 (index 1)
    ASSERT_TRUE(graph->removePort(p2_id));
    ASSERT_EQ(graph->getPorts().size(), 3);

    EXPECT_EQ(graph->getPort(p2_id), nullptr);
    EXPECT_NE(graph->getPort(p4_id), nullptr);

    // Check that p4 (last) was swapped into index 1
    EXPECT_EQ(graph->getPorts()[1].id, p4_id);
}

TEST_F(FactoryGraphTest, RemovePortFailure) {
    EXPECT_FALSE(graph->removePort(999));
}

TEST_F(FactoryGraphTest, RemoveNode) {
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    Port* fromPort = getPort(n1, "iron_ore", false);
    Port* toPort = getPort(n2, "iron_ore", true);
    uint64_t connId = graph->addConnection(fromPort->id, toPort->id);

    uint64_t fromPortId = fromPort->id;
    uint64_t inPortId = getPort(n1, "nothing", true)->id;

    ASSERT_EQ(graph->getNodes().size(), 2);
    ASSERT_EQ(graph->getPorts().size(), 4);
    ASSERT_EQ(graph->getConnections().size(), 1);

    // Remove node 1
    ASSERT_TRUE(graph->removeNode(n1));

    // Check node is gone
    EXPECT_EQ(graph->getNodes().size(), 1);
    EXPECT_EQ(graph->getNode(n1), nullptr);
    EXPECT_NE(graph->getNode(n2), nullptr);

    // Check node 1's ports are gone
    EXPECT_EQ(graph->getPort(fromPortId), nullptr);
    EXPECT_EQ(graph->getPort(inPortId), nullptr);
    EXPECT_EQ(graph->getPorts().size(), 2); // Only node 2's ports remain

    // Check connection is gone
    EXPECT_EQ(graph->getConnections().size(), 0);
    EXPECT_EQ(graph->getConnection(connId), nullptr);
}

TEST_F(FactoryGraphTest, RemoveNodeSwapAndPop) {
    // This test covers the swap-and-pop logic in removeNode
    uint64_t n1 = addNode("mine_ore");      // Index 0
    uint64_t n2 = addNode("smelt_ingot");   // Index 1
    uint64_t n3 = addNode("make_plates"); // Index 2

    ASSERT_EQ(graph->getNodes().size(), 3);
    Node* node3_ptr = graph->getNode(n3); // Get pointer to last element

    // Remove middle node (n2)
    ASSERT_TRUE(graph->removeNode(n2));
    ASSERT_EQ(graph->getNodes().size(), 2);

    EXPECT_EQ(graph->getNode(n2), nullptr);
    EXPECT_NE(graph->getNode(n1), nullptr);
    EXPECT_NE(graph->getNode(n3), nullptr);

    // Check that node n3 (last) was swapped into index 1
    EXPECT_EQ(graph->getNodes()[1].id, n3);
    EXPECT_EQ(graph->getNode(n3), &graph->getNodes()[1]);
}

TEST_F(FactoryGraphTest, RemoveNodeFailure) {
    EXPECT_FALSE(graph->removeNode(999));
}

TEST_F(FactoryGraphTest, RemoveNodeWithMultipleInputAndOutputPorts) {
    // Regression test for Bug 1.20: Container Mutation During Iteration in removeNode.
    // Ensure that removing a node with multiple inputs and multiple outputs
    // cleanly removes all of its ports and connections without leaking or dangling references.
    uint64_t multiNode = addNode("multi_io");
    Node* multiNodePtr = graph->getNode(multiNode);
    ASSERT_NE(multiNodePtr, nullptr);
    ASSERT_EQ(multiNodePtr->input_ports.size(), 3);
    ASSERT_EQ(multiNodePtr->output_ports.size(), 2);

    std::vector<uint64_t> inPortIds = multiNodePtr->input_ports;
    std::vector<uint64_t> outPortIds = multiNodePtr->output_ports;

    // Upstream producers
    uint64_t prod1 = addNode("make_plates");
    uint64_t prod2 = addNode("make_screws");
    uint64_t prod3 = addNode("make_wire");

    // Downstream consumers
    uint64_t cons1 = addNode("sink_reinforced");
    uint64_t cons2 = addNode("make_wire");

    // Connect producers to multiNode inputs
    Port* outP1 = getPort(prod1, "iron_plate", false);
    Port* outP2 = getPort(prod2, "screw", false);
    Port* outP3 = getPort(prod3, "wire", false);
    Port* inM1 = getPort(multiNode, "iron_plate", true);
    Port* inM2 = getPort(multiNode, "screw", true);
    Port* inM3 = getPort(multiNode, "wire", true);

    ASSERT_NE(outP1, nullptr);
    ASSERT_NE(outP2, nullptr);
    ASSERT_NE(outP3, nullptr);
    ASSERT_NE(inM1, nullptr);
    ASSERT_NE(inM2, nullptr);
    ASSERT_NE(inM3, nullptr);

    uint64_t conn1 = graph->addConnection(outP1->id, inM1->id);
    uint64_t conn2 = graph->addConnection(outP2->id, inM2->id);
    uint64_t conn3 = graph->addConnection(outP3->id, inM3->id);

    // Connect multiNode outputs to consumers
    Port* outM1 = getPort(multiNode, "reinforced_plate", false);
    Port* outM2 = getPort(multiNode, "copper_ingot", false);
    Port* inC1 = getPort(cons1, "reinforced_plate", true);
    Port* inC2 = getPort(cons2, "copper_ingot", true);

    ASSERT_NE(outM1, nullptr);
    ASSERT_NE(outM2, nullptr);
    ASSERT_NE(inC1, nullptr);
    ASSERT_NE(inC2, nullptr);

    uint64_t conn4 = graph->addConnection(outM1->id, inC1->id);
    uint64_t conn5 = graph->addConnection(outM2->id, inC2->id);

    EXPECT_NE(conn1, (uint64_t)-1);
    EXPECT_NE(conn2, (uint64_t)-1);
    EXPECT_NE(conn3, (uint64_t)-1);
    EXPECT_NE(conn4, (uint64_t)-1);
    EXPECT_NE(conn5, (uint64_t)-1);

    size_t totalNodesBefore = graph->getNodes().size();
    size_t totalPortsBefore = graph->getPorts().size();
    size_t totalConnsBefore = graph->getConnections().size();
    ASSERT_EQ(totalConnsBefore, 5);

    // Remove the multi-port node
    ASSERT_TRUE(graph->removeNode(multiNode));

    // Verify node is removed
    EXPECT_EQ(graph->getNode(multiNode), nullptr);
    EXPECT_EQ(graph->getNodes().size(), totalNodesBefore - 1);

    // Verify all 5 ports of the multi-port node are removed
    for (uint64_t pId : inPortIds) {
        EXPECT_EQ(graph->getPort(pId), nullptr);
    }
    for (uint64_t pId : outPortIds) {
        EXPECT_EQ(graph->getPort(pId), nullptr);
    }
    EXPECT_EQ(graph->getPorts().size(), totalPortsBefore - 5);

    // Verify all 5 connections are removed
    EXPECT_EQ(graph->getConnection(conn1), nullptr);
    EXPECT_EQ(graph->getConnection(conn2), nullptr);
    EXPECT_EQ(graph->getConnection(conn3), nullptr);
    EXPECT_EQ(graph->getConnection(conn4), nullptr);
    EXPECT_EQ(graph->getConnection(conn5), nullptr);
    EXPECT_EQ(graph->getConnections().size(), 0);

    // Verify neighbor nodes' ports have empty connection lists
    EXPECT_TRUE(graph->getConnectionsForPort(outP1->id).empty());
    EXPECT_TRUE(graph->getConnectionsForPort(outP2->id).empty());
    EXPECT_TRUE(graph->getConnectionsForPort(outP3->id).empty());
    EXPECT_TRUE(graph->getConnectionsForPort(inC1->id).empty());
    EXPECT_TRUE(graph->getConnectionsForPort(inC2->id).empty());
}

TEST_F(FactoryGraphTest, RemoveNodeWithMultiPortAndFanOutConnections) {
    // Verify removal of a multi-port node where an output port has multiple
    // downstream connections (fan-out pattern).
    uint64_t multiNode = addNode("multi_io");
    Node* multiNodePtr = graph->getNode(multiNode);
    ASSERT_NE(multiNodePtr, nullptr);

    Port* outM1 = getPort(multiNode, "reinforced_plate", false);
    ASSERT_NE(outM1, nullptr);

    // Two downstream consumers of the same output port
    uint64_t cons1 = addNode("sink_reinforced");
    uint64_t cons2 = addNode("sink_reinforced");
    Port* inC1 = getPort(cons1, "reinforced_plate", true);
    Port* inC2 = getPort(cons2, "reinforced_plate", true);
    ASSERT_NE(inC1, nullptr);
    ASSERT_NE(inC2, nullptr);

    uint64_t conn1 = graph->addConnection(outM1->id, inC1->id);
    uint64_t conn2 = graph->addConnection(outM1->id, inC2->id);
    EXPECT_NE(conn1, (uint64_t)-1);
    EXPECT_NE(conn2, (uint64_t)-1);
    EXPECT_EQ(graph->getConnectionsForPort(outM1->id).size(), 2);

    // Remove multiNode
    EXPECT_TRUE(graph->removeNode(multiNode));

    // Verify all connections involving outM1 are removed
    EXPECT_EQ(graph->getConnection(conn1), nullptr);
    EXPECT_EQ(graph->getConnection(conn2), nullptr);
    EXPECT_TRUE(graph->getConnectionsForPort(inC1->id).empty());
    EXPECT_TRUE(graph->getConnectionsForPort(inC2->id).empty());
}

TEST_F(FactoryGraphTest, ClearGraph) {
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    Port* fromPort = getPort(n1, "iron_ore", false);
    Port* toPort = getPort(n2, "iron_ore", true);
    graph->addConnection(fromPort->id, toPort->id);

    ASSERT_FALSE(graph->getNodes().empty());
    ASSERT_FALSE(graph->getPorts().empty());
    ASSERT_FALSE(graph->getConnections().empty());

    graph->clear();

    EXPECT_TRUE(graph->getNodes().empty());
    EXPECT_TRUE(graph->getPorts().empty());
    EXPECT_TRUE(graph->getConnections().empty());

    // Check that ID counters were reset
    uint64_t newNodeId = addNode("mine_ore");
    EXPECT_EQ(newNodeId, 0); // next_node_id was reset

    Node* newNode = graph->getNode(newNodeId);
    ASSERT_FALSE(newNode->output_ports.empty());
    Port* newPort = graph->getPort(newNode->output_ports[0]);
    ASSERT_NE(newPort, nullptr);
    // Note: next_port_id is reset in deserialize, but not in clear.
    // Let's check the code...
    // `clear()` resets `next_port_id` to 0.
    // `deserialize()` also resets `next_port_id` to 0.
    // The first port ID (input "nothing") should be 0.
    // The second port ID (output "iron_ore") should be 1.
    EXPECT_EQ(newPort->id, 1);
}

TEST_F(FactoryGraphTest, RestoreNodeAndConnection) {
    // This tests the functions used by deserialization and undo/redo

    // 1. Restore Node
    Node n;
    n.id = 100;
    n.name = "Restored Node";
    n.selected_recipe_key = "mine_ore";

    Port p_in;
    p_in.id = 101;
    p_in.node_id = 100;
    p_in.resource_key = "nothing";

    Port p_out;
    p_out.id = 102;
    p_out.node_id = 100;
    p_out.resource_key = "iron_ore";

    n.input_ports.push_back(p_in.id);
    n.output_ports.push_back(p_out.id);

    graph->restoreNode(n, {p_in, p_out});

    ASSERT_EQ(graph->getNodes().size(), 1);
    ASSERT_EQ(graph->getPorts().size(), 2);
    ASSERT_NE(graph->getNode(100), nullptr);
    ASSERT_NE(graph->getPort(101), nullptr);
    ASSERT_NE(graph->getPort(102), nullptr);
    EXPECT_EQ(graph->getNode(100)->name, "Restored Node");
    EXPECT_EQ(graph->getPort(101)->resource_key, "nothing");

    // 2. Restore Connection
    Connection c;
    c.id = 103;
    c.from_port = 102;
    c.to_port = 101; // Invalid, but restore doesn't check
    c.resource_key = "iron_ore";

    graph->restoreConnection(c);

    ASSERT_EQ(graph->getConnections().size(), 1);
    Connection* restoredConn = graph->getConnection(103);
    ASSERT_NE(restoredConn, nullptr);
    EXPECT_EQ(restoredConn->from_port, 102);
    EXPECT_EQ(graph->getConnectionsForPort(102).size(), 1);
    EXPECT_EQ(graph->getConnectionsForPort(101).size(), 1);
}

TEST_F(FactoryGraphTest, SerializeAndDeserialize) {
    // 1. Build a graph
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    uint64_t n3 = addNode("make_plates");

    Port* p1_out = getPort(n1, "iron_ore", false);
    Port* p2_in = getPort(n2, "iron_ore", true);
    Port* p2_out = getPort(n2, "iron_ingot", false);
    Port* p3_in = getPort(n3, "iron_ingot", true);

    // Add connections
    uint64_t c1 = graph->addConnection(p1_out->id, p2_in->id);
    uint64_t c2 = graph->addConnection(p2_out->id, p3_in->id);

    // Add constraint
    graph->setPortConstraint(p3_in->id, 120.0);

    // 2. Serialize
    nlohmann::json j = graph->serialize();

    // Check basic serialized data
    EXPECT_EQ(j["nodes"].size(), 3);
    EXPECT_EQ(j["connections"].size(), 2);
    EXPECT_EQ(j["next_node_id"], 3);
    EXPECT_EQ(j["next_connection_id"], 2);

    // 3. Deserialize into a new graph
    GameData newData = testGameData; // Use the same game data
    FactoryGraph newGraph(newData);
    newGraph.deserialize(j, newData);

    // 4. Validate new graph
    ASSERT_EQ(newGraph.getNodes().size(), 3);
    ASSERT_EQ(newGraph.getConnections().size(), 2);
    ASSERT_EQ(newGraph.getPorts().size(), 6); // 2 ports per node

    // Check nodes
    Node* newNode1 = newGraph.getNode(n1);
    Node* newNode3 = newGraph.getNode(n3);
    ASSERT_NE(newNode1, nullptr);
    ASSERT_NE(newNode3, nullptr);
    EXPECT_EQ(newNode1->selected_recipe_key, "mine_ore");
    EXPECT_EQ(newNode3->selected_recipe_key, "make_plates");

    // Check connections
    Connection* newConn1 = newGraph.getConnection(c1);
    Connection* newConn2 = newGraph.getConnection(c2);
    ASSERT_NE(newConn1, nullptr);
    ASSERT_NE(newConn2, nullptr);

    // Check that ports were re-mapped correctly
    // We can't guarantee port IDs are the same, but we can check they connect
    // the right nodes.
    // *Update*: The deserialize logic *re-generates* port IDs.
    // We need to find the new ports and check constraints.

    Port* newP3_in = nullptr;
    for (uint64_t pId : newNode3->input_ports) {
        Port* p = newGraph.getPort(pId);
        if (p->resource_key == "iron_ingot") {
            newP3_in = p;
            break;
        }
    }

    ASSERT_NE(newP3_in, nullptr);
    // Check constraint
    EXPECT_EQ(newP3_in->user_constraint, 120.0);

    // Check connection
    auto conns = newGraph.getConnectionsForPort(newP3_in->id);
    ASSERT_EQ(conns.size(), 1);
    EXPECT_EQ(conns[0]->id, c2);
}

TEST_F(FactoryGraphTest, DeserializeWithMismatchedGameData) {
    uint64_t n1 = addNode("mine_ore");
    nlohmann::json j = graph->serialize();

    // Create new EMPTY game data
    GameData emptyData;
    emptyData.gameName = "Empty";
    FactoryGraph newGraph(emptyData);

    // Deserialize. This should "fail" gracefully (i.e., load no nodes)
    // because the recipe "mine_ore" doesn't exist.
    newGraph.deserialize(j, emptyData);

    // Check that the graph is empty
    EXPECT_EQ(newGraph.getNodes().size(), 0);
    EXPECT_EQ(newGraph.getConnections().size(), 0);
}

// [ ... all your existing tests ... ]

TEST_F(FactoryGraphTest, SetNodeRecipeFailure) {
    // Test branch: if (!node)
    EXPECT_FALSE(graph->setNodeRecipe(999, "mine_ore"));

    // Test branch: if (game_data.recipes.count(recipe_key) == 0)
    uint64_t n1 = addNode("mine_ore");
    // This call will fail, but the node will be left in a state with no ports.
    EXPECT_FALSE(graph->setNodeRecipe(n1, "non_existent_recipe"));
}

TEST_F(FactoryGraphTest, RemovePortFromOutputList) {
    // This tests the `else` branch in removePort's parentNode check,
    // ensuring we can remove from the output_ports list.
    uint64_t n1 = addNode("smelt_ingot");
    Node* node = graph->getNode(n1);
    ASSERT_NE(node, nullptr);
    ASSERT_FALSE(node->output_ports.empty());

    uint64_t outPortId = node->output_ports[0]; // Get the output port
    Port* outPort = graph->getPort(outPortId);
    ASSERT_NE(outPort, nullptr);
    EXPECT_EQ(outPort->resource_key, "iron_ingot");

    // Remove the output port
    ASSERT_TRUE(graph->removePort(outPortId));

    // Check it's gone from the node's list
    for (uint64_t pId : node->output_ports) {
        EXPECT_NE(pId, outPortId);
    }
}

// New Fixture specifically for testing deserialization
class FactoryGraphDeserializeTest : public FactoryGraphTest {
protected:
    void SetUp() override {
        // Call the parent SetUp to get the rich testGameData
        FactoryGraphTest::SetUp();

        // Add a recipe with no machines, to test that branch
        Recipe recipe_no_machine;
        recipe_no_machine.name = "Recipe With No Machine";
        recipe_no_machine.input_ports.push_back(RecipePort{1.0, "nothing"});
        recipe_no_machine.output_ports.push_back(RecipePort{1.0, "iron_ore"});
        // recipe_no_machine.produced_in_machines_keys is EMPTY

        testGameData.recipes["recipe_no_machine"] = recipe_no_machine;

        // Re-init graph with the new game data
        graph = std::make_unique<FactoryGraph>(testGameData);
    }
};

TEST_F(FactoryGraphDeserializeTest, DeserializeCorruptedNode) {
    nlohmann::json j;

    // Test branch: if (nodeId == -1 || recipeKey.empty())
    j["nodes"] = nlohmann::json::array();
    j["nodes"].push_back({
        {"id", -1}, // Invalid ID
        {"selected_recipe_key", "mine_ore"}
    });
    j["nodes"].push_back({
        {"id", 1},
        {"selected_recipe_key", ""} // Invalid recipe key
    });

    graph->deserialize(j, testGameData);

    // The graph should have skipped both invalid nodes
    EXPECT_EQ(graph->getNodes().size(), 0);
}

TEST_F(FactoryGraphDeserializeTest, DeserializeRecipeWithNoMachine) {
    // Test branch: if (!recipe.produced_in_machines_keys.empty()) [else]
    uint64_t n1 = addNode("recipe_no_machine");
    Node* node = graph->getNode(n1);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->machine_key, ""); // Should be empty

    nlohmann::json j = graph->serialize();

    FactoryGraph newGraph(testGameData);
    newGraph.deserialize(j, testGameData);

    Node* newNode = newGraph.getNode(n1);
    ASSERT_NE(newNode, nullptr);
    // Check that the empty machine key was correctly deserialized
    EXPECT_EQ(newNode->machine_key, "");
}

TEST_F(FactoryGraphDeserializeTest, DeserializeCorruptedConnection) {
    nlohmann::json j;
    // Add a valid node so ports can be generated
    uint64_t n1 = addNode("mine_ore");
    j = graph->serialize();

    // Now add invalid connections

    // Test branch: if (oldFromPort == -1 || oldToPort == -1)
    j["connections"].push_back({
        {"id", 100},
        {"from_port", -1}, // Invalid from_port
        {"to_port", 1}
    });
    j["connections"].push_back({
        {"id", 101},
        {"from_port", 1},
        {"to_port", -1} // Invalid to_port
    });

    // Test branch: if (conn.id == -1)
    j["connections"].push_back({
        {"id", -1}, // Invalid connection ID
        {"from_port", 1},
        {"to_port", 2}
    });

    graph->deserialize(j, testGameData);

    // The graph should have the 1 node, but 0 connections (it skips all bad ones)
    EXPECT_EQ(graph->getNodes().size(), 1);
    EXPECT_EQ(graph->getConnections().size(), 0);
}

TEST_F(FactoryGraphDeserializeTest, DeserializeConnectionToMissingPort) {
    // This tests the branch:
    // if (oldToNewPortIdMap.count(oldFromPort) && oldToNewPortIdMap.count(oldToPort))

    // 1. Create a valid graph with two nodes and a connection
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    Port* p1_out = getPort(n1, "iron_ore", false);
    Port* p2_in = getPort(n2, "iron_ore", true);
    graph->addConnection(p1_out->id, p2_in->id);

    // 2. Serialize it
    nlohmann::json j = graph->serialize();
    ASSERT_EQ(j["nodes"].size(), 2);
    ASSERT_EQ(j["connections"].size(), 1);

    // 3. Manually corrupt the JSON by removing the second node
    j["nodes"].erase(1); // Erase node `n2`
    ASSERT_EQ(j["nodes"].size(), 1);
    // The connection in the JSON now points to ports (p2_in) that
    // will *not* be created, so oldToNewPortIdMap won't contain them.

    // 4. Deserialize
    FactoryGraph newGraph(testGameData);
    newGraph.deserialize(j, testGameData);

    // 5. Check that the graph loaded Node 1, but skipped the connection
    EXPECT_EQ(newGraph.getNodes().size(), 1);
    EXPECT_EQ(newGraph.getConnections().size(), 0);
}

// [ ... all your existing tests ... ]

// --- New tests to target missed branches ---

TEST_F(FactoryGraphTest, GetConnectionWithMultipleConnectionsOnPort) {
    uint64_t n_screws = addNode("make_screws");
    uint64_t n_rfp1 = addNode("make_reinforced");
    uint64_t n_rfp2 = addNode("make_reinforced");

    Port* fromPort = getPort(n_screws, "screw", false);
    Port* toPort1 = getPort(n_rfp1, "screw", true);
    Port* toPort2 = getPort(n_rfp2, "screw", true);

    uint64_t connId1 = graph->addConnection(fromPort->id, toPort1->id);
    uint64_t connId2 = graph->addConnection(fromPort->id, toPort2->id);

    // Test that we can find the *second* connection
    Connection* foundConn = graph->getConnection(fromPort->id, toPort2->id);
    ASSERT_NE(foundConn, nullptr);
    EXPECT_EQ(foundConn->id, connId2);

    // Test a non-existent connection from a port that has other connections
    Port* nonExistentPort = getPort(n_rfp1, "reinforced_plate", false);
    EXPECT_EQ(graph->getConnection(fromPort->id, nonExistentPort->id), nullptr);
}

TEST_F(FactoryGraphTest, ConnectionExistsWithMultipleConnectionsOnPort) {
    uint64_t n_screws = addNode("make_screws");
    uint64_t n_rfp1 = addNode("make_reinforced");
    uint64_t n_rfp2 = addNode("make_reinforced");

    Port* fromPort = getPort(n_screws, "screw", false);
    Port* toPort1 = getPort(n_rfp1, "screw", true);
    Port* toPort2 = getPort(n_rfp2, "screw", true);

    uint64_t connId1 = graph->addConnection(fromPort->id, toPort1->id);
    uint64_t connId2 = graph->addConnection(fromPort->id, toPort2->id);

    // Test that it finds the second connection
    EXPECT_TRUE(graph->connectionExists(fromPort->id, toPort2->id));

    // Test a non-existent connection
    Port* nonExistentPort = getPort(n_rfp1, "reinforced_plate", false);
    EXPECT_FALSE(graph->connectionExists(fromPort->id, nonExistentPort->id));
}

TEST_F(FactoryGraphTest, RemoveConnectionWithMultipleConnectionsOnPort) {
    uint64_t n_screws = addNode("make_screws");
    uint64_t n_rfp1 = addNode("make_reinforced");
    uint64_t n_rfp2 = addNode("make_reinforced");

    Port* fromPort = getPort(n_screws, "screw", false);
    Port* toPort1 = getPort(n_rfp1, "screw", true);
    Port* toPort2 = getPort(n_rfp2, "screw", true);

    uint64_t connId1 = graph->addConnection(fromPort->id, toPort1->id);
    uint64_t connId2 = graph->addConnection(fromPort->id, toPort2->id);

    // This tests the `break;` logic inside the `m_connectionsByPort` loop
    ASSERT_TRUE(graph->removeConnection(fromPort->id, toPort1->id));
    EXPECT_EQ(graph->getConnections().size(), 1);
    EXPECT_EQ(graph->getConnection(connId1), nullptr);
    EXPECT_NE(graph->getConnection(connId2), nullptr);

    // Check that the port maps were updated correctly
    EXPECT_EQ(graph->getConnectionsForPort(fromPort->id).size(), 1);
    EXPECT_EQ(graph->getConnectionsForPort(toPort1->id).size(), 0);
    EXPECT_EQ(graph->getConnectionsForPort(toPort2->id).size(), 1);
}

TEST_F(FactoryGraphDeserializeTest, DeserializeWithEmptyArrays) {
    nlohmann::json j;
    j["nodes"] = nlohmann::json::array();
    j["connections"] = nlohmann::json::array();
    j["next_node_id"] = 0;
    j["next_connection_id"] = 0;

    // This tests the `if (j.contains(...) ...)` branches for empty arrays
    graph->deserialize(j, testGameData);

    EXPECT_EQ(graph->getNodes().size(), 0);
    EXPECT_EQ(graph->getConnections().size(), 0);
}

TEST_F(FactoryGraphDeserializeTest, DeserializeWithInvalidJsonTypes) {
    nlohmann::json j;

    // Test branches: `... && j["nodes"].is_array()`
    j["nodes"] = "not_an_array";

    // Test branches: `... && j["connections"].is_array()`
    j["connections"] = 123;

    j["next_node_id"] = "not_a_number"; // tests .value() default

    graph->deserialize(j, testGameData);

    // Graph should be empty as it skipped all invalid sections
    EXPECT_EQ(graph->getNodes().size(), 0);
    EXPECT_EQ(graph->getConnections().size(), 0);
}

TEST_F(FactoryGraphDeserializeTest, DeserializeNodeWithInvalidPortArrayTypes) {
    nlohmann::json j;
    j["nodes"] = nlohmann::json::array();

    // Test branches: `...value("input_ports", nlohmann::json::array())`
    j["nodes"].push_back({
        {"id", 0},
        {"selected_recipe_key", "mine_ore"},
        {"input_ports", "not_an_array"}, // Invalid type
        {"output_ports", 123} // Invalid type
    });

    graph->deserialize(j, testGameData);

    ASSERT_EQ(graph->getNodes().size(), 1);
    Node* node = graph->getNode(0);
    ASSERT_NE(node, nullptr);

    EXPECT_EQ(node->input_ports.size(), 1);
    EXPECT_EQ(node->output_ports.size(), 1);
    EXPECT_EQ(graph->getPorts().size(), 2);
}

TEST_F(FactoryGraphDeserializeTest, DeserializeNodeWithInvalidPortConstraintType) {
    nlohmann::json j;
    j["nodes"] = nlohmann::json::array();

    // Test branch: `...value("port_constraints", nlohmann::json::object())`
    j["nodes"].push_back({
        {"id", 0},
        {"selected_recipe_key", "mine_ore"},
        {"port_constraints", "not_an_object"} // Invalid type
    });

    graph->deserialize(j, testGameData);

    ASSERT_EQ(graph->getNodes().size(), 1);
    Node* node = graph->getNode(0);
    ASSERT_NE(node, nullptr);

    // Node and ports are loaded, but constraints are skipped
    EXPECT_EQ(node->input_ports.size(), 1);
    EXPECT_EQ(node->output_ports.size(), 1);

    Port* inPort = graph->getPort(node->input_ports[0]);
    EXPECT_EQ(inPort->user_constraint, -1.0); // Remains default
}

TEST_F(FactoryGraphTest, RemoveConnectionById) {
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    Port* fromPort = getPort(n1, "iron_ore", false);
    Port* toPort = getPort(n2, "iron_ore", true);
    uint64_t connId = graph->addConnection(fromPort->id, toPort->id);

    ASSERT_EQ(graph->getConnections().size(), 1);
    EXPECT_FALSE(graph->removeConnection(99999));
    EXPECT_TRUE(graph->removeConnection(connId));
    EXPECT_EQ(graph->getConnections().size(), 0);
    EXPECT_FALSE(graph->connectionExists(fromPort->id, toPort->id));
    EXPECT_EQ(graph->getConnection(connId), nullptr);
    EXPECT_EQ(graph->getConnectionsForPort(fromPort->id).size(), 0);
    EXPECT_EQ(graph->getConnectionsForPort(toPort->id).size(), 0);
}

TEST_F(FactoryGraphTest, SetNodeRecipeCleansUpOldPortsAndConnections) {
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    Port* fromPort = getPort(n1, "iron_ore", false);
    Port* toPort = getPort(n2, "iron_ore", true);
    uint64_t connId = graph->addConnection(fromPort->id, toPort->id);

    ASSERT_EQ(graph->getPorts().size(), 4);
    ASSERT_EQ(graph->getConnections().size(), 1);

    // Change recipe of n2 to "make_plates"
    ASSERT_TRUE(graph->setNodeRecipe(n2, "make_plates"));

    // Total ports should be 2 (from n1) + 2 (from n2) = 4, not 6
    EXPECT_EQ(graph->getPorts().size(), 4);
    // Connection should be automatically removed because the connected port was destroyed
    EXPECT_EQ(graph->getConnections().size(), 0);
    EXPECT_EQ(graph->getConnection(connId), nullptr);
    EXPECT_EQ(graph->getConnectionsForPort(fromPort->id).size(), 0);

    // Check that new ports exist on n2
    Node* node2 = graph->getNode(n2);
    ASSERT_NE(node2, nullptr);
    EXPECT_EQ(node2->selected_recipe_key, "make_plates");
    EXPECT_EQ(node2->input_ports.size(), 1);
    EXPECT_EQ(node2->output_ports.size(), 1);
    EXPECT_NE(graph->getPort(node2->input_ports[0]), nullptr);
    EXPECT_EQ(graph->getPort(node2->input_ports[0])->resource_key, "iron_ingot");
    EXPECT_NE(graph->getPort(node2->output_ports[0]), nullptr);
    EXPECT_EQ(graph->getPort(node2->output_ports[0])->resource_key, "iron_plate");
}

TEST_F(FactoryGraphTest, ClearGraphCleansPortConnectionsAndPreferredMachines) {
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    Port* fromPort = getPort(n1, "iron_ore", false);
    Port* toPort = getPort(n2, "iron_ore", true);
    graph->addConnection(fromPort->id, toPort->id);
    graph->setMachinePreferred("smelter", true);
    EXPECT_TRUE(graph->isMachinePreferred("smelter"));
    EXPECT_FALSE(graph->getConnectionsForPort(fromPort->id).empty());

    graph->clear();

    EXPECT_FALSE(graph->isMachinePreferred("smelter"));
    EXPECT_TRUE(graph->getConnectionsForPort(fromPort->id).empty());
    EXPECT_TRUE(graph->getNodes().empty());
    EXPECT_TRUE(graph->getPorts().empty());
    EXPECT_TRUE(graph->getConnections().empty());
}

TEST_F(FactoryGraphDeserializeTest, DeserializedConnectionPopulatesResourceKey) {
    uint64_t n1 = graph->addNode("Miner", "mine_ore");
    uint64_t n2 = graph->addNode("Smelter", "smelt_ingot");
    Node* node1 = graph->getNode(n1);
    Node* node2 = graph->getNode(n2);
    ASSERT_NE(node1, nullptr);
    ASSERT_NE(node2, nullptr);
    ASSERT_FALSE(node1->output_ports.empty());
    ASSERT_FALSE(node2->input_ports.empty());
    uint64_t outPortId = node1->output_ports[0];
    uint64_t inPortId = node2->input_ports[0];
    uint64_t connId = graph->addConnection(outPortId, inPortId);

    nlohmann::json j = graph->serialize();

    FactoryGraph newGraph(testGameData);
    newGraph.deserialize(j, testGameData);

    ASSERT_EQ(newGraph.getConnections().size(), 1);
    const Connection* conn = newGraph.getConnection(connId);
    ASSERT_NE(conn, nullptr);
    EXPECT_EQ(conn->resource_key, "iron_ore");
}

TEST_F(FactoryGraphTest, ConstConnectionExistsAndGetConnection) {
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    Port* fromPort = getPort(n1, "iron_ore", false);
    Port* toPort = getPort(n2, "iron_ore", true);
    uint64_t connId = graph->addConnection(fromPort->id, toPort->id);

    const FactoryGraph& constGraph = *graph;
    EXPECT_TRUE(constGraph.connectionExists(fromPort->id, toPort->id));
    EXPECT_FALSE(constGraph.connectionExists(toPort->id, fromPort->id));
    const Connection* conn = constGraph.getConnection(fromPort->id, toPort->id);
    ASSERT_NE(conn, nullptr);
    EXPECT_EQ(conn->id, connId);
    EXPECT_EQ(constGraph.getConnection(toPort->id, fromPort->id), nullptr);
}

TEST(GameDataManagerTest, LoadFromFileCorruptedJsonReturnsFalseAndPopulatesError) {
    std::filesystem::path tempFile = std::filesystem::temp_directory_path() / "test_corrupted_gamedata.gd";
    std::ofstream ofs(tempFile);
    ofs << "{ \"name\": \"broken\", \"recipes\": [ invalid_json }";
    ofs.close();

    GameDataManager manager;
    std::string error;
    EXPECT_FALSE(manager.loadFromFile(tempFile, error));
    EXPECT_FALSE(error.empty());

    std::filesystem::remove(tempFile);
}

TEST(GameDataManagerTest, LoadFromFileNonExistentFileReturnsFalseAndPopulatesError) {
    GameDataManager manager;
    std::string error;
    EXPECT_FALSE(manager.loadFromFile("/non/existent/path/gamedata_404.gd", error));
    EXPECT_FALSE(error.empty());
}

TEST_F(FactoryGraphTest, ProjectIOSaveHeadlessDoesNotCrash) {
    uint64_t n1 = addNode("mine_ore");
    uint64_t n2 = addNode("smelt_ingot");
    Port* fromPort = getPort(n1, "iron_ore", false);
    Port* toPort = getPort(n2, "iron_ore", true);
    graph->addConnection(fromPort->id, toPort->id);

    std::filesystem::path tempProject = std::filesystem::temp_directory_path() / "test_headless_save.fpp";
    // Saving with default context = nullptr must not crash
    bool saveResult = ProjectIO::saveProject(tempProject.string(), *graph);
    EXPECT_TRUE(saveResult);

    std::ifstream ifs(tempProject);
    ASSERT_TRUE(ifs.is_open());
    nlohmann::json j;
    EXPECT_NO_THROW(ifs >> j);
    EXPECT_TRUE(j.contains("graph_data"));
    EXPECT_TRUE(j.contains("editor_settings"));

    std::filesystem::remove(tempProject);
}

TEST_F(FactoryGraphTest, ProjectIOLoadProjectErrorHandling) {
    FactoryGraph targetGraph(testGameData);
    EXPECT_FALSE(ProjectIO::loadProject("/non/existent/project_404.fpp", targetGraph));

    std::filesystem::path tempCorrupted = std::filesystem::temp_directory_path() / "test_corrupted.fpp";
    std::ofstream ofs(tempCorrupted);
    ofs << "not valid json {{{";
    ofs.close();

    EXPECT_FALSE(ProjectIO::loadProject(tempCorrupted.string(), targetGraph));
    std::filesystem::remove(tempCorrupted);

    std::filesystem::path tempNotObject = std::filesystem::temp_directory_path() / "test_not_object.fpp";
    std::ofstream ofs2(tempNotObject);
    ofs2 << "[1, 2, 3]";
    ofs2.close();

    EXPECT_FALSE(ProjectIO::loadProject(tempNotObject.string(), targetGraph));
    std::filesystem::remove(tempNotObject);
}

TEST(SettingsManagerTest, LoadSettingsWithIntegerAndFloatFontSize) {
    auto configDir = getExecutableDirectory().value_or(std::filesystem::current_path());
    auto settingsPath = configDir / "app_settings.json";
    auto backupPath = configDir / "app_settings.json.bak_test";

    AppSettings origSettings = SettingsManager::instance().getSettings();

    std::error_code ec;
    bool hadExisting = std::filesystem::exists(settingsPath, ec);
    if (hadExisting) {
        std::filesystem::rename(settingsPath, backupPath, ec);
    }

    struct Guard {
        std::filesystem::path p, b;
        bool had;
        AppSettings s;
        ~Guard() {
            std::error_code cleanupEc;
            std::filesystem::remove(p, cleanupEc);
            if (had) {
                std::filesystem::rename(b, p, cleanupEc);
            }
            SettingsManager::instance().setSettings(s);
        }
    } guard{settingsPath, backupPath, hadExisting, origSettings};

    // 1. Write settings with integer fontSize (e.g. 24 instead of 24.0)
    {
        std::ofstream ofs(settingsPath);
        ofs << "{\n  \"fontSize\": 24\n}\n";
    }

    SettingsManager::instance().load();
    EXPECT_FLOAT_EQ(SettingsManager::instance().getSettings().fontSize, 24.0f);

    // Test atomic save
    SettingsManager::instance().save();
    EXPECT_TRUE(std::filesystem::exists(settingsPath));
    EXPECT_FALSE(std::filesystem::exists(settingsPath.string() + ".tmp"));

    // 2. Write settings with float fontSize (e.g. 19.5)
    {
        std::ofstream ofs(settingsPath);
        ofs << "{\n  \"fontSize\": 19.5\n}\n";
    }

    SettingsManager::instance().load();
    EXPECT_FLOAT_EQ(SettingsManager::instance().getSettings().fontSize, 19.5f);

    // 3. Write invalid negative fontSize (should not overwrite existing valid size)
    {
        std::ofstream ofs(settingsPath);
        ofs << "{\n  \"fontSize\": -5.0\n}\n";
    }

    SettingsManager::instance().load();
    EXPECT_FLOAT_EQ(SettingsManager::instance().getSettings().fontSize, 19.5f);
}

TEST(SessionManagerTest, AtomicSaveLeavesNoTempFile) {
    auto sessionPath = getExecutableDirectory().value_or(std::filesystem::current_path()) / "session.json";
    auto backupPath = sessionPath.string() + ".bak_test";

    SessionState origState = SessionManager::instance().getSessionState();

    std::error_code ec;
    bool hadExisting = std::filesystem::exists(sessionPath, ec);
    if (hadExisting) {
        std::filesystem::rename(sessionPath, backupPath, ec);
    }

    struct Guard {
        std::filesystem::path p, b;
        bool had;
        SessionState s;
        ~Guard() {
            std::error_code cleanupEc;
            std::filesystem::remove(p, cleanupEc);
            if (had) {
                std::filesystem::rename(b, p, cleanupEc);
            }
            SessionManager::instance().setSessionState(s);
        }
    } guard{sessionPath, backupPath, hadExisting, origState};

    SessionManager::instance().save();
    EXPECT_TRUE(std::filesystem::exists(sessionPath));
    EXPECT_FALSE(std::filesystem::exists(sessionPath.string() + ".tmp"));
}

TEST(RecentFilesTest, AtomicSaveLeavesNoTempFile) {
    auto recentPath = getExecutableDirectory().value_or(std::filesystem::current_path()) / "recent.json";
    auto backupPath = recentPath.string() + ".bak_test";

    std::error_code ec;
    bool hadExisting = std::filesystem::exists(recentPath, ec);
    if (hadExisting) {
        std::filesystem::rename(recentPath, backupPath, ec);
    }

    struct Guard {
        std::filesystem::path p, b;
        bool had;
        ~Guard() {
            std::error_code cleanupEc;
            std::filesystem::remove(p, cleanupEc);
            if (had) {
                std::filesystem::rename(b, p, cleanupEc);
            }
            RecentFiles::instance().load();
        }
    } guard{recentPath, backupPath, hadExisting};

    RecentFiles::instance().save();
    EXPECT_TRUE(std::filesystem::exists(recentPath));
    EXPECT_FALSE(std::filesystem::exists(recentPath.string() + ".tmp"));
}

TEST(TextureManagerTest, CacheMissingTexturePreventsRepeatedDiskLookups) {
    std::filesystem::path nonExistent = "this_path_does_not_exist_12345.png";
    ImTextureID id1 = TextureManager::instance().loadTexture(nonExistent);
    ImTextureID id2 = TextureManager::instance().loadTexture(nonExistent);
    EXPECT_EQ(id1, id2);

    TextureManager::instance().invalidateTexture(nonExistent);
    ImTextureID id3 = TextureManager::instance().loadTexture(nonExistent);
    EXPECT_EQ(id1, id3);

    TextureManager::instance().cleanup();
}

TEST(GameDataScannerTest, ScanNonExistentDirectoryReturnsEmpty) {
    auto pkgs = scanForGameData("/non/existent/path/for/scanner");
    EXPECT_TRUE(pkgs.empty());
}

TEST(GameDataEditorTest, VectorDeletionLoopDoesNotSkipElements) {
    // 1. Delete middle element
    {
        std::vector<std::string> keys = {"machine_A", "machine_B", "machine_C", "machine_D"};
        std::vector<std::string> visited;

        for (int i = 0; i < (int)keys.size(); ++i) {
            visited.push_back(keys[i]);
            if (keys[i] == "machine_B") {
                keys.erase(keys.begin() + i);
                --i;
                continue;
            }
        }

        std::vector<std::string> expectedVisited = {"machine_A", "machine_B", "machine_C", "machine_D"};
        EXPECT_EQ(visited, expectedVisited);
        std::vector<std::string> expectedRemaining = {"machine_A", "machine_C", "machine_D"};
        EXPECT_EQ(keys, expectedRemaining);
    }

    // 2. Delete first element
    {
        std::vector<std::string> keys = {"first", "second", "third"};
        std::vector<std::string> visited;

        for (int i = 0; i < (int)keys.size(); ++i) {
            visited.push_back(keys[i]);
            if (keys[i] == "first") {
                keys.erase(keys.begin() + i);
                --i;
                continue;
            }
        }

        std::vector<std::string> expectedVisited = {"first", "second", "third"};
        EXPECT_EQ(visited, expectedVisited);
        std::vector<std::string> expectedRemaining = {"second", "third"};
        EXPECT_EQ(keys, expectedRemaining);
    }

    // 3. Delete consecutive elements
    {
        std::vector<std::string> keys = {"A", "delete_1", "delete_2", "B"};
        std::vector<std::string> visited;

        for (int i = 0; i < (int)keys.size(); ++i) {
            visited.push_back(keys[i]);
            if (keys[i].rfind("delete_", 0) == 0) {
                keys.erase(keys.begin() + i);
                --i;
                continue;
            }
        }

        std::vector<std::string> expectedVisited = {"A", "delete_1", "delete_2", "B"};
        EXPECT_EQ(visited, expectedVisited);
        std::vector<std::string> expectedRemaining = {"A", "B"};
        EXPECT_EQ(keys, expectedRemaining);
    }
}

TEST(StringUtilsTest, SlugifyBasic) {
    EXPECT_EQ(slugify("Iron Plate"), "iron-plate");
    EXPECT_EQ(slugify("Copper Wire"), "copper-wire");
    EXPECT_EQ(slugify("Miner Mk.1"), "miner-mk-1");
    EXPECT_EQ(slugify("Assembler 2"), "assembler-2");
}

TEST(StringUtilsTest, SlugifySpecialChars) {
    EXPECT_EQ(slugify("C++ Engine"), "cplusplus-engine");
    EXPECT_EQ(slugify("recipe:iron:ingot"), "recipe-iron-ingot");
    EXPECT_EQ(slugify("100% Pure Water!"), "100-pure-water");
    EXPECT_EQ(slugify("item @#$ with *&^ symbols"), "item-with-symbols");
    EXPECT_EQ(slugify("multiple   spaces   and---hyphens"), "multiple-spaces-and-hyphens");
}

TEST(StringUtilsTest, SlugifyEdgeCases) {
    EXPECT_EQ(slugify(""), "");
    EXPECT_EQ(slugify("   "), "");
    EXPECT_EQ(slugify("\t\n  \r"), "");
    EXPECT_EQ(slugify("---leading-and-trailing---"), "leading-and-trailing");
    EXPECT_EQ(slugify("++++"), "plusplusplusplus");
    EXPECT_EQ(slugify("!@#$%^&*()"), "");
}

TEST(StringUtilsTest, NaturalLessNumbers) {
    EXPECT_TRUE(naturalLess("file2", "file10"));
    EXPECT_FALSE(naturalLess("file10", "file2"));
    EXPECT_FALSE(naturalLess("file2", "file2"));
    EXPECT_TRUE(naturalLess("item1", "item2"));
    EXPECT_TRUE(naturalLess("test01", "test10"));
    EXPECT_TRUE(naturalLess("node1_in", "node1_out"));
    EXPECT_FALSE(naturalLess("node1_out", "node1_in"));
}

TEST(StringUtilsTest, NaturalLessCaseInsensitive) {
    EXPECT_TRUE(naturalLess("FILE2", "file10"));
    EXPECT_TRUE(naturalLess("file2", "FILE10"));
    EXPECT_FALSE(naturalLess("abc", "ABC"));
    EXPECT_FALSE(naturalLess("ABC", "abc"));
    EXPECT_TRUE(naturalLess("abc", "ABD"));
    EXPECT_TRUE(naturalLess("ABC", "abd"));
}

TEST(StringUtilsTest, NaturalLessPrefixAndBoundaries) {
    EXPECT_TRUE(naturalLess("file", "file1"));
    EXPECT_FALSE(naturalLess("file1", "file"));
    EXPECT_TRUE(naturalLess("", "a"));
    EXPECT_FALSE(naturalLess("a", ""));
    EXPECT_FALSE(naturalLess("", ""));
}

TEST(StringUtilsTest, NaturalLessNonAsciiSafety) {
    // Non-ASCII bytes (e.g. UTF-8 or values > 127) must not trigger UB or assertions
    std::string s1 = "caf\xC3\xA9";
    std::string s2 = "cafe";
    EXPECT_NO_FATAL_FAILURE({
        bool res1 = naturalLess(s1, s2);
        bool res2 = naturalLess(s2, s1);
        EXPECT_NE(res1, res2);
    });

    std::string highByte = "\xFF";
    std::string asciiChar = "a";
    EXPECT_NO_FATAL_FAILURE({
        naturalLess(highByte, asciiChar);
        naturalLess(asciiChar, highByte);
    });
}

TEST(NotificationManagerTest, AddNotificationFormatting) {
    NotificationManager& nm = NotificationManager::instance();
    EXPECT_NO_FATAL_FAILURE({
        nm.addNotification("Test Notification", "All 3 copied nodes contained invalid recipes.", NotificationType::Warning);
        nm.addNotification("Success", "Loaded successfully", NotificationType::Success);
    });
}

TEST(TextureUtilsTest, LoadFromFile_EdgeCases) {
    unsigned int tex = 0;
    int w = 0, h = 0;

    // Null filename returns false
    EXPECT_FALSE(TextureUtils::loadTextureFromFile(nullptr, &tex, &w, &h));

    // Null out_texture returns false without crashing or leaking
    EXPECT_FALSE(TextureUtils::loadTextureFromFile("non_existent.png", nullptr, &w, &h));

    // Non-existent path returns false
    EXPECT_FALSE(TextureUtils::loadTextureFromFile("/non/existent/path/image.png", &tex, &w, &h));

    // Empty filename returns false
    EXPECT_FALSE(TextureUtils::loadTextureFromFile("", &tex, &w, &h));
}

TEST(TextureUtilsTest, LoadFromMemory_EdgeCases) {
    unsigned int tex = 0;
    int w = 0, h = 0;

    // Null data returns false
    EXPECT_FALSE(TextureUtils::loadTextureFromMemory(nullptr, 100, &tex, &w, &h));

    // Zero size returns false
    const unsigned char dummy[4] = {0, 0, 0, 0};
    EXPECT_FALSE(TextureUtils::loadTextureFromMemory(dummy, 0, &tex, &w, &h));

    // Null out_texture returns false
    EXPECT_FALSE(TextureUtils::loadTextureFromMemory(dummy, 4, nullptr, &w, &h));

    // Corrupted/non-image data returns false
    const char corruptData[] = "NotAnImageHeader";
    EXPECT_FALSE(TextureUtils::loadTextureFromMemory(corruptData, sizeof(corruptData), &tex, &w, &h));
}

TEST(GraphPrimitivesTest, DefaultInitializers) {
    // Audit Item 3.8: In-class default member initializers for Port, Node, Connection
    Port p;
    EXPECT_EQ(p.id, 0);
    EXPECT_EQ(p.node_id, 0);
    EXPECT_DOUBLE_EQ(p.rate, 0.0);
    EXPECT_DOUBLE_EQ(p.user_constraint, -1.0);
    EXPECT_DOUBLE_EQ(p.excess_rate, 0.0);
    EXPECT_TRUE(p.resource_key.empty());

    Node n;
    EXPECT_EQ(n.id, 0);
    EXPECT_DOUBLE_EQ(n.machine_count, 0.0);
    EXPECT_DOUBLE_EQ(n.clock_speed, 100.0);
    EXPECT_DOUBLE_EQ(n.production_multiplier, 100.0);
    EXPECT_TRUE(n.name.empty());
    EXPECT_TRUE(n.machine_key.empty());
    EXPECT_TRUE(n.selected_recipe_key.empty());
    EXPECT_TRUE(n.input_ports.empty());
    EXPECT_TRUE(n.output_ports.empty());

    Connection c;
    EXPECT_EQ(c.id, 0);
    EXPECT_EQ(c.from_port, 0);
    EXPECT_EQ(c.to_port, 0);
    EXPECT_DOUBLE_EQ(c.rate, 0.0);
    EXPECT_TRUE(c.resource_key.empty());
}

TEST(GraphPrimitivesTest, HeaderSelfSufficiency) {
    // Audit Item 3.9: Header self-sufficiency for CopyBuffer.h and Port.h
    CopyBuffer buf;
    EXPECT_TRUE(buf.isEmpty());
    EXPECT_EQ(buf.sourceEditor, nullptr);
    EXPECT_TRUE(buf.gameName.empty());
    EXPECT_TRUE(buf.nodes.empty());
    EXPECT_TRUE(buf.ports.empty());
    EXPECT_TRUE(buf.connections.empty());
    EXPECT_TRUE(buf.nodePositions.empty());
    buf.clear();
    EXPECT_TRUE(buf.isEmpty());
}