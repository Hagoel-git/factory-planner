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

    friend bool operator==(const Node &lhs, const Node &rhs) {
        return lhs.id == rhs.id;
    }

    friend bool operator!=(const Node &lhs, const Node &rhs) {
        return !(lhs == rhs);
    }
};

namespace std {
    template <>
    struct hash<Node> {
        std::size_t operator()(const Node& n) const noexcept {
            return std::hash<int>{}(n.id);
        }
    };
}

#endif //NODE_H
