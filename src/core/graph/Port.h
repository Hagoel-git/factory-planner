#ifndef PORT_H
#define PORT_H
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>


struct Port {
    double rate = 0.0; // Rate of the port, e.g., how much resource it can handle per second
    double user_constraint = -1.0; // User-defined constraint for the port, -1 means no constraint
    // Solver output (not serialized). For output ports with connections: production not consumed downstream.
    // Otherwise: amount by which the rate overshoots its target constraint.
    double excess_rate = 0.0;
    uint64_t id = 0;
    uint64_t node_id = 0;
    std::string resource_key; // ID of the resource associated with this port
    //bool isInput;

    Port() = default;

    Port(uint64_t id, uint64_t node_id, std::string resource_key)
        : id(id), node_id(node_id), resource_key(std::move(resource_key)) {
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
