#ifndef UNDOREDOSYSTEM_H
#define UNDOREDOSYSTEM_H
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <imgui.h>

#include "common/CopyBuffer.h"
#include "core/data/GameDataV4.h"

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
    explicit CompositeCommand(std::string desc, CommandFlags flags = {true, true})
        : description(std::move(desc)), flags(flags) {}
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
    AddNodeCommand(std::string name, std::string recipeKey, uint64_t fromPort, const ImVec2& pos)
        : nodeName(std::move(name)),
          recipeKey(std::move(recipeKey)),
          fromPort(fromPort),
          position(pos),
          nodeData(),
          connectionData(-1, -1, -1, ""),
          executed(false) {
    }
    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, true}; }
    std::string getDescription() const override { return "Add Node: " + nodeName; }
};

class RemoveNodeCommand : public Command {
private:
    uint64_t id;
    std::string nodeName;

    Node nodeData;
    std::vector<Port> ports_data;
    std::vector<Connection> connections_data;
    ImVec2 position;
    bool executed;
public:
    explicit RemoveNodeCommand(uint64_t id)
        : id(id), nodeName(), nodeData(), position(0, 0), executed(false) {
    }
    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, true}; }
    std::string getDescription() const override {
        return "Remove Node: " + (!nodeName.empty() ? nodeName : nodeData.name);
    }
};

class AddConnectionCommand : public Command {
private:
    uint64_t fromPort;
    uint64_t toPort;

    Connection connectionData;
    bool executed;
public:
    AddConnectionCommand(uint64_t fromPort, uint64_t toPort)
        : fromPort(fromPort), toPort(toPort), connectionData(-1, -1, -1, ""), executed(false) {
    }
    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, false}; }
    std::string getDescription() const override {
        return "Add Connection: " + std::to_string(fromPort) + " -> " + std::to_string(toPort);
    }
};

class RemoveConnectionCommand : public Command {
private:
    uint64_t fromPort;
    uint64_t toPort;

    Connection connectionData;
public:
    RemoveConnectionCommand(uint64_t fromPort, uint64_t toPort)
        : fromPort(fromPort), toPort(toPort), connectionData(-1, -1, -1, "") {
    }
    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, false}; }
    std::string getDescription() const override {
        return "Remove Connection: " + std::to_string(fromPort) + " -> " + std::to_string(toPort);
    }
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
          mapExternalConnections(map_external_connections),
          executed(false) {
    }

    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, true}; }
    std::string getDescription() const override {
        return "Pasted " + std::to_string(copy_buffer.nodes.size()) + " nodes";
    }
};

class ChangeNodeParamCommand : public Command {
private:
    uint64_t nodeId;
    std::string paramId;
    std::optional<gamedata::ParamValue> oldValue;
    gamedata::ParamValue newValue;
public:
    ChangeNodeParamCommand(uint64_t nodeId, std::string paramId,
                           std::optional<gamedata::ParamValue> oldValue,
                           gamedata::ParamValue newValue)
        : nodeId(nodeId),
          paramId(std::move(paramId)),
          oldValue(std::move(oldValue)),
          newValue(std::move(newValue)) {
    }

    ChangeNodeParamCommand(uint64_t nodeId, std::string paramId,
                           gamedata::ParamValue oldValue,
                           gamedata::ParamValue newValue)
        : nodeId(nodeId),
          paramId(std::move(paramId)),
          oldValue(std::move(oldValue)),
          newValue(std::move(newValue)) {
    }

    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, false}; }
    std::string getDescription() const override {
        return "Change Node Parameter '" + paramId + "' on Node ID " + std::to_string(nodeId);
    }
};

class SetPortConstraintCommand : public Command {
private:
    uint64_t portId;
    double oldConstraint;
    double newConstraint;
    bool hasModeChange = false;
    ConstraintMode oldMode = ConstraintMode::AUTO;
    ConstraintMode newMode = ConstraintMode::AUTO;
public:
    SetPortConstraintCommand(uint64_t portId, double oldConstraint, double newConstraint)
        : portId(portId), oldConstraint(oldConstraint), newConstraint(newConstraint) {
    }
    SetPortConstraintCommand(uint64_t portId, double oldConstraint, double newConstraint,
                             ConstraintMode oldMode, ConstraintMode newMode)
        : portId(portId),
          oldConstraint(oldConstraint),
          newConstraint(newConstraint),
          hasModeChange(true),
          oldMode(oldMode),
          newMode(newMode) {
    }
    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, false}; }
    std::string getDescription() const override {
        return "Set Port Constraint: Port ID " + std::to_string(portId) +
               " from " + std::to_string(oldConstraint) + " to " + std::to_string(newConstraint);
    }
};

