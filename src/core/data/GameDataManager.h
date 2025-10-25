#ifndef GAMEDATAMANAGER_H
#define GAMEDATAMANAGER_H

#include <string>
#include <vector>

#include "GameData.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

class GameDataManager {
public:
    GameDataManager();
    // Load from file path (reads JSON)
    bool loadFromFile(const std::string &path, std::string &outError);

    // Save to file (writes JSON)
    bool saveToFile(const std::string &path, std::string &outError);

    GameData createNew(const std::string &gameName, const std::string &timeUnit="seconds");

    // CRUD operations
    bool addResource(const Resource &r);
    bool editResource(const std::string& key_name, const Resource &r, std::string &outError);
    bool deleteResource(const std::string& key_name, std::string &err);

    bool addMachine(const Machine &m);
    bool editMachine(const std::string& key_name, const Machine &m, std::string &err);
    bool deleteMachine(const std::string& key_name, std::string &err);

    bool addRecipe(const Recipe &r);
    bool editRecipe(const std::string& key_name, const Recipe &r, std::string &err);
    bool deleteRecipe(const std::string& key_name, std::string &err);

    // Validation - returns vector of human readable errors/warnings
    std::vector<std::string> validate(const GameData &gd);

    GameData &current();

    void clear() {
        _data = {};
    }
private:
    GameData jsonToGameData(const json &j, std::filesystem::path& packageIconRoot, std::string &outError);
    json gameDataToJson(const GameData &gd);
    GameData _data;
};


#endif //GAMEDATAMANAGER_H
