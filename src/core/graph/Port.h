#ifndef PORT_H
#define PORT_H
#include <cstdint>
#include <utility>

enum class ConstraintType {
    LIMIT,  // Hard Cap: Rate <= UserValue (Default for Inputs/Miners)
    TARGET  // Soft Goal: Rate + Excess = UserValue (Default for Outputs)
};

struct Port {
    std::string resource_key; // ID of the resource associated with this port
    double rate = 0.0; // Rate of the port, e.g., how much resource it can handle per second
    double user_constraint = -1.0; // User-defined constraint for the port, -1 means no constraint
    double excess_rate = 0.0; // Excess amount for target constraints
    uint64_t id;
    uint64_t node_id;
    ConstraintType constraint_type;

    Port() = default;

    Port(uint64_t id, uint64_t node_id, std::string resource_key, ConstraintType constraint_type = ConstraintType::LIMIT)
        : resource_key(std::move(resource_key)), id(id), node_id(node_id), constraint_type(constraint_type) {
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
