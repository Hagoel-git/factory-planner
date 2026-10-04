#include "UndoRedoSystem.h"
#include "imgui_node_editor.h"
#include "core/graph/FactoryGraph.h"
#include "common/IdUtils.h"
#include <cstdint>
#include <unordered_set>
#include <absl/log/log.h>

namespace ed = ax::NodeEditor;


void UndoRedoManager::executeCommand(std::unique_ptr<Command> command, FactoryGraph &graph) {
    VLOG(2) << "Executing command: " << command->getDescription();
    command->execute(graph);
    undoStack.push_back(std::move(command));
    redoStack.clear(); // Clear redo stack on new command
    trimUndoStack();
}

void UndoRedoManager::undo(FactoryGraph &graph) {
    if (undoStack.empty()) return;

    auto command = std::move(undoStack.back());
    undoStack.pop_back();
    VLOG(2) << "Undoing command: " << command->getDescription();
    command->undo(graph);

    redoStack.push_back(std::move(command));
}

void UndoRedoManager::redo(FactoryGraph &graph) {
    if (redoStack.empty()) return;

    auto command = std::move(redoStack.back());
    redoStack.pop_back();
    VLOG(2) << "Redoing command: " << command->getDescription();
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
        uint64_t newNodeId = graph.addNode(nodeName, recipeKey);
        ed::SetNodePosition(IdUtils::toNodeId(newNodeId), position);

        connectionData = Connection(-1, -1, -1, "");
        if (fromPort != (uint64_t)-1) {
            Port* fromPortPtr = graph.getPort(fromPort);
            if (fromPortPtr) {
                bool fromInput = graph.isInputPort(fromPortPtr->id);
                // Find the corresponding port on the newly created node to connect to
                Node* newNodePtr = graph.getNode(newNodeId);
                if (newNodePtr) {
                    const auto &ports_on_new_node = fromInput
                                                        ? newNodePtr->output_ports
                                                        : newNodePtr->input_ports;
                    for (uint64_t new_port_id: ports_on_new_node) {
                        Port* newPortPtr = graph.getPort(new_port_id);
                        if (newPortPtr && newPortPtr->resource_key == fromPortPtr->resource_key) {
                            // Determine connection direction dynamically
                            uint64_t source_id = fromInput ? new_port_id : fromPort;
                            uint64_t target_id = fromInput ? fromPort : new_port_id;
                            uint64_t newConnId = graph.addConnection(source_id, target_id);
                            if (newConnId != (uint64_t)-1) {
                                auto conn = graph.getConnection(newConnId);
                                if (conn) {
                                    connectionData = *conn; // Store connection data for undo
                                }
                            }

                            break; // Connect to the first available port and stop searching
                        }
                    }
                }
            }
        }
        auto node = graph.getNode(newNodeId);
        if (node) {
            nodeData = *node; // Store the node data for undo
        }
        for (uint64_t port_id: nodeData.input_ports) {
            auto port = graph.getPort(port_id);
            if (port) {
                ports_data.push_back(*port); // Store input port data
            }
        }
        for (uint64_t port_id: nodeData.output_ports) {
            auto port = graph.getPort(port_id);
            if (port) {
                ports_data.push_back(*port); // Store output port data
            }
        }
        executed = true;
    } else {
        graph.restoreNode(nodeData, ports_data);
        ed::SetNodePosition(IdUtils::toNodeId(nodeData.id), position);
        // Reconnect ports if necessary
        if (connectionData.from_port != (uint64_t)-1 && connectionData.to_port != (uint64_t)-1) {
            graph.restoreConnection(connectionData);
        }
    }
}

void AddNodeCommand::undo(FactoryGraph &graph) {
    graph.removeNode(nodeData.id);
}

void RemoveNodeCommand::execute(FactoryGraph &graph) {
    Node *node = graph.getNode(id);
    if (!node) return;
    nodeData = *node; // Store the node data for undo
    nodeName = node->name;
    position = ed::GetNodePosition(IdUtils::toNodeId(id));
    // Store all ports and connections for undo
    ports_data.clear();
    connections_data.clear();
    std::unordered_set<uint64_t> recorded_connections;
    for (uint64_t port_id: nodeData.input_ports) {
        auto port = graph.getPort(port_id);
        if (port) {
            ports_data.push_back(*port); // Store input port data
        }
        for (const auto &conn: graph.getConnectionsForPort(port_id)) {
            if (conn && recorded_connections.insert(conn->id).second) {
                connections_data.push_back(*conn); // Store connection data
            }
        }
    }
    for (uint64_t port_id: nodeData.output_ports) {
        auto port = graph.getPort(port_id);
        if (port) {
            ports_data.push_back(*port); // Store output port data
        }
        for (const auto &conn: graph.getConnectionsForPort(port_id)) {
            if (conn && recorded_connections.insert(conn->id).second) {
                connections_data.push_back(*conn); // Store connection data
            }
        }
    }

    graph.removeNode(id);
}

