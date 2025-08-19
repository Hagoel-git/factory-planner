//
// Created by hagoel on 8/19/25.
//

#include "UndoRedoSystem.h"
#include "imgui_node_editor.h"

namespace ed = ax::NodeEditor;

static constexpr uintptr_t NODE_ID_OFFSET = 0x10000000u;
static constexpr uintptr_t PIN_ID_OFFSET = 0x20000000u;
static constexpr uintptr_t LINK_ID_OFFSET = 0x30000000u;

static inline ed::NodeId ToNodeId(int id) { return ed::NodeId((uintptr_t) id + NODE_ID_OFFSET); }
static inline ed::PinId ToPinId(int id) { return ed::PinId((uintptr_t) id + PIN_ID_OFFSET); }
static inline ed::LinkId ToLinkId(int id) { return ed::LinkId((uintptr_t) id + LINK_ID_OFFSET); }
static inline int FromPinId(ed::PinId id) { return (int) (id.Get() - PIN_ID_OFFSET); }
static inline int FromNodeId(ed::NodeId id) { return (int) (id.Get() - NODE_ID_OFFSET); }
static inline int FromLinkId(ed::LinkId id) { return (int) (id.Get() - LINK_ID_OFFSET); }

void UndoRedoManager::executeCommand(std::unique_ptr<Command> command, FactoryGraph &graph) {
    command->execute(graph);
    undoStack.push_back(std::move(command));
    redoStack.clear(); // Clear redo stack on new command
    trimUndoStack();
}

void UndoRedoManager::undo(FactoryGraph &graph) {
    if (undoStack.empty()) return;

    auto command = std::move(undoStack.back());
    undoStack.pop_back();

    command->undo(graph);

    redoStack.push_back(std::move(command));
}

void UndoRedoManager::redo(FactoryGraph &graph) {
    if (redoStack.empty()) return;

    auto command = std::move(redoStack.back());
    redoStack.pop_back();

    command->execute(graph);

    undoStack.push_back(std::move(command));
}

void UndoRedoManager::trimUndoStack() {
    while (undoStack.size() > maxHistorySize) {
        undoStack.erase(undoStack.begin());
    }
}

void AddNodeCommand::execute(FactoryGraph &graph) {
    if (!executed) {
        int newNodeId = graph.addNode(nodeName, nodeType, recipeId);
        ed::SetNodePosition(ToNodeId(newNodeId), position);

        if (fromPort != -1) {
            bool fromInput = graph.getPort(fromPort)->isInput;
            // Find the corresponding port on the newly created node to connect to
            const auto& ports_on_new_node = fromInput ? graph.getNode(newNodeId)->output_ports : graph.getNode(newNodeId)->input_ports;
            for (int new_port_id : ports_on_new_node) {
                if (graph.getPort(new_port_id)->resource_id == graph.getPort(fromPort)->resource_id) {
                    // Determine connection direction dynamically
                    int source_id = fromInput ? new_port_id : fromPort;
                    int target_id = fromInput ? fromPort : new_port_id;
                    int conn_id = graph.addConnection(source_id, target_id);
                    connectionData = *graph.getConnection(conn_id); // Store connection data for undo

                    break; // Connect to the first available port and stop searching
                }
            }
        } else {
            connectionData = Connection(-1, -1, -1, -1); // No connection data if no port is specified
        }
        auto node = graph.getNode(newNodeId);
        if (node) {
            nodeData = *node; // Store the node data for undo
        }
        for (int port_id : nodeData.input_ports) {
            auto port = graph.getPort(port_id);
            if (port) {
                ports_data.push_back(*port); // Store input port data
            }
        }
        for (int port_id : nodeData.output_ports) {
            auto port = graph.getPort(port_id);
            if (port) {
                ports_data.push_back(*port); // Store output port data
            }
        }
        executed = true;
    } else {
        graph.restoreNode(nodeData, ports_data);
        ed::SetNodePosition(ToNodeId(nodeData.id), position);
        // Reconnect ports if necessary
        if (connectionData.from_port != -1 && connectionData.to_port != -1) {
            graph.restoreConnection(connectionData);
        }
    }
}

void AddNodeCommand::undo(FactoryGraph &graph) {
    graph.removeNode(nodeData.id);
    graph.removeConnection(connectionData.from_port, connectionData.to_port);
}
