//
// Created by hagoel on 8/22/25.
//

#include "GameDataManager.h"

#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <iostream>

#include <../../../external/nlohmann/json.hpp>

using json = nlohmann::json;

GameDataManager::GameDataManager() {
    // initialize an empty GameData with the "nothing" resource
    createNew("New Game", "seconds");
}

bool GameDataManager::loadFromFile(const std::string &path, std::string &outError) {
    std::lock_guard<std::mutex> lk(_mutex);
    try {
        if (!std::filesystem::exists(path)) {
            outError = "File does not exist: " + path;
            return false;
        }
        std::ifstream ifs(path);
        if (!ifs) {
            outError = "Failed to open file: " + path;
            return false;
        }
        json j;
        ifs >> j;
        _data.gameDataFilePath = path;
        _data = jsonToGameData(j, outError);
        if (!outError.empty()) {
            // jsonToGameData will put validation messages into outError when parse problems occur
            return false;
        }
        // successful load
        notifyChange();
        return true;
    } catch (const std::exception &ex) {
        outError = std::string("Exception while loading: ") + ex.what();
        return false;
    }
}

bool GameDataManager::saveToFile(const std::string &path, std::string &outError) {
    std::lock_guard<std::mutex> lk(_mutex);
    try {
        json j = gameDataToJson(_data);
        std::ofstream ofs(path);
        if (!ofs) {
            outError = "Failed to open file for write: " + path;
            return false;
        }
        ofs << j.dump(2);
        return true;
    } catch (const std::exception &ex) {
        outError = std::string("Exception while saving: ") + ex.what();
        return false;
    }
}

GameData GameDataManager::createNew(const std::string &gameName, const std::string &timeUnit) {
    std::lock_guard<std::mutex> lk(_mutex);
    GameData gd;
    gd.gameName = gameName;
    gd.gameDataFilePath = "";
    gd.time_unit = timeUnit;
    gd.schema_version = 1;
    // ensure "nothing" resource at id 0
    Resource nothing;
    nothing.id = 0;
    nothing.key_name = "nothing";
    nothing.name = "Nothing";
    gd.resources.push_back(nothing);
    gd.resourceKeyToId["nothing"] = 0;
    _data = gd;
    notifyChange();
    return _data;
}

GameData GameDataManager::duplicateConfig(const GameData &source, const std::string &newName) {
    std::lock_guard<std::mutex> lk(_mutex);
    GameData copy = source;
    copy.gameName = newName;
    // ensure maps are rebuilt
    rebuildMaps(copy);
    _data = copy;
    notifyChange();
    return _data;
}

// Access current in-memory config (thread-unsafe reference; protect yourself if using multi-threaded)
GameData& GameDataManager::current() {
    return _data;
}

int GameDataManager::addResource(const Resource &r) {
    std::lock_guard<std::mutex> lk(_mutex);
    // don't allow adding key "nothing"
    if (r.key_name == "nothing") return 0;
    int id = nextResourceId();
    Resource rr = r;
    rr.id = id;
    _data.resources.push_back(rr);
    _data.resourceKeyToId[rr.key_name] = id;
    notifyChange();
    return id;
}

bool GameDataManager::editResource(int id, const Resource &r, std::string &outError) {
    std::lock_guard<std::mutex> lk(_mutex);
    if (id == 0) {
        outError = "Cannot edit reserved resource 'nothing' (id 0).";
        return false;
    }
    auto it = std::find_if(_data.resources.begin(), _data.resources.end(), [&](const Resource &res){ return res.id == id; });
    if (it == _data.resources.end()) {
        outError = "Resource id not found.";
        return false;
    }
    // Check for key_name conflict
    if (r.key_name != it->key_name && _data.resourceKeyToId.count(r.key_name)) {
        outError = "Another resource already uses key_name '" + r.key_name + "'.";
        return false;
    }
    // update map
    _data.resourceKeyToId.erase(it->key_name);
    it->key_name = r.key_name;
    it->name = r.name;
    _data.resourceKeyToId[it->key_name] = it->id;
    notifyChange();
    return true;
}

