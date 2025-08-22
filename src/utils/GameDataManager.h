#ifndef GAMEDATAMANAGER_H
#define GAMEDATAMANAGER_H

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <filesystem>

#include "JsonLoader.h"
#include "../common/GameData.h"

class GameDataManager {
public:
    GameDataManager();
    // Load from file path (reads JSON)
    bool loadFromFile(const std::string &path, std::string &outError);

    // Save to file (writes JSON)
    bool saveToFile(const std::string &path, std::string &outError);

    // Create new blank config
    GameData createNew(const std::string &gameName, const std::string &timeUnit="seconds");

    // CRUD operations
    int addResource(const Resource &r);
    bool editResource(int id, const Resource &r, std::string &err);
    bool deleteResource(int id, std::string &err);

    int addMachine(const Machine &m);
    bool editMachine(int id, const Machine &m, std::string &err);
    bool deleteMachine(int id, std::string &err);

    int addRecipe(const Recipe &r);
    bool editRecipe(int id, const Recipe &r, std::string &err);
    bool deleteRecipe(int id, std::string &err);

    // Duplicate config (deep copy) - useful for "Duplicate" button
    GameData duplicateConfig(const GameData &source, const std::string &newName);

    // Validation - returns vector of human readable errors/warnings
    std::vector<std::string> validate(const GameData &gd);

    GameData &current();

    void clear() {
        std::lock_guard<std::mutex> lk(_mutex);
        _data = {};
        notifyChange();
    }
    // Observers (simple) - you can use signals/slots or function callbacks
    void setChangeCallback(std::function<void()> cb);
private:
    void notifyChange();

    GameData jsonToGameData(const json &j, std::string &outError);

    json gameDataToJson(const GameData &gd);

    void rebuildMaps(GameData &gd);

    int nextResourceId() const;

    int nextMachineId() const;

    int nextRecipeId() const;

    // Access current in-memory config
private:
    GameData _data;
    std::function<void()> _onChange;
    std::mutex _mutex; // thread-safety if used from UI thread background tasks
};


#endif //GAMEDATAMANAGER_H
