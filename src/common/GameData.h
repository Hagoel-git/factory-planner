//
// Created by hagoel on 8/5/25.
//

#ifndef GAMEDATA_H
#define GAMEDATA_H

#include <iostream>
#include <string>
#include <vector>

#include "../core/Recipe.h"
#include "../core/Resource.h"
#include "../core/Machine.h"
#include "../utils/JsonLoader.h"

struct GameData {
    std::string gameName;
    std::vector<Resource> resources;
    std::vector<Machine> machines;
    std::vector<Recipe> recipes;
    int time_unit = 1; // 0 - seconds, 1 - minutes, 2 - hours // default is minutes

    int getIdByResourceName(const std::string &name) const {
        for (const auto &resource : resources) {
            if (resource.name == name) {
                return resource.id;
            }
        }
        return -1; // Resource not found
    }
    int getIdByMachineName(const std::string &name) const {
        for (const auto &machine : machines) {
            if (machine.name == name) {
                return machine.id;
            }
        }
        std::cerr << "Machine not found" << std::endl;
        return -1; // Machine not found
    }

    int getIdByRecipeName(const std::string &name) const {
        for (const auto &recipe : recipes) {
            if (recipe.name == name) {
                return recipe.id;
            }
        }
        std::cerr << "Recipe not found" << std::endl;
        return -1; // Recipe not found
    }

    void clear() {
        gameName.clear();
        resources.clear();
        machines.clear();
        recipes.clear();
        time_unit = 0; // Reset to default (minutes)
    }

    ~GameData() = default;

    GameData(const std::string jsonFile) {
        json raw = JsonLoader::loadFromFile(jsonFile);

        if (!JsonLoader::validateSchema(raw)) {
            throw std::invalid_argument("Invalid JSON data");
        }

        gameName = raw["gameName"].get<std::string>();
        std::string t_unit = raw.value("time_unit", "minutes");
        if (t_unit == "hours") {
            time_unit = 2; // hours
        } else if (t_unit == "minutes") {
            time_unit = 1; // minutes
        } else if (t_unit == "seconds") {
            time_unit = 0; // seconds
        }
        std::unordered_map<std::string, int> keyNameToId;
        std::unordered_map<std::string, int> categoryNameToId;

        int resource_id_counter = 0;
        for (const auto &itemJson: raw["items"]) {
            std::string key = itemJson.value("key_name", "");
            keyNameToId[key] = resource_id_counter;

            Resource res = JsonLoader::parseResource(itemJson, resource_id_counter);
            resources.push_back(res);

            ++resource_id_counter;
        }
        for (const auto &itemJson: raw["fluids"]) {
            std::string key = itemJson.value("key_name", "");
            keyNameToId[key] = resource_id_counter;

            Resource res = JsonLoader::parseResource(itemJson, resource_id_counter);
            resources.push_back(res);

            ++resource_id_counter;
        }

        // Parse machines
        int machineId = 0;
        for (const auto &machineJson: raw["machines"]) {
            std::string categoryName = machineJson.value("category", "");
            categoryNameToId[categoryName] = machineId++;
            Machine machine = JsonLoader::parseMachine(machineJson, machineId);
            machines.push_back(machine);
        }

        // Parse recipes
        int recipeId = 0;
        for (const auto &recipeJson: raw["recipes"]) {
            Recipe recipe = JsonLoader::parseRecipe(recipeJson, recipeId, keyNameToId, categoryNameToId);
            recipes.push_back(recipe);
            ++recipeId;
        }
    }
};


#endif //GAMEDATA_H
