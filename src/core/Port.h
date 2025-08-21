//
// Created by hagoel on 8/4/25.
//

#ifndef PORT_H
#define PORT_H

struct Port {
    double rate = 0.0; // Rate of the port, e.g., how much resource it can handle per second
    double user_constraint = -1.0; // User-defined constraint for the port, -1 means no constraint
    int id;
    int node_id;
    int resource_id; // ID of the resource associated with this port
    bool isInput;

    Port() = default;

    Port(int id, int node_id, int resource_id, bool isInput)
        : id(id), node_id(node_id), resource_id(resource_id), isInput(isInput) {
    }

    friend bool operator==(const Port &lhs, const Port &rhs) {
        return lhs.id == rhs.id;
    }

    friend bool operator!=(const Port &lhs, const Port &rhs) {
        return !(lhs == rhs);
    }
};
namespace std {
    template <>
    struct hash<Port> {
        std::size_t operator()(const Port& p) const noexcept {
            return std::hash<int>{}(p.id);
        }
    };
}
#endif //PORT_H
