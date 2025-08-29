#ifndef GAMEDATAMANAGER_H
#define GAMEDATAMANAGER_H

#include <string>
#include <vector>
#include <functional>
#include <mutex>

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

    // Create new blank config
    GameData createNew(const std::string &gameName, const std::string &timeUnit="seconds");

    // CRUD operations
    bool addResource(const Resource &r);
    bool editResource(std::string key_name, const Resource &r, std::string &outError);
    bool deleteResource(std::string key_name, std::string &err);

    bool addMachine(const Machine &m);
    bool editMachine(std::string key_name, const Machine &m, std::string &err);
    bool deleteMachine(std::string key_name, std::string &err);

    bool addRecipe(const Recipe &r);
    bool editRecipe(std::string key_name, const Recipe &r, std::string &err);
    bool deleteRecipe(std::string key_name, std::string &err);

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
private:
    GameData _data;
    std::function<void()> _onChange;
    std::mutex _mutex; // thread-safety if used from UI thread background tasks
};


#endif //GAMEDATAMANAGER_H
