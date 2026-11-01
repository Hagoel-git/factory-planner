#ifndef PORT_H
#define PORT_H
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>


enum class ConstraintMode {
    AUTO,
    LIMIT,
    TARGET,
    PUSH
};

inline const char* constraintModeToString(ConstraintMode mode) {
    switch (mode) {
        case ConstraintMode::LIMIT: return "limit";
        case ConstraintMode::TARGET: return "target";
        case ConstraintMode::PUSH: return "push";
        case ConstraintMode::AUTO:
        default: return "auto";
    }
}

inline ConstraintMode constraintModeFromString(const std::string &str) {
    if (str == "limit" || str == "LIMIT") return ConstraintMode::LIMIT;
    if (str == "target" || str == "TARGET") return ConstraintMode::TARGET;
    if (str == "push" || str == "PUSH") return ConstraintMode::PUSH;
    return ConstraintMode::AUTO;
}

struct Port {
    using ConstraintMode = ::ConstraintMode;

    double rate = 0.0; // Rate of the port, e.g., how much resource it can handle per second
    double user_constraint = -1.0; // User-defined constraint for the port, -1 means no constraint
    ConstraintMode constraint_mode = ConstraintMode::AUTO;
    // Solver output (not serialized). For wired output ports: production not consumed downstream.
    // Unwired output ports and input ports always report excess_rate = 0.0.
    double excess_rate = 0.0;
    uint64_t id = 0;
    uint64_t node_id = 0;
    std::string resource_key; // ID of the resource associated with this port

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
