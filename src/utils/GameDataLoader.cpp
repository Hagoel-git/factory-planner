//
// Created by hagoel on 8/2/25.
//

#include "GameDataLoader.h"

GameDataLoader::GameData GameDataLoader::loadGameData(const std::string &jsonFile) {
    GameData data;
    json raw = JsonLoader::loadFromFile(jsonFile);

    if (!JsonLoader::validateSchema(raw)) {
        throw std::invalid_argument("Invalid JSON data");
    }

    data.gameName = raw["gameName"].get<std::string>();
    std::unordered_map<std::string, int> keyNameToId;
    std::unordered_map<std::string, int> categoryNameToId;

    int resource_id_counter = 0;
    for (const auto &itemJson: raw["items"]) {
        std::string key = itemJson.value("key_name", "");
        keyNameToId[key] = resource_id_counter;

        Resource res = JsonLoader::parseResource(itemJson, resource_id_counter);
        data.resources.push_back(res);

        ++resource_id_counter;
    }
    for (const auto &itemJson: raw["fluids"]) {
        std::string key = itemJson.value("key_name", "");
        keyNameToId[key] = resource_id_counter;

        Resource res = JsonLoader::parseResource(itemJson, resource_id_counter);
        data.resources.push_back(res);

        ++resource_id_counter;
    }

    // Parse machines
    int machineId = 0;
    for (const auto &machineJson: raw["machines"]) {
        std::string categoryName = machineJson.value("category", "");
        categoryNameToId[categoryName] = machineId++;
        Machine machine = JsonLoader::parseMachine(machineJson, machineId);
        data.machines.push_back(machine);
    }

    // Parse recipes
    int recipeId = 0;
    for (const auto &recipeJson: raw["recipes"]) {
        Recipe recipe = JsonLoader::parseRecipe(recipeJson, recipeId, keyNameToId, categoryNameToId);
        data.recipes.push_back(recipe);
        ++recipeId;
    }

    return data;
}
