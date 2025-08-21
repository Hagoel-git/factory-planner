//
// Created by hagoel on 8/19/25.
//

#include "UndoRedoSystem.h"
#include "imgui_node_editor.h"
#include "../common/IdUtils.h"

namespace ed = ax::NodeEditor;


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
        ed::SetNodePosition(IdUtils::ToNodeId(newNodeId), position);

        if (fromPort != -1) {
            bool fromInput = graph.getPort(fromPort)->isInput;
            // Find the corresponding port on the newly created node to connect to
            const auto &ports_on_new_node = fromInput
                                                ? graph.getNode(newNodeId)->output_ports
                                                : graph.getNode(newNodeId)->input_ports;
            for (int new_port_id: ports_on_new_node) {
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
        for (int port_id: nodeData.input_ports) {
            auto port = graph.getPort(port_id);
            if (port) {
                ports_data.push_back(*port); // Store input port data
            }
        }
        for (int port_id: nodeData.output_ports) {
            auto port = graph.getPort(port_id);
            if (port) {
                ports_data.push_back(*port); // Store output port data
            }
        }
        executed = true;
    } else {
        graph.restoreNode(nodeData, ports_data);
        ed::SetNodePosition(IdUtils::ToNodeId(nodeData.id), position);
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

void RemoveNodeCommand::execute(FactoryGraph &graph) {
    Node *node = graph.getNode(id);
    if (!node) return;
    nodeData = *node; // Store the node data for undo
    position = ed::GetNodePosition(IdUtils::ToNodeId(id));
    // Store all ports and connections for undo
    for (int port_id: nodeData.input_ports) {
        auto port = graph.getPort(port_id);
        if (port) {
            ports_data.push_back(*port); // Store input port data
        }
        for (const auto &conn: graph.getConnectionsForPort(port_id)) {
            connections_data.push_back(*conn); // Store connection data
        }
    }
    for (int port_id: nodeData.output_ports) {
        auto port = graph.getPort(port_id);
        if (port) {
            ports_data.push_back(*port); // Store output port data
        }
        for (const auto &conn: graph.getConnectionsForPort(port_id)) {
            connections_data.push_back(*conn); // Store connection data
        }
    }

    graph.removeNode(id);
}

void RemoveNodeCommand::undo(FactoryGraph &graph) {
    graph.restoreNode(nodeData, ports_data);
    ed::SetNodePosition(IdUtils::ToNodeId(nodeData.id), position);

    // Restore all connections
    for (const auto &conn: connections_data) {
        graph.restoreConnection(conn);
    }
    connections_data.clear();
    ports_data.clear();
    nodeData = Node(); // Clear node data to avoid double undo
}

void AddConnectionCommand::execute(FactoryGraph &graph) {
    if (!executed) {
        int connId = graph.addConnection(fromPort, toPort);
        connectionData = *graph.getConnection(connId); // Store connection data for undo
        executed = true;
    } else {
        graph.restoreConnection(connectionData);
    }
}

void AddConnectionCommand::undo(FactoryGraph &graph) {
    graph.removeConnection(fromPort, toPort);
}

void RemoveConnectionCommand::execute(FactoryGraph &graph) {
    Connection* conn = graph.getConnection(fromPort, toPort);
    if (conn != nullptr) {
        connectionData = *conn;
        graph.removeConnection(fromPort, toPort);
    }
}

void RemoveConnectionCommand::undo(FactoryGraph &graph) {
    graph.restoreConnection(connectionData);
}

void SetPortConstraintCommand::execute(FactoryGraph &graph) {
    graph.setPortDemand(portId, newConstraint);
}

void SetPortConstraintCommand::undo(FactoryGraph &graph) {
    graph.setPortDemand(portId, oldConstraint);
}

void MoveNodeCommand::execute(FactoryGraph &graph) {
    ed::SetNodePosition(IdUtils::ToNodeId(nodeId),  newPosition);
}

void MoveNodeCommand::undo(FactoryGraph &graph) {
    ed::SetNodePosition(IdUtils::ToNodeId(nodeId), oldPosition);
}
