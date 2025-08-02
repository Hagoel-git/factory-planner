#include "JsonLoader.h"
#include <fstream>
#include <iostream>
#include <unordered_map>

json JsonLoader::loadFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open JSON file: " + filePath);
    }

    json data;
    file >> data;
    return data;
}

bool JsonLoader::validateSchema(const json& data) {
    return data.contains("items") && data.contains("recipes");
}

Resource JsonLoader::parseResource(const json& itemJson, int id) {
    Resource res;
    res.id = id;
    res.name = itemJson.value("name", "");
    return res;
}

Recipe JsonLoader::parseRecipe(
    const json& recipeJson,
    int id,
    const std::unordered_map<std::string, int>& keyNameToId)
{
    Recipe recipe;
    recipe.id = id;
    recipe.name = recipeJson.value("name", "");
    recipe.time = recipeJson.value("time", 1.0);

    if (recipeJson.contains("ingredients")) {
        for (const auto& pair : recipeJson["ingredients"]) {
            const std::string& key = pair[0];
            double amount = pair[1];
            auto it = keyNameToId.find(key);
            if (it != keyNameToId.end()) {
                recipe.ingredients[it->second] = amount;
            } else {
                std::cerr << "Warning: Unknown ingredient key_name: " << key << std::endl;
            }
        }
    }

    if (recipeJson.contains("products")) {
        for (const auto& pair : recipeJson["products"]) {
            const std::string& key = pair[0];
            double amount = pair[1];
            auto it = keyNameToId.find(key);
            if (it != keyNameToId.end()) {
                recipe.products[it->second] = amount;
            } else {
                std::cerr << "Warning: Unknown product key_name: " << key << std::endl;
            }
        }
    }

    return recipe;
}
