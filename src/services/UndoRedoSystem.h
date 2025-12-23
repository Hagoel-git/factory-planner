#ifndef UNDOREDOSYSTEM_H
#define UNDOREDOSYSTEM_H
#include <imgui.h>

#include "common/CopyBuffer.h"
class FactoryGraph;

struct CommandFlags {
    bool needsSolve;
    bool needsRebuild;
};

class Command {
public:
    virtual ~Command() = default;
    virtual void execute(FactoryGraph& graph) = 0;
    virtual void undo(FactoryGraph& graph) = 0;
    virtual CommandFlags getFlags() const { return CommandFlags{false, false}; }
    virtual std::string getDescription() const = 0;
};

class CompositeCommand : public Command {
private:
    std::vector<std::unique_ptr<Command>> commands;
    std::string description;
    CommandFlags flags;
public:
    explicit CompositeCommand(std::string desc, CommandFlags flags = {true, true}) : description(std::move(desc)), flags(flags) {}
    void addCommand(std::unique_ptr<Command> command) {
        commands.push_back(std::move(command));
    }
    void execute(FactoryGraph& graph) override {
        for (const auto& cmd : commands) {
            cmd->execute(graph);
        }
    }
    void undo(FactoryGraph& graph) override {
        for (auto it = commands.rbegin(); it != commands.rend(); ++it) {
            (*it)->undo(graph);
        }
    }
    CommandFlags getFlags() const override { return flags; }
    std::string getDescription() const override {
        return description;
    }
    bool isEmpty() const {
        return commands.empty();
    }
};

class AddNodeCommand : public Command {
private:
    std::string nodeName;
    std::string recipeKey;
    uint64_t fromPort;
    ImVec2 position;

    Node nodeData;
    std::vector<Port> ports_data;
    Connection connectionData;
    bool executed;
public:
    AddNodeCommand(std::string  name, std::string recipeKey, uint64_t fromPort, const ImVec2& pos)
        : nodeName(std::move(name)), recipeKey(std::move(recipeKey)), fromPort(fromPort), position(pos), nodeData(), connectionData(), executed(false) {
    }
    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, true}; }
    std::string getDescription() const override { return "Add Node: " + nodeName; }
};

class RemoveNodeCommand : public Command {
private:
    uint64_t id;

    Node nodeData;
    std::vector<Port> ports_data;
    std::vector<Connection> connections_data;
    ImVec2 position;
public:
    explicit RemoveNodeCommand(uint64_t id)
        : id(id), nodeData(), position(0, 0) {
    }
    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, true}; }
    std::string getDescription() const override { return "Remove Node: " + nodeData.name; }

};

class AddConnectionCommand : public Command {
private:
    uint64_t fromPort;
    uint64_t toPort;

    Connection connectionData;
    bool executed;
public:
    AddConnectionCommand(uint64_t fromPort, uint64_t toPort)
        : fromPort(fromPort), toPort(toPort), connectionData(), executed(false) {
    }
    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, false}; }
    std::string getDescription() const override { return "Add Connection: " + std::to_string(fromPort) + " -> " + std::to_string(toPort); }
};

class RemoveConnectionCommand : public Command {
private:
    uint64_t fromPort;
    uint64_t toPort;

    Connection connectionData;
    public:
    RemoveConnectionCommand(uint64_t fromPort, uint64_t toPort)
        : fromPort(fromPort), toPort(toPort), connectionData() {
    }
    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, false}; }
    std::string getDescription() const override { return "Remove Connection: " + std::to_string(fromPort) + " -> " + std::to_string(toPort); }
};

class PasteCommand : public Command {
private:
    CopyBuffer copy_buffer;
    bool mapExternalConnections;

    std::vector<Node> pastedNodes;
    std::unordered_multimap<uint64_t, Port> pastedPorts;
    std::vector<Connection> pastedConnections;
    std::unordered_map<uint64_t, ImVec2> pastedNodePositions;
    bool executed;
public:
    PasteCommand(CopyBuffer copy_buffer, bool map_external_connections)
        : copy_buffer(std::move(copy_buffer)),
          mapExternalConnections(map_external_connections), executed(false) {
    }

    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, true}; }
    std::string getDescription() const override { return "Pasted " + std::to_string(copy_buffer.nodes.size()) + " nodes";}
};