bool GameDataManager::deleteResource(int id, std::string &outError) {
    std::lock_guard<std::mutex> lk(_mutex);
    if (id == 0) {
        outError = "Cannot delete reserved resource 'nothing' (id 0).";
        return false;
    }
    auto it = std::find_if(_data.resources.begin(), _data.resources.end(), [&](const Resource &res){ return res.id == id; });
    if (it == _data.resources.end()) {
        outError = "Resource id not found.";
        return false;
    }
    // Ensure no recipes reference this resource
    for (const auto &r : _data.recipes) {
        for (const auto &p : r.input_ports) if (p.resource_id == id) {
            outError = "Resource is referenced by recipe '" + r.name + "'";
            return false;
        }
        for (const auto &p : r.output_ports) if (p.resource_id == id) {
            outError = "Resource is referenced by recipe '" + r.name + "'";
            return false;
        }
    }
    // remove
    _data.resourceKeyToId.erase(it->key_name);
    _data.resources.erase(it);
    notifyChange();
    return true;
}

int GameDataManager::addMachine(const Machine &m) {
    std::lock_guard<std::mutex> lk(_mutex);
    int id = nextMachineId();
    Machine mm = m;
    mm.id = id;
    _data.machines.push_back(mm);
    _data.machineKeyToId[mm.key_name] = id;
    notifyChange();
    return id;
}

bool GameDataManager::editMachine(int id, const Machine &m, std::string &outError) {
    std::lock_guard<std::mutex> lk(_mutex);
    auto it = std::find_if(_data.machines.begin(), _data.machines.end(), [&](const Machine &mm){ return mm.id == id; });
    if (it == _data.machines.end()) { outError = "Machine id not found."; return false; }
    if (m.key_name != it->key_name && _data.machineKeyToId.count(m.key_name)) { outError = "Another machine uses that key_name."; return false; }
    _data.machineKeyToId.erase(it->key_name);
    it->key_name = m.key_name;
    it->name = m.name;
    it->base_power_usage = m.base_power_usage;
    it->efficiency = m.efficiency <= 0.0 ? 1.0 : m.efficiency;
    it->category_id = m.category_id;
    _data.machineKeyToId[it->key_name] = it->id;
    notifyChange();
    return true;
}

bool GameDataManager::deleteMachine(int id, std::string &outError) {
    std::lock_guard<std::mutex> lk(_mutex);
    auto it = std::find_if(_data.machines.begin(), _data.machines.end(), [&](const Machine &mm){ return mm.id == id; });
    if (it == _data.machines.end()) { outError = "Machine id not found."; return false; }
    // Optionally check references (e.g., saved default assignments). We skip checks here.
    _data.machineKeyToId.erase(it->key_name);
    _data.machines.erase(it);
    notifyChange();
    return true;
}

int GameDataManager::addRecipe(const Recipe &r) {
    std::lock_guard<std::mutex> lk(_mutex);
    int id = nextRecipeId();
    Recipe rr = r;
    rr.id = id;
    // if input empty, ensure nothing input
    if (rr.input_ports.empty()) {
        rr.input_ports.push_back(RecipePort{0.0, 0});
    }
    _data.recipes.push_back(rr);
    if (!rr.key_name.empty()) _data.recipeKeyToId[rr.key_name] = rr.id;
    notifyChange();
    return id;
}

bool GameDataManager::editRecipe(int id, const Recipe &r, std::string &outError) {
    std::lock_guard<std::mutex> lk(_mutex);
    auto it = std::find_if(_data.recipes.begin(), _data.recipes.end(), [&](const Recipe &rr){ return rr.id == id; });
    if (it == _data.recipes.end()) { outError = "Recipe id not found."; return false; }
    if (r.key_name != it->key_name && _data.recipeKeyToId.count(r.key_name)) { outError = "Another recipe uses that key_name."; return false; }
    // update maps
    _data.recipeKeyToId.erase(it->key_name);
    *it = r;
    if (it->input_ports.empty()) it->input_ports.push_back(RecipePort{0.0, 0});
    if (!it->key_name.empty()) _data.recipeKeyToId[it->key_name] = it->id;
    notifyChange();
    return true;
}

