//
// Created by hagoel on 8/2/25.
//

#ifndef GAMEDATALOADER_H
#define GAMEDATALOADER_H
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
    };

    static GameData loadGameData(const std::string& jsonFile);
};



#endif //GAMEDATALOADER_H