class SetPortConstraintModeCommand : public Command {
private:
    uint64_t portId;
    ConstraintMode oldMode;
    ConstraintMode newMode;
public:
    SetPortConstraintModeCommand(uint64_t portId, ConstraintMode oldMode, ConstraintMode newMode)
        : portId(portId), oldMode(oldMode), newMode(newMode) {
    }
    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, false}; }
    std::string getDescription() const override {
        return "Set Port Constraint Mode: Port ID " + std::to_string(portId) + " from " +
               constraintModeToString(oldMode) + " to " + constraintModeToString(newMode);
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
        return "Change Node Position: Node ID " + std::to_string(nodeId) +
               " from (" + std::to_string(oldPosition.x) + ", " + std::to_string(oldPosition.y) +
               ") to (" + std::to_string(newPosition.x) + ", " + std::to_string(newPosition.y) + ")";
    }
};

class ChangeMachineCommand : public Command {
private:
    uint64_t nodeId;
    std::string oldMachine;
    std::string newMachine;

    Node m_beforeNode;
    std::vector<Port> m_beforePorts;
    std::vector<Connection> m_beforeConnections;
    std::unordered_map<std::string, uint64_t> m_addedPortIds;
    bool m_executed = false;
public:
    ChangeMachineCommand(uint64_t nodeId, std::string oldMachine, std::string newMachine)
        : nodeId(nodeId), oldMachine(std::move(oldMachine)), newMachine(std::move(newMachine)) {
    }

    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, false}; }
    std::string getDescription() const override {
        return "Change Machine: Node ID " + std::to_string(nodeId) +
               " from " + oldMachine + " to " + newMachine;
    }
};

class ChangeRecipeCommand : public Command {
private:
    uint64_t nodeId;
    std::string oldRecipe;
    std::string newRecipe;

    Node m_beforeNode;
    std::vector<Port> m_beforePorts;
    std::vector<Connection> m_beforeConnections;
    std::unordered_map<std::string, uint64_t> m_addedPortIds;
    bool m_executed = false;
public:
    ChangeRecipeCommand(uint64_t nodeId, std::string oldRecipe, std::string newRecipe)
        : nodeId(nodeId), oldRecipe(std::move(oldRecipe)), newRecipe(std::move(newRecipe)) {
    }

    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, true}; }
    std::string getDescription() const override {
        return "Change Recipe: Node ID " + std::to_string(nodeId) +
               " from " + oldRecipe + " to " + newRecipe;
    }
};

class SetGlobalParamCommand : public Command {
private:
    std::string paramId;
    std::optional<gamedata::ParamValue> oldValue;
    gamedata::ParamValue newValue;
public:
    SetGlobalParamCommand(std::string paramId,
                          std::optional<gamedata::ParamValue> oldValue,
                          gamedata::ParamValue newValue)
        : paramId(std::move(paramId)),
          oldValue(std::move(oldValue)),
          newValue(std::move(newValue)) {
    }

    SetGlobalParamCommand(std::string paramId,
                          gamedata::ParamValue oldValue,
                          gamedata::ParamValue newValue)
        : paramId(std::move(paramId)),
          oldValue(std::move(oldValue)),
          newValue(std::move(newValue)) {
    }

    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{true, false}; }
    std::string getDescription() const override {
        return "Set Global Parameter '" + paramId + "'";
    }
};

class SetRateUnitCommand : public Command {
private:
    gamedata::RateUnit oldUnit;
    gamedata::RateUnit newUnit;
public:
    SetRateUnitCommand(gamedata::RateUnit oldUnit, gamedata::RateUnit newUnit)
        : oldUnit(oldUnit), newUnit(newUnit) {
    }

    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    CommandFlags getFlags() const override { return CommandFlags{false, false}; }
    std::string getDescription() const override {
        return std::string("Set Rate Unit: ") + gamedata::rateUnitToString(oldUnit) +
               " -> " + gamedata::rateUnitToString(newUnit);
    }
};

class UndoRedoManager {
private:
    std::vector<std::unique_ptr<Command>> undoStack;
    std::vector<std::unique_ptr<Command>> redoStack;
    size_t maxHistorySize;
public:
    explicit UndoRedoManager(size_t maxSize = 100) : maxHistorySize(maxSize) {}

    void executeCommand(std::unique_ptr<Command> command, FactoryGraph& graph);
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }

    void undo(FactoryGraph& graph);
    void redo(FactoryGraph& graph);

    const Command* getCommandToUndo() const {
        return undoStack.empty() ? nullptr : undoStack.back().get();
    }
    const Command* getCommandToRedo() const {
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
