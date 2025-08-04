//
// Created by hagoel on 8/2/25.
//

#ifndef RECIPE_H
#define RECIPE_H
#include <string>
#include <vector>

struct RecipePort {
    double amount; // Amount of the resource required or produced
    int resource_id; // ID of the resource
};

struct Recipe {
    std::vector<RecipePort> input_ports;
    std::vector<RecipePort> output_ports;
    std::string name; // Name of the recipe
    double time; // Time taken to complete the recipe in seconds
    int id; // Unique identifier for the recipe

    int getInputPortCount() const {
        return static_cast<int>(input_ports.size());
    }

    int getOutputPortCount() const {
        return static_cast<int>(output_ports.size());
    }

    int getInputPortResourceId(int port) const {
        return port < input_ports.size() ? input_ports[port].resource_id : -1;
    }

    int getOutputPortResourceId(int port) const {
        return port < output_ports.size() ? output_ports[port].resource_id : -1;
    }
};

#endif //RECIPE_H
