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
    bool editResource(const std::string& current_key, const Resource &r, std::string &outError);
    bool deleteResource(const std::string& key_name, std::string &err);
    bool setResourceIcon(const std::string& key, const std::filesystem::path& sourcePath, std::string &outError);

    bool addMachine(const Machine &m);
    bool editMachine(const std::string& current_key, const Machine &m, std::string &err);
    bool deleteMachine(const std::string& key_name, std::string &err);
    bool setMachineIcon(const std::string& key, const std::filesystem::path& sourcePath, std::string &outError);

    bool addRecipe(const Recipe &r);
    bool editRecipe(const std::string& current_key, const Recipe &r, std::string &err);
    bool deleteRecipe(const std::string& key_name, std::string &err);

    // Validation - returns vector of human readable errors/warnings
    std::vector<std::string> validate(const GameData &gd);

    bool duplicateDataFile(const std::filesystem::path& sourcePath, std::string& outError);
    bool renameDataFile(const std::string& newName, std::string& outError);
    bool moveDataFile(const std::string& targetGameName, std::string& outError);
    bool deleteDataFile(const std::filesystem::path& path, std::string& outError);

    GameData &current();

    void clear();
private:
    GameData m_data;

    GameData jsonToGameData(const json &j, std::filesystem::path& packageIconRoot, std::string &outError);
    json gameDataToJson(const GameData &gd);

    void registerAlias(const std::string& prefix, const std::string& oldId, const std::string& newId);
    bool performRenameResource(const std::string& oldKey, const std::string& newKey);
    bool performRenameMachine(const std::string& oldKey, const std::string& newKey);
    bool performRenameRecipe(const std::string& oldKey, const std::string& newKey);

    bool renameIconFile(const std::string& category, const std::string& oldKey, const std::string& newKey);
    bool copyIconFile(const std::string& category, const std::string& key, const std::filesystem::path& sourcePath);
};


#endif //GAMEDATAMANAGER_H
