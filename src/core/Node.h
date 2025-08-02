//
// Created by hagoel on 8/1/25.
//

#ifndef NODE_H
#define NODE_H
#include <string>
#include <unordered_map>

#include "../common/NodeType.h"

struct Node {
    std::string name;
    NodeType type;

    int id;
    int key_id;             // machine type (Constructor, Assembler, etc.)
    int selected_recipe_id; // -1 if not selected

    double machine_count;
    double power_usage = 0.0; // in MW

    std::unordered_map<int, double> resource_input;  // resource_id -> amount
    std::unordered_map<int, double> resource_output; // resource_id -> amount

    Node() = default;
    Node(std::string name, NodeType type, int id, int key_id)
        : name(std::move(name)), type(type), id(id), key_id(key_id), selected_recipe_id(-1) {}

};

#endif //NODE_H