void RemoveNodeCommand::undo(FactoryGraph &graph) {
    graph.restoreNode(nodeData, ports_data);
    ed::SetNodePosition(IdUtils::toNodeId(nodeData.id), position);

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
        uint64_t connId = graph.addConnection(fromPort, toPort);
        if (connId != (uint64_t)-1) {
            auto conn = graph.getConnection(connId);
            if (conn) {
                connectionData = *conn; // Store connection data for undo
            }
        }
        executed = true;
    } else {
        if (connectionData.from_port != (uint64_t)-1 && connectionData.to_port != (uint64_t)-1) {
            graph.restoreConnection(connectionData);
        }
    }
}

void AddConnectionCommand::undo(FactoryGraph &graph) {
    if (connectionData.from_port != (uint64_t)-1 && connectionData.to_port != (uint64_t)-1) {
        graph.removeConnection(fromPort, toPort);
    }
}

void RemoveConnectionCommand::execute(FactoryGraph &graph) {
    Connection *conn = graph.getConnection(fromPort, toPort);
    if (conn != nullptr) {
        connectionData = *conn;
        graph.removeConnection(fromPort, toPort);
    }
}

void RemoveConnectionCommand::undo(FactoryGraph &graph) {
    if (connectionData.from_port != (uint64_t)-1 && connectionData.to_port != (uint64_t)-1) {
        graph.restoreConnection(connectionData);
    }
}

void ChangeClockSpeedCommand::execute(FactoryGraph &graph) {
    graph.setNodeClockSpeed(nodeId, newSpeed);
}

void ChangeClockSpeedCommand::undo(FactoryGraph &graph) {
    graph.setNodeClockSpeed(nodeId, oldSpeed);
}

void ChangeProductionMultiplierCommand::execute(FactoryGraph &graph) {
    graph.setProductionMultiplier(nodeId, newMultiplier);
}

void ChangeProductionMultiplierCommand::undo(FactoryGraph &graph) {
    graph.setProductionMultiplier(nodeId, oldMultiplier);
}

void SetPortConstraintCommand::execute(FactoryGraph &graph) {
    graph.setPortConstraint(portId, newConstraint);
}

void SetPortConstraintCommand::undo(FactoryGraph &graph) {
    graph.setPortConstraint(portId, oldConstraint);
}

void MoveNodeCommand::execute(FactoryGraph &graph) {
    ed::SetNodePosition(IdUtils::toNodeId(nodeId), newPosition);
}

void MoveNodeCommand::undo(FactoryGraph &graph) {
    ed::SetNodePosition(IdUtils::toNodeId(nodeId), oldPosition);
}