class ChangeClockSpeedCommand : public Command {
private:
    uint64_t nodeId;
    double oldSpeed;
    double newSpeed;
public:
    ChangeClockSpeedCommand(uint64_t nodeId, double oldSpeed, double newSpeed)
        : nodeId(nodeId), oldSpeed(oldSpeed), newSpeed(newSpeed) {
    }

    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, false}; }
    std::string getDescription() const override {
        return "Change Clock Speed: Node ID " + std::to_string(nodeId) + " from " + std::to_string(oldSpeed) + " to " + std::to_string(newSpeed);
    }
};

class ChangeProductionMultiplierCommand : public Command {
private:
    uint64_t nodeId;
    double oldMultiplier;
    double newMultiplier;
public:
    ChangeProductionMultiplierCommand(uint64_t nodeId, double oldMultiplier, double newMultiplier)
        : nodeId(nodeId), oldMultiplier(oldMultiplier), newMultiplier(newMultiplier) {
    }
    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, false}; }
    std::string getDescription() const override {
        return "Change Production Multiplier: Node ID " + std::to_string(nodeId) + " from " + std::to_string(oldMultiplier) + " to " + std::to_string(newMultiplier);
    }
};

class SetPortConstraintCommand : public Command {
private:
    uint64_t portId;
    double oldConstraint;
    double newConstraint;
public:
    SetPortConstraintCommand(uint64_t portId, double oldConstraint, double newConstraint)
        : portId(portId), oldConstraint(oldConstraint), newConstraint(newConstraint) {
    }
    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, false}; }
    std::string getDescription() const override {
        return "Set Port Constraint: Port ID " + std::to_string(portId) + " from " + std::to_string(oldConstraint) + " to " + std::to_string(newConstraint);
    }
};

class MoveNodeCommand : public Command {
private:
    ImVec2 oldPosition;
    ImVec2 newPosition;
    uint64_t nodeId;
public:
    MoveNodeCommand(uint64_t nodeId, const ImVec2& oldPosition, const ImVec2& newPosition)
        : oldPosition(oldPosition), newPosition(newPosition), nodeId(nodeId) {
    }
    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{false, true}; }
    std::string getDescription() const override {
        return "Change Node Position: Node ID " + std::to_string(nodeId) + " from (" + std::to_string(oldPosition.x) + ", " + std::to_string(oldPosition.y) + ") to (" + std::to_string(newPosition.x) + ", " + std::to_string(newPosition.y) + ")";
    }
};

class ChangeMachineCommand : public Command {
private:
    uint64_t nodeId;
    std::string oldMachine;
    std::string newMachine;
public:
    ChangeMachineCommand(uint64_t nodeId, std::string oldMachine, std::string newMachine)
        : nodeId(nodeId), oldMachine(std::move(oldMachine)), newMachine(std::move(newMachine)) {
    }

    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, false}; }
    std::string getDescription() const override {
        return "Change Machine: Node ID " + std::to_string(nodeId) + " from " + oldMachine + " to " + newMachine;
    }
};

class UndoRedoManager {
private:
    std::vector<std::unique_ptr<Command>> undoStack;
    std::vector<std::unique_ptr<Command>> redoStack;
    size_t maxHistorySize;
public:
    explicit UndoRedoManager(size_t maxSize = 100) : maxHistorySize(maxSize) {};

    void executeCommand(std::unique_ptr<Command> command, FactoryGraph& graph);
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }

    void undo(FactoryGraph& graph);
    void redo(FactoryGraph& graph);

    const Command *getCommandToUndo() const {
        return undoStack.empty() ? nullptr : undoStack.back().get();
    }
    const Command *getCommandToRedo() const {
        return redoStack.empty() ? nullptr : redoStack.back().get();
    }

    std::string getUndoDescription() const {
        return undoStack.empty() ? "" : undoStack.back()->getDescription();
    }

    std::string getRedoDescription() const {
        return redoStack.empty() ? "" : redoStack.back()->getDescription();
    }

    size_t getUndoStackSize() const { return undoStack.size(); }
    size_t getRedoStackSize() const { return redoStack.size(); }


private:
    void trimUndoStack();
};

#endif //UNDOREDOSYSTEM_H

