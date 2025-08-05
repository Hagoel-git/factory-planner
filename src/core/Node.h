//
// Created by hagoel on 8/1/25.
//

#ifndef NODE_H
#define NODE_H
#include <string>
#include <iostream>
#include <unordered_map>

#include "Recipe.h"
#include "../common/NodeType.h"

struct Node {
    std::string name;
    NodeType type;

    int id;
    int machine_id = -1;
    int selected_recipe_id = -1;

    mutable double machine_count = 0.0;;
    mutable double power_usage = 0.0; // in MW

    std::vector<int> input_ports;
    std::vector<int> output_ports;

    Node() = default;

    Node(std::string name, NodeType type, int id)
        : name(std::move(name)), type(type), id(id) {
    }
};

#endif //NODE_H
