#ifndef NODE_H
#define NODE_H
#include <string>
#include <vector>

enum class NodeType {
    PRODUCER,
    PROCESSOR,
    CONSUMER
};

inline const char *toString(NodeType type) {
    switch (type) {
        case NodeType::PRODUCER: return "Producer";
        case NodeType::PROCESSOR: return "Processor";
        case NodeType::CONSUMER: return "Consumer";
        default: return "Unknown";
    }
}

struct Node {
    std::string name;
    NodeType type;

    int id;
    std::string machine_key;
    std::string selected_recipe_key;

    mutable double machine_count = 0.0;
    mutable double clock_speed = 100.0; // percentage, 100% is normal speed, 200% is double speed, etc.
    mutable double production_multiplier = 100.0; // percentage, 100% is normal output, 200% is double output, etc.

    std::vector<int> input_ports;
    std::vector<int> output_ports;

    Node() = default;

    Node(std::string name, int id)
        : name(std::move(name)), type(NodeType::PROCESSOR), id(id) {
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
