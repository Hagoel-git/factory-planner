#ifndef GAMEDATAMANAGER_H
#define GAMEDATAMANAGER_H

#include <filesystem>
#include <string>
#include <vector>

#include "core/data/GameData.h"

class GameDataManager {
public:
    GameDataManager();

    bool loadFromFile(const std::filesystem::path& path, std::string& outError, bool ignoreErrors = false);

    std::vector<std::string> validate(const GameData& gd);

    const GameData& current() const;
    GameData& current();

    const gamedata::LoadReport& lastReport() const { return m_lastReport; }

    void clear();

private:
    GameData m_data;
    gamedata::LoadReport m_lastReport;
};

#endif // GAMEDATAMANAGER_H
