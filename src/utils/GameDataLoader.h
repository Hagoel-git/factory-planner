//
// Created by hagoel on 8/2/25.
//

#ifndef GAMEDATALOADER_H
#define GAMEDATALOADER_H
#include <iostream>
#include <string>
#include <vector>

#include "../core/Recipe.h"
#include "../core/Resource.h"
#include "JsonLoader.h"


class GameDataLoader {
public:
    struct GameData {
        std::string gameName;
        std::vector<Resource> resources;
        std::vector<Recipe> recipes;

        int getIdByResourceName(const std::string &name) const {
            for (const auto &resource : resources) {
                if (resource.name == name) {
                    return resource.id;
                }
            }
            return -1; // Resource not found
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
    };

    static GameData loadGameData(const std::string &jsonFile);
};


#endif //GAMEDATALOADER_H
