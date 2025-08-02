//
// Created by hagoel on 8/2/25.
//

#ifndef RECIPE_H
#define RECIPE_H
#include <string>
#include <unordered_map>
#include <vector>

struct Recipe {
    int id; // Unique identifier for the recipe
    std::string name; // Name of the recipe
    std::unordered_map<int, double> ingredients; // resource_id -> amount required
    std::unordered_map<int, double> products; // resource_id -> amount produced
    double time; // Time taken to complete the recipe in seconds
    std::vector<int> compatible_machines; // List of machine types that can use this recipe
};

#endif //RECIPE_H
