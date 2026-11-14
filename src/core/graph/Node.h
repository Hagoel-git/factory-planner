#ifndef NODE_H
#define NODE_H
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include "core/data/GameDataV4.h"

struct Node {
    std::string name;

    uint64_t id = 0;
    std::string machine_key;
    std::string selected_recipe_key;

    std::unordered_map<std::string, gamedata::ParamValue> params;
    nlohmann::json unresolved_params = nlohmann::json::object();
    nlohmann::json invalid_param_raw = nlohmann::json::object();

    bool is_unresolved = false;
    nlohmann::json unresolved_json = nlohmann::json::object();

    mutable double machine_count = 0.0;
    mutable double crafts = 0.0;

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
