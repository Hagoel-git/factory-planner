//
// Created by hagoel on 8/1/25.
//

#ifndef NODE_H
#define NODE_H
#include <string>
#include <unordered_map>

#include "Recipe.h"
#include "../common/NodeType.h"

struct Node {
    std::string name;
    NodeType type;

    int id;
    int key_id;             // machine type (Constructor, Assembler, etc.)
    int selected_recipe_id;

    double machine_count = 0.0;;
    double power_usage = 0.0; // in MW

    std::vector<double> input_rates;
    std::vector<double> output_rates;

    std::vector<double> required_input_rates;
    std::vector<double> required_output_rates;

    Node() = default;
    Node(std::string name, NodeType type, int id, int key_id)
        : name(std::move(name)), type(type), id(id), key_id(key_id) {}

    void print() const {
        std::cout << "Node ID: " << id << ", Name: " << name << ", Type: " << static_cast<int>(type)
                  << ", Key ID: " << key_id << ", Recipe ID: " << selected_recipe_id
                  << ", Machine Count: " << machine_count << ", Power Usage: " << power_usage << " MW" << std::endl;
        // Print input rates
        std::cout << "Input Rates: ";
        for (const auto& rate : input_rates) {
            std::cout << rate << " ";
        }
        std::cout << std::endl;
        // Print output rates
        std::cout << "Output Rates: ";
        for (const auto& rate : output_rates) {
            std::cout << rate << " ";
        }
        std::cout << std::endl;
        // Print required input rates
        std::cout << "Required Input Rates: ";
        for (const auto& rate : required_input_rates) {
            std::cout << rate << " ";
        }
        std::cout << std::endl;
        // Print required output rates
        std::cout << "Required Output Rates: ";
        for (const auto& rate : required_output_rates) {
            std::cout << rate << " ";
        }
        std::cout << std::endl;
    }

};

#endif //NODE_H
