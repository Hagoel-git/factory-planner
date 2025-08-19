//
// Created by hagoel on 8/19/25.
//

#ifndef UNDOREDOSYSTEM_H
#define UNDOREDOSYSTEM_H
#include <imgui.h>

#include "../core/FactoryGraph.h"


class Command {
public:
    virtual ~Command() = default;
    virtual void execute(FactoryGraph& graph) = 0;
    virtual void undo(FactoryGraph& graph) = 0;
    virtual std::string getDescription() const = 0;
};

class AddNodeCommand : public Command {
private:
    std::string nodeName;
    NodeType nodeType;
    int recipeId;
    int fromPort;
    ImVec2 position;

    Node nodeData;
    std::vector<Port> ports_data;
    Connection connectionData;
    bool executed;
public:
    AddNodeCommand(const std::string& name, NodeType type, int recipeId, int fromPort, const ImVec2& pos)
        : nodeName(name), nodeType(type), recipeId(recipeId), fromPort(fromPort), position(pos), nodeData(), connectionData(), executed(false) {
    }
    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    std::string getDescription() const override { return "Add Node: " + nodeName; }
};

class RemoveNodeCommand : public Command {
private:
    int id;

    Node nodeData;
    std::vector<Port> ports_data;
    std::vector<Connection> connections_data;
    ImVec2 position;
public:
    RemoveNodeCommand(int id)
        : id(id), nodeData(), position(0, 0) {
    }
    void execute(FactoryGraph& graph) override;
    void undo(FactoryGraph& graph) override;
    std::string getDescription() const override { return "Remove Node: " + nodeData.name; }

};

class UndoRedoManager {
private:
    std::vector<std::unique_ptr<Command>> undoStack;
    std::vector<std::unique_ptr<Command>> redoStack;
    size_t maxHistorySize;
public:
    UndoRedoManager(size_t maxSize = 100) : maxHistorySize(maxSize) {};

    void executeCommand(std::unique_ptr<Command> command, FactoryGraph& graph);
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }

    void undo(FactoryGraph& graph);
    void redo(FactoryGraph& graph);


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