void PasteCommand::execute(FactoryGraph &graph) {
    ed::ClearSelection();
    if (!executed) {
        if (copy_buffer.isEmpty()) return; // Nothing to paste

        ImVec2 mousePos = ed::ScreenToCanvas(ImGui::GetMousePos());
        ImVec2 originalCenter = ImVec2(0, 0);

        for (const auto &pair: copy_buffer.nodePositions) {
            originalCenter.x += pair.second.x; // Sum up original positions
            originalCenter.y += pair.second.y; // Sum up original positions
        }

        ImVec2 offset = ImVec2(0, 0);
        if (!copy_buffer.nodePositions.empty()) {
            originalCenter.x /= copy_buffer.nodePositions.size();
            originalCenter.y /= copy_buffer.nodePositions.size();
            offset = ImVec2(mousePos.x - originalCenter.x, mousePos.y - originalCenter.y);
        }

        std::unordered_map<uint64_t, uint64_t> nodeIdMap;

        for (const auto &pair: copy_buffer.nodes) {
            uint64_t oldId = pair.first;
            const auto &node = pair.second;
            uint64_t newId = graph.addNode(node.name, node.selected_recipe_key);
            auto posIt = copy_buffer.nodePositions.find(oldId);
            ImVec2 oldPos = (posIt != copy_buffer.nodePositions.end()) ? posIt->second : ImVec2(0, 0);
            ImVec2 newPos = ImVec2(oldPos.x + offset.x, oldPos.y + offset.y);

            auto newNodePtr = graph.getNode(newId);
            if (newNodePtr) {
                pastedNodes.push_back(*newNodePtr);
                pastedNodePositions[newId] = newPos;

                ed::SetNodePosition(IdUtils::toNodeId(newId), newPos);
                ed::SelectNode(IdUtils::toNodeId(newId), true);
                nodeIdMap[oldId] = newId; // Map old node ID to new node
            }
        }
        std::unordered_map<uint64_t, uint64_t> oldToNewPortMap;
        for (const auto &pair: nodeIdMap) {
            uint64_t oldNodeId = pair.first;
            uint64_t newNodeId = pair.second;

            const auto& oldNode = copy_buffer.nodes.at(oldNodeId);
            auto newNode = graph.getNode(newNodeId);

            if (newNode) {
                size_t inputCount = std::min(oldNode.input_ports.size(), newNode->input_ports.size());
                for (size_t i = 0; i < inputCount; ++i) {
                    uint64_t oldPortId = oldNode.input_ports[i];
                    uint64_t newPortId = newNode->input_ports[i];
                    oldToNewPortMap[oldPortId] = newPortId;

                    Port* newPort = graph.getPort(newPortId);
                    if (newPort) {
                        auto oldPortIt = copy_buffer.ports.find(oldPortId);
                        if (oldPortIt != copy_buffer.ports.end()) {
                            newPort->user_constraint = oldPortIt->second.user_constraint;
                        }
                        pastedPorts.insert({newNodeId, *newPort});
                    }
                }
                size_t outputCount = std::min(oldNode.output_ports.size(), newNode->output_ports.size());
                for (size_t i = 0; i < outputCount; ++i) {
                    uint64_t oldPortId = oldNode.output_ports[i];
                    uint64_t newPortId = newNode->output_ports[i];
                    oldToNewPortMap[oldPortId] = newPortId;

                    Port* newPort = graph.getPort(newPortId);
                    if (newPort) {
                        auto oldPortIt = copy_buffer.ports.find(oldPortId);
                        if (oldPortIt != copy_buffer.ports.end()) {
                            newPort->user_constraint = oldPortIt->second.user_constraint;
                        }
                        pastedPorts.insert({newNodeId, *newPort});
                    }
                }
            }
        }

        for (const auto &conn: copy_buffer.connections) {
            uint64_t fromPort = conn.from_port;
            uint64_t toPort = conn.to_port;

            bool fromIsCopied = oldToNewPortMap.find(fromPort) != oldToNewPortMap.end();
            bool toIsCopied = oldToNewPortMap.find(toPort) != oldToNewPortMap.end();

            // Case 1: Both ports are copied
            if (fromIsCopied && toIsCopied) {
                uint64_t newFromPort = oldToNewPortMap[fromPort];
                uint64_t newToPort = oldToNewPortMap[toPort];
                uint64_t newConnId = graph.addConnection(newFromPort, newToPort);
                if (newConnId != (uint64_t)-1) {
                    auto conn_ptr = graph.getConnection(newConnId);
                    if (conn_ptr) {
                        pastedConnections.push_back(*conn_ptr);
                    }
                }
            }

            if (mapExternalConnections) {
                // Case 2: From port is copied, to port is external
                if (fromIsCopied && !toIsCopied) {
                    uint64_t newFromPort = oldToNewPortMap[fromPort];
                    uint64_t originalToPort = toPort;
                    uint64_t newConnId = graph.addConnection(newFromPort, originalToPort);
                    if (newConnId != (uint64_t)-1) {
                        auto conn_ptr = graph.getConnection(newConnId);
                        if (conn_ptr) {
                            pastedConnections.push_back(*conn_ptr);
                        }
                    }
                }
                // Case 3: From port is external, to port is copied
                else if (!fromIsCopied && toIsCopied) {
                    uint64_t originalFromPort = fromPort;
                    uint64_t newToPort = oldToNewPortMap[toPort];
                    uint64_t newConnId = graph.addConnection(originalFromPort, newToPort);
                    if (newConnId != (uint64_t)-1) {
                        auto conn_ptr = graph.getConnection(newConnId);
                        if (conn_ptr) {
                            pastedConnections.push_back(*conn_ptr);
                        }
                    }
                }
            }
        }
        executed = true;
    } else {
        for (const auto &node: pastedNodes) {
            std::vector<Port> ports;

            auto range = pastedPorts.equal_range(node.id);
            for (auto it = range.first; it != range.second; ++it) {
                ports.push_back(it->second);
            }

            graph.restoreNode(node, ports);
            auto pos_it = pastedNodePositions.find(node.id);
            if (pos_it != pastedNodePositions.end()) {
                ed::SetNodePosition(IdUtils::toNodeId(node.id), pos_it->second);
            }
        }
        for (const auto &conn: pastedConnections) {
            graph.restoreConnection(conn);
        }
    }
}

void PasteCommand::undo(FactoryGraph &graph) {
    for (const auto &node: pastedNodes) {
        graph.removeNode(node.id);
    }
}

void ChangeMachineCommand::execute(FactoryGraph &graph) {
    graph.changeMachine(nodeId, newMachine);
}

void ChangeMachineCommand::undo(FactoryGraph &graph) {
    graph.changeMachine(nodeId, oldMachine);
}