bool GameDataManager::deleteRecipe(int id, std::string &outError) {
    std::lock_guard<std::mutex> lk(_mutex);
    auto it = std::find_if(_data.recipes.begin(), _data.recipes.end(), [&](const Recipe &rr){ return rr.id == id; });
    if (it == _data.recipes.end()) { outError = "Recipe id not found."; return false; }
    _data.recipeKeyToId.erase(it->key_name);
    _data.recipes.erase(it);
    notifyChange();
    return true;
}

std::vector<std::string> GameDataManager::validate(const GameData &gd) {
    std::vector<std::string> messages;
    // check nothing resource exists and has id 0
    bool foundNothing = false;
    for (const auto &res : gd.resources) {
        if (res.id == 0) {
            foundNothing = true;
            if (res.key_name != "nothing") messages.push_back("Resource id 0 should use key_name 'nothing'.");
            break;
        }
    }
    if (!foundNothing) {
        messages.push_back("Missing reserved resource with id 0 ('nothing').");
    }
    // unique key_name checks
    std::unordered_map<std::string,int> seen;
    for (const auto &res : gd.resources) {
        if (res.key_name.empty()) { messages.push_back("Resource with empty key_name: '" + res.name + "'."); continue; }
        if (seen.count(res.key_name)) messages.push_back("Duplicate resource key_name: '" + res.key_name + "'.");
        seen[res.key_name] = 1;
    }
    seen.clear();
    for (const auto &m : gd.machines) {
        if (m.key_name.empty()) messages.push_back("Machine with empty key_name: '" + m.name + "'.");
        if (!m.key_name.empty() && seen.count(m.key_name)) messages.push_back("Duplicate machine key_name: '" + m.key_name + "'.");
        seen[m.key_name] = 1;
        if (m.base_power_usage < 0.0) messages.push_back("Machine '" + m.name + "' has negative base_power_usage.");
        if (m.efficiency <= 0.0) messages.push_back("Machine '" + m.name + "' has non-positive efficiency.");
    }
    seen.clear();
    for (const auto &r : gd.recipes) {
        if (r.key_name.empty()) messages.push_back("Recipe with empty key_name: '" + r.name + "'.");
        if (!r.key_name.empty() && seen.count(r.key_name)) messages.push_back("Duplicate recipe key_name: '" + r.key_name + "'.");
        seen[r.key_name] = 1;
        if (r.time_seconds < 0.0) messages.push_back("Recipe '" + r.name + "' has negative time.");
        for (const auto &p : r.input_ports) {
            if (p.amount < 0.0) messages.push_back("Recipe '" + r.name + "' has negative ingredient amount.");
            if (std::none_of(gd.resources.begin(), gd.resources.end(), [&](const Resource &res){ return res.id == p.resource_id; })) {
                messages.push_back("Recipe '" + r.name + "' references unknown input resource id " + std::to_string(p.resource_id));
            }
        }
        for (const auto &p : r.output_ports) {
            if (p.amount <= 0.0) messages.push_back("Recipe '" + r.name + "' has non-positive product amount.");
            if (std::none_of(gd.resources.begin(), gd.resources.end(), [&](const Resource &res){ return res.id == p.resource_id; })) {
                messages.push_back("Recipe '" + r.name + "' references unknown output resource id " + std::to_string(p.resource_id));
            }
        }
    }
    return messages;
}

void GameDataManager::setChangeCallback(std::function<void()> cb) {
    std::lock_guard<std::mutex> lk(_mutex);
    _onChange = std::move(cb);
}

