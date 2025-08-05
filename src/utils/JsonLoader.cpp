#include "JsonLoader.h"
#include <fstream>
#include <iostream>
#include <unordered_map>

json JsonLoader::loadFromFile(const std::string &filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open JSON file: " + filePath);
    }

    json data;
    file >> data;
    return data;
}

bool JsonLoader::validateSchema(const json &data) {
    return data.contains("items") && data.contains("recipes");
}

Resource JsonLoader::parseResource(const json &itemJson, int id) {
    Resource res;
    res.id = id;
    res.name = itemJson.value("name", "");
    return res;
}

Machine JsonLoader::parseMachine(const json &machineJson, int id) {
    Machine machine;
    machine.id = id;
    machine.name = machineJson.value("name", "");
    machine.base_power_usage = machineJson.value("base_power_usage", 0.0);
    machine.max_somersloop_slots = machineJson.value("max_somersloop_slots", 0);
    return machine;
}

Recipe JsonLoader::parseRecipe(
    const json &recipeJson,
    int id,
    const std::unordered_map<std::string, int> &keyNameToId,
    const std::unordered_map<std::string, int> &categoryNameToId) {
    Recipe recipe;
    recipe.id = id;
    recipe.name = recipeJson.value("name", "");
    recipe.time = recipeJson.value("time", 1.0);
    try {
        recipe.category_id = categoryNameToId.at(recipeJson.value("category", ""));
    } catch (const std::out_of_range &e) {
        std::cerr << "Warning: Category not found for recipe " << recipe.name << ": " << e.what() << std::endl;
        recipe.category_id = -1; // Default to -1 if category not found
    }

    if (recipeJson.contains("ingredients")) {
        for (const auto &pair: recipeJson["ingredients"]) {
            const std::string &key = pair[0];
            double amount = pair[1];
            auto it = keyNameToId.find(key);
            if (it != keyNameToId.end()) {
                RecipePort port;
                port.resource_id = it->second;
                port.amount = amount;
                recipe.input_ports.push_back(port);
            } else {
                std::cerr << "Warning: Unknown ingredient key_name: " << key << std::endl;
            }
        }
    }

    if (recipeJson.contains("products")) {
        for (const auto &pair: recipeJson["products"]) {
            const std::string &key = pair[0];
            double amount = pair[1];
            auto it = keyNameToId.find(key);
            if (it != keyNameToId.end()) {
                RecipePort port;
                port.resource_id = it->second;
                port.amount = amount;
                recipe.output_ports.push_back(port);
            } else {
                std::cerr << "Warning: Unknown product key_name: " << key << std::endl;
            }
        }
    }

    return recipe;
}
