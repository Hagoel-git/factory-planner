#ifndef PORT_H
#define PORT_H
#include <cstdint>


struct Port {
    double rate = 0.0; // Rate of the port, e.g., how much resource it can handle per second
    double user_constraint = -1.0; // User-defined constraint for the port, -1 means no constraint
    uint64_t id;
    uint64_t node_id;
    std::string resource_key; // ID of the resource associated with this port
    bool isInput;

    Port() = default;

    Port(uint64_t id, uint64_t node_id, std::string resource_key, bool isInput)
        : id(id), node_id(node_id), resource_key(resource_key), isInput(isInput) {
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
            return std::hash<uint64_t>{}(p.id);
        }
    };
}
#endif //PORT_H