static double convertTimeToSeconds(double timeValue, const std::string &unit) {
    if (unit == "seconds") return timeValue;
    if (unit == "minutes") return timeValue / 60.0;
    if (unit == "hours") return timeValue / 3600.0;
    return timeValue; // fallback
}

void GameDataManager::notifyChange() {
    if (_onChange) {
        // call without holding a lock to avoid deadlocks in callback
        auto cb = _onChange;
        // unlock happens as lock_guard goes out of scope in callers; here just call
        cb();
    }
}

GameData GameDataManager::jsonToGameData(const json &j, std::string &outError) {
    outError.clear();
    std::filesystem::path path = _data.gameDataFilePath;
    GameData gd;
    try {
        gd.gameName = j.value("gameName", std::string("unknown"));
        gd.time_unit = j.value("time_unit", std::string("seconds"));
        gd.gameDataFilePath = path;
        // Ensure nothing resource at id 0
        Resource nothing{0, "nothing", "Nothing"};
        gd.resources.push_back(nothing);
        gd.resourceKeyToId["nothing"] = 0;
        int nextResourceId = 1;
        // Items (resources). Accepts arrays named "items", "resources", and "fluids" (fluids appended)
        auto handleResourceArray = [&](const json &arr) {
            if (!arr.is_array()) return;
            for (const auto &rj : arr) {
                std::string name = rj.value("name", std::string());
                std::string key = rj.value("key_name", std::string());
                if (key.empty()) {
                    // skip or create key from name
                    continue;
                }
                // avoid duplicating "nothing"
                if (key == "nothing") continue;
                Resource res;
                res.id = nextResourceId++;
                res.key_name = key;
                res.name = name.empty() ? key : name;
                gd.resources.push_back(res);
                gd.resourceKeyToId[res.key_name] = res.id;
            }
        };
        if (j.contains("items")) handleResourceArray(j["items"]);
        if (j.contains("resources")) handleResourceArray(j["resources"]);
        if (j.contains("fluids")) handleResourceArray(j["fluids"]);


        // Categories
        int nextCategoryId = 0;
        if (j.contains("categories") && j["categories"].is_array()) {
            for (const auto &cj : j["categories"]) {
                std::string key = cj.value("key_name", std::string());
                std::string name = cj.value("name", key);
                if (key.empty()) continue;
                Category cat{nextCategoryId++, key, name};
                gd.categories.push_back(cat);
                gd.categoryKeyToId[key] = cat.id;
            }
        }


        // Machines
        int nextMachineId = 0;
        if (j.contains("machines") && j["machines"].is_array()) {
            for (const auto &mj : j["machines"]) {
                std::string name = mj.value("name", std::string());
                std::string key = mj.value("key_name", std::string());
                std::string catKey = mj.value("category", std::string());
                double power = mj.value("base_power_usage", 0.0);
                double eff = mj.value("efficiency", 1.0);
                int catId = -1;
                if (!catKey.empty()) {
                    if (gd.categoryKeyToId.count(catKey)) catId = gd.categoryKeyToId[catKey];
                    else {
                        // create category on the fly
                        int id = nextCategoryId++;
                        Category c{id, catKey, catKey};
                        gd.categories.push_back(c);
                        gd.categoryKeyToId[catKey] = id;
                        catId = id;
                    }
                }
                Machine mm;
                mm.id = nextMachineId++;
                mm.key_name = key;
                mm.name = name.empty() ? key : name;
                mm.category_id = catId;
                mm.base_power_usage = power;
                mm.efficiency = eff <= 0.0 ? 1.0 : eff;
                gd.machines.push_back(mm);
                if (!mm.key_name.empty()) gd.machineKeyToId[mm.key_name] = mm.id;
            }
        }


        // Recipes
        int nextRecipeId = 0;
        if (j.contains("recipes") && j["recipes"].is_array()) {
            for (const auto &rj : j["recipes"]) {
                Recipe r;
                r.id = nextRecipeId++;
                r.key_name = rj.value("key_name", std::string());
                r.name = rj.value("name", r.key_name);
                std::string catKey = rj.value("category", std::string());
                if (!catKey.empty()) {
                    if (!gd.categoryKeyToId.count(catKey)) {
                        int id = nextCategoryId++;
                        Category c{id, catKey, catKey};
                        gd.categories.push_back(c);
                        gd.categoryKeyToId[catKey] = id;
                    }
                    r.category_id = gd.categoryKeyToId[catKey];
                }
                double timeVal = rj.value("time", 0.0);
                r.time_seconds = convertTimeToSeconds(timeVal, gd.time_unit);


                // ingredients
                if (rj.contains("ingredients") && rj["ingredients"].is_array()) {
                    const auto &ing = rj["ingredients"];
                    if (ing.empty()) {
                        // inject nothing
                        r.input_ports.push_back(RecipePort{0.0, 0});
                    } else {
                        for (const auto &pair : ing) {
                            if (!pair.is_array() || pair.size() < 2) continue;
                            std::string key = pair[0].get<std::string>();
                            double amount = pair[1].get<double>();
                            if (!gd.resourceKeyToId.count(key)) {
                                // auto-create resource
                                Resource res;
                                res.id = nextResourceId++;
                                res.key_name = key;
                                res.name = key;
                                gd.resources.push_back(res);
                                gd.resourceKeyToId[key] = res.id;
                            }
                            int resId = gd.resourceKeyToId[key];
                            r.input_ports.push_back(RecipePort{amount, resId});
                        }
                    }
                } else {
                    // treat missing field as nothing
                    r.input_ports.push_back(RecipePort{0.0, 0});
                }


                // products
                if (rj.contains("products") && rj["products"].is_array()) {
                    for (const auto &pair : rj["products"]) {
                        if (!pair.is_array() || pair.size() < 2) continue;
                        std::string key = pair[0].get<std::string>();
                        double amount = pair[1].get<double>();
                        if (!gd.resourceKeyToId.count(key)) {
                            Resource res;
                            res.id = nextResourceId++;
                            res.key_name = key;
                            res.name = key;
                            gd.resources.push_back(res);
                            gd.resourceKeyToId[key] = res.id;
                        }
                        int resId = gd.resourceKeyToId[key];
                        r.output_ports.push_back(RecipePort{amount, resId});
                    }
                }


                if (!r.key_name.empty()) gd.recipeKeyToId[r.key_name] = r.id;
                gd.recipes.push_back(std::move(r));
            }
        }


        // rebuild maps for safety
        rebuildMaps(gd);
        // perform light validation; if critical issues found, add to outError (but do not fail on warnings)
        auto errors = validate(gd);
        if (!errors.empty()) {
            // aggregate as single string (caller may still accept config but wants to see warnings)
            std::ostringstream oss;
            for (const auto &m : errors) oss << m << "\n";
            outError = oss.str();
            // We choose to still return the parsed GD; caller may inspect messages.
        }
        return gd;
    } catch (const std::exception &ex) {
        outError = std::string("Exception while parsing JSON: ") + ex.what();
        return {};
    }
}

