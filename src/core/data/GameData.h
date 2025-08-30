#ifndef GAMEDATA_H
#define GAMEDATA_H

#include <string>
#include <vector>
#include <filesystem>
#include <map>

struct Resource {
    std::string name;
};


struct Machine {
    std::string name;
    double base_crafting_speed = 1.0; // linear crafting speed multiplier. Default 1.0 means recipe time is used as-is.
};


struct RecipePort {
    double amount = 0.0;
    std::string resource_key;
};


struct Recipe {
    std::string name;
    double time_seconds = 0.0;

    std::vector<std::string> produced_in_machines_keys;
    std::vector<RecipePort> input_ports;
    std::vector<RecipePort> output_ports;

};


struct GameData {
    std::string gameName;
    std::filesystem::path gameDataFilePath;
    std::string time_unit = "seconds"; // original unit from JSON
    int schema_version = 2;

    // Using maps for easy lookup by key_name
    std::map<std::string, Resource> resources;
    std::map<std::string, Machine> machines;
    std::map<std::string, Recipe> recipes;
};


#endif //GAMEDATA_H
