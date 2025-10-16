#ifndef NODE_H
#define NODE_H
#include <string>
#include <vector>
#include <cstdint>

struct Node {
    std::string name;

    uint64_t id;
    std::string machine_key;
    std::string selected_recipe_key;

    mutable double machine_count = 0.0;
    mutable double clock_speed = 100.0; // percentage, 100% is normal speed, 200% is double speed, etc.
    mutable double production_multiplier = 100.0; // percentage, 100% is normal output, 200% is double output, etc.

    std::vector<uint64_t> input_ports;
    std::vector<uint64_t> output_ports;

    Node() = default;

    Node(std::string name, uint64_t id)
        : name(std::move(name)), id(id) {
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
            return std::hash<uint64_t>{}(n.id);
        }
    };
}

#endif //NODE_H