json GameDataManager::gameDataToJson(const GameData &gd) {
    json j;
    j["gameName"] = gd.gameName;
    j["time_unit"] = gd.time_unit;
    // resources: write all except the reserved "nothing" with id 0
    json items = json::array();
    for (const auto &res : gd.resources) {
        if (res.id == 0) continue; // skip internal 'nothing'
        json rj;
        rj["name"] = res.name;
        rj["key_name"] = res.key_name;
        items.push_back(rj);
    }
    if (!items.empty()) j["items"] = items;


    // categories
    if (!gd.categories.empty()) {
        json carr = json::array();
        for (const auto &c : gd.categories) {
            json cj;
            cj["name"] = c.name;
            cj["key_name"] = c.key_name;
            carr.push_back(cj);
        }
        j["categories"] = carr;
    }


    // machines
    if (!gd.machines.empty()) {
        json marr = json::array();
        for (const auto &m : gd.machines) {
            json mj;
            mj["name"] = m.name;
            mj["key_name"] = m.key_name;
            if (m.category_id != -1) {
                // lookup key
                std::string catKey = "";
                auto it = std::find_if(gd.categories.begin(), gd.categories.end(), [&](const Category &c){ return c.id == m.category_id; });
                if (it != gd.categories.end()) catKey = it->key_name;
                if (!catKey.empty()) mj["category"] = catKey;
            }
            mj["base_power_usage"] = m.base_power_usage;
            mj["efficiency"] = m.efficiency;
            marr.push_back(mj);
        }
        j["machines"] = marr;
    }


    // recipes
    if (!gd.recipes.empty()) {
        json rarr = json::array();
        for (const auto &r : gd.recipes) {
            json rj;
            rj["name"] = r.name;
            rj["key_name"] = r.key_name;
            if (r.category_id != -1) {
                auto it = std::find_if(gd.categories.begin(), gd.categories.end(), [&](const Category &c){ return c.id == r.category_id; });
                if (it != gd.categories.end()) rj["category"] = it->key_name;
            }
            // convert time_seconds back to unit used by this GameData
            double tVal = r.time_seconds;
            if (gd.time_unit == "minutes") tVal = r.time_seconds / 60.0;
            else if (gd.time_unit == "hours") tVal = r.time_seconds / 3600.0;
            rj["time"] = tVal;


            // ingredients
            json ing = json::array();
            for (const auto &p : r.input_ports) {
                // if the input is the reserved nothing with amount 0, write empty array to be consistent with original style
                if (p.resource_id == 0 && p.amount == 0.0) continue;
                // lookup resource key
                auto it = std::find_if(gd.resources.begin(), gd.resources.end(), [&](const Resource &res){ return res.id == p.resource_id; });
                std::string key = it == gd.resources.end() ? std::to_string(p.resource_id) : it->key_name;
                json pair = json::array({ key, p.amount });
                ing.push_back(pair);
            }
            // if ing remains empty, write [] to indicate mining/no input
            rj["ingredients"] = ing;


            // products
            json prod = json::array();
            for (const auto &p : r.output_ports) {
                auto it = std::find_if(gd.resources.begin(), gd.resources.end(), [&](const Resource &res){ return res.id == p.resource_id; });
                std::string key = it == gd.resources.end() ? std::to_string(p.resource_id) : it->key_name;
                json pair = json::array({ key, p.amount });
                prod.push_back(pair);
            }
            rj["products"] = prod;


            rarr.push_back(rj);
        }
        j["recipes"] = rarr;
    }


    return j;
}

void GameDataManager::rebuildMaps(GameData &gd) {
    gd.resourceKeyToId.clear();
    for (const auto &r : gd.resources) gd.resourceKeyToId[r.key_name] = r.id;
    gd.categoryKeyToId.clear();
    for (const auto &c : gd.categories) gd.categoryKeyToId[c.key_name] = c.id;
    gd.machineKeyToId.clear();
    for (const auto &m : gd.machines) gd.machineKeyToId[m.key_name] = m.id;
    gd.recipeKeyToId.clear();
    for (const auto &r : gd.recipes) gd.recipeKeyToId[r.key_name] = r.id;
}


int GameDataManager::nextResourceId() const {
    int maxId = 0;
    for (const auto &r : _data.resources) if (r.id > maxId) maxId = r.id;
    return maxId + 1;
}


int GameDataManager::nextMachineId() const {
    int maxId = 0;
    for (const auto &m : _data.machines) if (m.id > maxId) maxId = m.id;
    return maxId + 1;
}


int GameDataManager::nextRecipeId() const {
    int maxId = 0;
    for (const auto &r : _data.recipes) if (r.id > maxId) maxId = r.id;
    return maxId + 1;
}