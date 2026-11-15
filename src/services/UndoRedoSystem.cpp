#include "UndoRedoSystem.h"
#include "imgui_node_editor.h"
#include "core/graph/FactoryGraph.h"
#include "common/IdUtils.h"
#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <absl/log/log.h>

namespace ed = ax::NodeEditor;

void UndoRedoManager::executeCommand(std::unique_ptr<Command> command, FactoryGraph& graph) {
    VLOG(2) << "Executing command: " << command->getDescription();
    command->execute(graph);
    undoStack.push_back(std::move(command));
    redoStack.clear();
    trimUndoStack();
}

void UndoRedoManager::undo(FactoryGraph& graph) {
    if (undoStack.empty()) return;

    auto command = std::move(undoStack.back());
    undoStack.pop_back();
    VLOG(2) << "Undoing command: " << command->getDescription();
    command->undo(graph);

    redoStack.push_back(std::move(command));
}

void UndoRedoManager::redo(FactoryGraph& graph) {
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

void AddNodeCommand::execute(FactoryGraph& graph) {
    if (!executed) {
        uint64_t newNodeId = graph.addNode(nodeName, recipeKey);
        if (ed::GetCurrentEditor() != nullptr) {
            ed::SetNodePosition(IdUtils::toNodeId(newNodeId), position);
        }

        connectionData = Connection(-1, -1, -1, "");
        if (fromPort != static_cast<uint64_t>(-1)) {
            Port* fromPortPtr = graph.getPort(fromPort);
            if (fromPortPtr) {
                bool fromInput = graph.isInputPort(fromPortPtr->id);
                Node* newNodePtr = graph.getNode(newNodeId);
                if (newNodePtr) {
                    const auto& ports_on_new_node = fromInput
                                                        ? newNodePtr->output_ports
                                                        : newNodePtr->input_ports;
                    for (uint64_t new_port_id : ports_on_new_node) {
                        Port* newPortPtr = graph.getPort(new_port_id);
                        if (newPortPtr && newPortPtr->active &&
                            newPortPtr->resource_key == fromPortPtr->resource_key) {
                            uint64_t source_id = fromInput ? new_port_id : fromPort;
                            uint64_t target_id = fromInput ? fromPort : new_port_id;
                            uint64_t newConnId = graph.addConnection(source_id, target_id);
                            if (newConnId != static_cast<uint64_t>(-1)) {
                                auto conn = graph.getConnection(newConnId);
                                if (conn) {
                                    connectionData = *conn;
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }
        auto node = graph.getNode(newNodeId);
        if (node) {
            nodeData = *node;
        }
        for (uint64_t port_id : nodeData.input_ports) {
            auto port = graph.getPort(port_id);
            if (port) {
                ports_data.push_back(*port);
            }
        }
        for (uint64_t port_id : nodeData.output_ports) {
            auto port = graph.getPort(port_id);
            if (port) {
                ports_data.push_back(*port);
            }
        }
        executed = true;
    } else {
        graph.restoreNode(nodeData, ports_data);
        if (ed::GetCurrentEditor() != nullptr) {
            ed::SetNodePosition(IdUtils::toNodeId(nodeData.id), position);
        }
        if (connectionData.from_port != static_cast<uint64_t>(-1) &&
            connectionData.to_port != static_cast<uint64_t>(-1)) {
            graph.restoreConnection(connectionData);
        }
    }
}

void AddNodeCommand::undo(FactoryGraph& graph) {
    if (!executed) return;
    graph.removeNode(nodeData.id);
}

void RemoveNodeCommand::execute(FactoryGraph& graph) {
    if (!executed) {
        Node* node = graph.getNode(id);
        if (!node) return;
        nodeData = *node;
        nodeName = node->name;
        if (ed::GetCurrentEditor() != nullptr) {
            position = ed::GetNodePosition(IdUtils::toNodeId(id));
        }
        ports_data.clear();
        connections_data.clear();
        std::unordered_set<uint64_t> recorded_connections;
        for (uint64_t port_id : nodeData.input_ports) {
            auto port = graph.getPort(port_id);
            if (port) {
                ports_data.push_back(*port);
            }
            for (const auto& conn : graph.getConnectionsForPort(port_id)) {
                if (conn && recorded_connections.insert(conn->id).second) {
                    connections_data.push_back(*conn);
                }
            }
        }
        for (uint64_t port_id : nodeData.output_ports) {
            auto port = graph.getPort(port_id);
            if (port) {
                ports_data.push_back(*port);
            }
            for (const auto& conn : graph.getConnectionsForPort(port_id)) {
                if (conn && recorded_connections.insert(conn->id).second) {
                    connections_data.push_back(*conn);
                }
            }
        }

        graph.removeNode(id);
        executed = true;
    } else {
        graph.removeNode(id);
    }
}

void RemoveNodeCommand::undo(FactoryGraph& graph) {
    if (!executed) return;
    graph.restoreNode(nodeData, ports_data);
    if (ed::GetCurrentEditor() != nullptr) {
        ed::SetNodePosition(IdUtils::toNodeId(nodeData.id), position);
    }

    for (const auto& conn : connections_data) {
        graph.restoreConnection(conn);
    }
}

void AddConnectionCommand::execute(FactoryGraph& graph) {
    if (!executed) {
        uint64_t connId = graph.addConnection(fromPort, toPort);
        if (connId != static_cast<uint64_t>(-1)) {
            auto conn = graph.getConnection(connId);
            if (conn) {
                connectionData = *conn;
            }
        }
        executed = true;
    } else {
        if (connectionData.from_port != static_cast<uint64_t>(-1) &&
            connectionData.to_port != static_cast<uint64_t>(-1)) {
            graph.restoreConnection(connectionData);
        }
    }
}

void AddConnectionCommand::undo(FactoryGraph& graph) {
    if (connectionData.from_port != static_cast<uint64_t>(-1) &&
        connectionData.to_port != static_cast<uint64_t>(-1)) {
        graph.removeConnection(fromPort, toPort);
    }
}

void RemoveConnectionCommand::execute(FactoryGraph& graph) {
    Connection* conn = graph.getConnection(fromPort, toPort);
    if (conn != nullptr) {
        connectionData = *conn;
        graph.removeConnection(fromPort, toPort);
    }
}

void RemoveConnectionCommand::undo(FactoryGraph& graph) {
    if (connectionData.from_port != static_cast<uint64_t>(-1) &&
        connectionData.to_port != static_cast<uint64_t>(-1)) {
        graph.restoreConnection(connectionData);
    }
}

void ChangeNodeParamCommand::execute(FactoryGraph& graph) {
    graph.setNodeParam(nodeId, paramId, newValue);
}

void ChangeNodeParamCommand::undo(FactoryGraph& graph) {
    if (oldValue.has_value()) {
        graph.setNodeParam(nodeId, paramId, *oldValue);
    } else {
        if (Node* node = graph.getNode(nodeId)) {
            node->params.erase(paramId);
            graph.evaluateNode(nodeId);
        }
    }
}

void SetPortConstraintCommand::execute(FactoryGraph& graph) {
    if (hasModeChange) {
        graph.setPortConstraint(portId, newConstraint, newMode);
    } else {
        graph.setPortConstraint(portId, newConstraint);
    }
}

void SetPortConstraintCommand::undo(FactoryGraph& graph) {
    if (hasModeChange) {
        graph.setPortConstraint(portId, oldConstraint, oldMode);
    } else {
        graph.setPortConstraint(portId, oldConstraint);
    }
}

void SetPortConstraintModeCommand::execute(FactoryGraph& graph) {
    graph.setPortConstraintMode(portId, newMode);
}

void SetPortConstraintModeCommand::undo(FactoryGraph& graph) {
    graph.setPortConstraintMode(portId, oldMode);
}

void MoveNodeCommand::execute(FactoryGraph& graph) {
    if (ed::GetCurrentEditor() != nullptr) {
        ed::SetNodePosition(IdUtils::toNodeId(nodeId), newPosition);
    }
}

void MoveNodeCommand::undo(FactoryGraph& graph) {
    if (ed::GetCurrentEditor() != nullptr) {
        ed::SetNodePosition(IdUtils::toNodeId(nodeId), oldPosition);
    }
}

void PasteCommand::execute(FactoryGraph& graph) {
    if (ed::GetCurrentEditor() != nullptr) {
        ed::ClearSelection();
    }
    if (!executed) {
        if (copy_buffer.isEmpty()) return;

        ImVec2 mousePos = ImVec2(0, 0);
        if (ed::GetCurrentEditor() != nullptr && ImGui::GetCurrentContext() != nullptr) {
            mousePos = ed::ScreenToCanvas(ImGui::GetMousePos());
        }
        ImVec2 originalCenter = ImVec2(0, 0);

        for (const auto& pair : copy_buffer.nodePositions) {
            originalCenter.x += pair.second.x;
            originalCenter.y += pair.second.y;
        }

        ImVec2 offset = ImVec2(0, 0);
        if (!copy_buffer.nodePositions.empty()) {
            originalCenter.x /= copy_buffer.nodePositions.size();
            originalCenter.y /= copy_buffer.nodePositions.size();
            offset = ImVec2(mousePos.x - originalCenter.x, mousePos.y - originalCenter.y);
        }

        std::unordered_map<uint64_t, uint64_t> nodeIdMap;

        for (const auto& pair : copy_buffer.nodes) {
            uint64_t oldId = pair.first;
            const auto& node = pair.second;
            uint64_t newId = graph.addNode(node.name, node.selected_recipe_key);
            auto posIt = copy_buffer.nodePositions.find(oldId);
            ImVec2 oldPos = (posIt != copy_buffer.nodePositions.end()) ? posIt->second : ImVec2(0, 0);
            ImVec2 newPos = ImVec2(oldPos.x + offset.x, oldPos.y + offset.y);

            auto newNodePtr = graph.getNode(newId);
            if (newNodePtr) {
                if (!node.machine_key.empty()) {
                    graph.changeMachine(newId, node.machine_key);
                }
                for (const auto& [paramId, paramVal] : node.params) {
                    graph.setNodeParam(newId, paramId, paramVal);
                }

                newNodePtr = graph.getNode(newId);
                pastedNodes.push_back(*newNodePtr);
                pastedNodePositions[newId] = newPos;

                if (ed::GetCurrentEditor() != nullptr) {
                    ed::SetNodePosition(IdUtils::toNodeId(newId), newPos);
                    ed::SelectNode(IdUtils::toNodeId(newId), true);
                }
                nodeIdMap[oldId] = newId;
            }
        }

        std::unordered_map<uint64_t, uint64_t> oldToNewPortMap;
        for (const auto& pair : nodeIdMap) {
            uint64_t oldNodeId = pair.first;
            uint64_t newNodeId = pair.second;

            const auto& oldNode = copy_buffer.nodes.at(oldNodeId);
            auto newNode = graph.getNode(newNodeId);

            if (newNode) {
                auto mapPortList = [&](const std::vector<uint64_t>& oldList,
                                       const std::vector<uint64_t>& newList) {
                    for (size_t i = 0; i < oldList.size(); ++i) {
                        uint64_t oldPortId = oldList[i];
                        auto oldPortIt = copy_buffer.ports.find(oldPortId);
                        Port* matchedNewPort = nullptr;
                        if (oldPortIt != copy_buffer.ports.end() && !oldPortIt->second.slot_key.empty()) {
                            matchedNewPort = graph.getPortBySlot(newNodeId, oldPortIt->second.slot_key);
                        }
                        if (!matchedNewPort && i < newList.size()) {
                            matchedNewPort = graph.getPort(newList[i]);
                        }
                        if (matchedNewPort) {
                            oldToNewPortMap[oldPortId] = matchedNewPort->id;
                            if (oldPortIt != copy_buffer.ports.end()) {
                                matchedNewPort->user_constraint = oldPortIt->second.user_constraint;
                                matchedNewPort->constraint_mode = oldPortIt->second.constraint_mode;
                            }
                        }
                    }
                };

                mapPortList(oldNode.input_ports, newNode->input_ports);
                mapPortList(oldNode.output_ports, newNode->output_ports);

                for (uint64_t newPortId : newNode->input_ports) {
                    if (Port* newPort = graph.getPort(newPortId)) {
                        pastedPorts.insert({newNodeId, *newPort});
                    }
                }
                for (uint64_t newPortId : newNode->output_ports) {
                    if (Port* newPort = graph.getPort(newPortId)) {
                        pastedPorts.insert({newNodeId, *newPort});
                    }
                }
            }
        }

        for (const auto& conn : copy_buffer.connections) {
            uint64_t fromPort = conn.from_port;
            uint64_t toPort = conn.to_port;

            bool fromIsCopied = oldToNewPortMap.find(fromPort) != oldToNewPortMap.end();
            bool toIsCopied = oldToNewPortMap.find(toPort) != oldToNewPortMap.end();

            if (fromIsCopied && toIsCopied) {
                uint64_t newFromPort = oldToNewPortMap[fromPort];
                uint64_t newToPort = oldToNewPortMap[toPort];
                uint64_t newConnId = graph.addConnection(newFromPort, newToPort);
                if (newConnId != static_cast<uint64_t>(-1)) {
                    if (auto conn_ptr = graph.getConnection(newConnId)) {
                        pastedConnections.push_back(*conn_ptr);
                    }
                }
            }

            if (mapExternalConnections) {
                if (fromIsCopied && !toIsCopied) {
                    uint64_t newFromPort = oldToNewPortMap[fromPort];
                    uint64_t originalToPort = toPort;
                    uint64_t newConnId = graph.addConnection(newFromPort, originalToPort);
                    if (newConnId != static_cast<uint64_t>(-1)) {
                        if (auto conn_ptr = graph.getConnection(newConnId)) {
                            pastedConnections.push_back(*conn_ptr);
                        }
                    }
                } else if (!fromIsCopied && toIsCopied) {
                    uint64_t originalFromPort = fromPort;
                    uint64_t newToPort = oldToNewPortMap[toPort];
                    uint64_t newConnId = graph.addConnection(originalFromPort, newToPort);
                    if (newConnId != static_cast<uint64_t>(-1)) {
                        if (auto conn_ptr = graph.getConnection(newConnId)) {
                            pastedConnections.push_back(*conn_ptr);
                        }
                    }
                }
            }
        }
        executed = true;
    } else {
        for (const auto& node : pastedNodes) {
            std::vector<Port> ports;
            auto range = pastedPorts.equal_range(node.id);
            for (auto it = range.first; it != range.second; ++it) {
                ports.push_back(it->second);
            }

            graph.restoreNode(node, ports);
            if (ed::GetCurrentEditor() != nullptr) {
                auto pos_it = pastedNodePositions.find(node.id);
                if (pos_it != pastedNodePositions.end()) {
                    ed::SetNodePosition(IdUtils::toNodeId(node.id), pos_it->second);
                }
            }
        }
        for (const auto& conn : pastedConnections) {
            graph.restoreConnection(conn);
        }
    }
}

void PasteCommand::undo(FactoryGraph& graph) {
    for (const auto& node : pastedNodes) {
        graph.removeNode(node.id);
    }
}

namespace {

void snapshotNodeState(FactoryGraph& graph, uint64_t nodeId,
                       Node& outNode,
                       std::vector<Port>& outPorts,
                       std::vector<Connection>& outConnections) {
    Node* node = graph.getNode(nodeId);
    if (!node) return;
    outNode = *node;
    outPorts.clear();
    outConnections.clear();

    std::unordered_set<uint64_t> recordedConns;
    auto collectPort = [&](uint64_t pid) {
        if (Port* p = graph.getPort(pid)) {
            outPorts.push_back(*p);
        }
        for (Connection* c : graph.getConnectionsForPort(pid)) {
            if (c && recordedConns.insert(c->id).second) {
                outConnections.push_back(*c);
            }
        }
    };

    for (uint64_t pid : outNode.input_ports) collectPort(pid);
    for (uint64_t pid : outNode.output_ports) collectPort(pid);
}

void restoreNodeSnapshot(FactoryGraph& graph, uint64_t nodeId,
                         const Node& beforeNode,
                         const std::vector<Port>& beforePorts,
                         const std::vector<Connection>& beforeConnections) {
    Node* node = graph.getNode(nodeId);
    if (!node) return;

    std::unordered_set<uint64_t> beforePortIds;
    for (const auto& bp : beforePorts) {
        beforePortIds.insert(bp.id);
    }

    std::vector<uint64_t> currentPorts;
    currentPorts.insert(currentPorts.end(), node->input_ports.begin(), node->input_ports.end());
    currentPorts.insert(currentPorts.end(), node->output_ports.begin(), node->output_ports.end());

    for (uint64_t pid : currentPorts) {
        if (beforePortIds.find(pid) == beforePortIds.end()) {
            graph.removePort(pid);
        }
    }

    node = graph.getNode(nodeId);
    if (!node) return;

    node->machine_key = beforeNode.machine_key;
    node->selected_recipe_key = beforeNode.selected_recipe_key;
    node->params = beforeNode.params;
    node->unresolved_params = beforeNode.unresolved_params;
    node->invalid_param_raw = beforeNode.invalid_param_raw;
    node->is_unresolved = beforeNode.is_unresolved;
    node->unresolved_json = beforeNode.unresolved_json;
    node->input_ports = beforeNode.input_ports;
    node->output_ports = beforeNode.output_ports;

    for (const auto& bp : beforePorts) {
        if (Port* existing = graph.getPort(bp.id)) {
            *existing = bp;
        } else {
            graph.addPort(bp.resource_key, bp.node_id, bp.slot_key, bp.id);
            if (Port* restored = graph.getPort(bp.id)) {
                *restored = bp;
            }
        }
    }

    graph.evaluateNode(nodeId);

    for (const auto& bc : beforeConnections) {
        if (!graph.getConnection(bc.id)) {
            graph.restoreConnection(bc);
        }
    }
    graph.updateWireStates();
}

} // namespace

void ChangeMachineCommand::execute(FactoryGraph& graph) {
    if (!m_executed) {
        Node* node = graph.getNode(nodeId);
        if (!node) return;

        snapshotNodeState(graph, nodeId, m_beforeNode, m_beforePorts, m_beforeConnections);

        std::unordered_set<uint64_t> beforePortIds;
        for (const auto& bp : m_beforePorts) beforePortIds.insert(bp.id);

        if (!graph.changeMachine(nodeId, newMachine)) {
            return;
        }

        node = graph.getNode(nodeId);
        if (node) {
            auto recordNewPorts = [&](const std::vector<uint64_t>& portIds) {
                for (uint64_t pid : portIds) {
                    if (beforePortIds.find(pid) == beforePortIds.end()) {
                        if (const Port* p = graph.getPort(pid)) {
                            if (!p->slot_key.empty()) {
                                m_addedPortIds[p->slot_key] = p->id;
                            }
                        }
                    }
                }
            };
            recordNewPorts(node->input_ports);
            recordNewPorts(node->output_ports);
        }

        m_executed = true;
    } else {
        graph.changeMachine(nodeId, newMachine, m_addedPortIds);
    }
}

void ChangeMachineCommand::undo(FactoryGraph& graph) {
    if (!m_executed) return;
    restoreNodeSnapshot(graph, nodeId, m_beforeNode, m_beforePorts, m_beforeConnections);
}

void ChangeRecipeCommand::execute(FactoryGraph& graph) {
    if (!m_executed) {
        Node* node = graph.getNode(nodeId);
        if (!node) return;

        snapshotNodeState(graph, nodeId, m_beforeNode, m_beforePorts, m_beforeConnections);

        if (!graph.setNodeRecipe(nodeId, newRecipe)) {
            return;
        }

        node = graph.getNode(nodeId);
        if (node) {
            auto recordPorts = [&](const std::vector<uint64_t>& portIds) {
                for (uint64_t pid : portIds) {
                    if (const Port* p = graph.getPort(pid)) {
                        if (!p->slot_key.empty()) {
                            m_addedPortIds[p->slot_key] = p->id;
                        }
                    }
                }
            };
            recordPorts(node->input_ports);
            recordPorts(node->output_ports);
        }

        m_executed = true;
    } else {
        graph.setNodeRecipe(nodeId, newRecipe, m_addedPortIds);
    }
}

void ChangeRecipeCommand::undo(FactoryGraph& graph) {
    if (!m_executed) return;
    restoreNodeSnapshot(graph, nodeId, m_beforeNode, m_beforePorts, m_beforeConnections);
}

void SetGlobalParamCommand::execute(FactoryGraph& graph) {
    graph.setGlobalParam(paramId, newValue);
}

void SetGlobalParamCommand::undo(FactoryGraph& graph) {
    if (oldValue.has_value()) {
        graph.setGlobalParam(paramId, *oldValue);
    } else {
        graph.clearGlobalParam(paramId);
    }
}

void SetRateUnitCommand::execute(FactoryGraph& graph) {
    graph.setRateUnit(newUnit);
}

void SetRateUnitCommand::undo(FactoryGraph& graph) {
    graph.setRateUnit(oldUnit);
}
