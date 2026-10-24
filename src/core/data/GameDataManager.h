#ifndef GAMEDATAMANAGER_H
#define GAMEDATAMANAGER_H

#include <string>
#include <vector>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "GameData.h"

class GameDataManager {
public:
    GameDataManager();

    // Load from file path (reads JSON)
    bool loadFromFile(const std::filesystem::path &path, std::string &outError);

    // Validation - returns vector of human readable errors/warnings
    std::vector<std::string> validate(const GameData &gd);

    const GameData &current() const;
    GameData &current();

    void clear();

private:
    GameData m_data;

    GameData jsonToGameData(const nlohmann::json &j, std::filesystem::path& packageIconRoot, std::string &outError);
};

#endif //GAMEDATAMANAGER_H
