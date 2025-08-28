//
// Created by hagoel on 8/5/25.
//

#ifndef GAMEDATA_H
#define GAMEDATA_H

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

struct Resource {
    int id = -1; // 0 reserved for "nothing"
    std::string key_name;
    std::string name;
};


struct Machine {
    int id = -1;
    std::string key_name;
    std::string name;

    double base_crafting_speed = 1.0; // linear crafting speed multiplier. Default 1.0 means recipe time is used as-is.

    int max_clock = 100; // percentage, 100% is normal speed, 200% is double speed, etc.
};


struct RecipePort {
    double amount = 0.0;
    int resource_id = -1;
};


struct Recipe {
    int id = -1;
    std::string key_name;
    std::string name;

    double time_seconds = 0.0;

    std::vector<int> produced_in_machines_ids;

    std::vector<RecipePort> input_ports;
    std::vector<RecipePort> output_ports;

};


struct GameData {
    std::string gameName;
    std::filesystem::path gameDataFilePath;
    std::string time_unit = "seconds"; // original unit from JSON
    int schema_version = 2;


    std::vector<Resource> resources; // resource with id 0 is reserved for "nothing"
    std::vector<Machine> machines;
    std::vector<Recipe> recipes;


    // helper maps for quick lookups
    std::unordered_map<std::string,int> resourceKeyToId;
    std::unordered_map<std::string,int> machineKeyToId;
    std::unordered_map<std::string,int> recipeKeyToId;
};


#endif //GAMEDATA_H
