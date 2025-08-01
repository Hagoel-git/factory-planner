//
// Created by hagoel on 8/1/25.
//

#ifndef NODE_H
#define NODE_H
#include <string>

#include "../common/NodeType.h"

struct Node {
    std::string name;
    NodeType type;

    int id;
    int key_id;
    int selected_recipe_id; // if node is processor; -1 if not

    double power_usage = 0.0; // in MW

    Node() = default;
    Node(std::string name, NodeType type, int id, int key_id)
        : name(std::move(name)), type(type), id(id), key_id(key_id), selected_recipe_id(-1) {}

};

#endif //NODE_H
