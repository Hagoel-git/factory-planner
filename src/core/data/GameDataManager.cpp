#include "GameDataManager.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <absl/log/log.h>
#include <GL/gl.h>

#include <nlohmann/json.hpp>

#include "services/SettingsManager.h"
#include "common/StringUtils.h"
#include "common/TextureUtils.h"

using json = nlohmann::json;

GameDataManager::GameDataManager() {
    // initialize an empty GameData with the "nothing" resource
    DLOG(INFO) << "Initializing GameDataManager with default data.";
    createNew("New Game", "seconds");
}

bool GameDataManager::loadFromFile(const std::string &path, std::string &outError) {
    try {
        if (!std::filesystem::exists(path)) {
            LOG(ERROR) << "File does not exist: " << path;
            outError = "File does not exist: " + path;
            return false;
        }
        std::ifstream ifs(path);
        if (!ifs) {
            LOG(ERROR) << "Failed to open file: " << path;
            outError = "Failed to open file: " + path;
            return false;
        }
        json j;
        ifs >> j;

        std::filesystem::path dataFilePath(path);
        std::filesystem::path packageRoot = dataFilePath.parent_path().parent_path(); // structure: <package_root>/game_datas/<datafile>.gd
        std::filesystem::path iconRoot = packageRoot / "icons";

        _data.gameDataFilePath = path;
        _data = jsonToGameData(j, iconRoot, outError);

        if (!outError.empty()) {
            LOG(ERROR) << "Validation errors while loading game data from file: " << outError;
            return false;
        }
        LOG(INFO) << "GameData loaded successfully from file: " << path;
        return true;
    } catch (const std::exception &ex) {
        LOG(ERROR) << "Exception while loading: " << ex.what();
        return false;
    }
}

bool GameDataManager::saveToFile(const std::string &path, std::string &outError) {
    try {
        json j = gameDataToJson(_data);
        std::ofstream ofs(path);
        if (!ofs) {
            LOG(ERROR) << "Failed to open file for write: " << path;
            outError = "Failed to open file for write: " + path;
            return false;
        }
        ofs << j.dump(2);
        LOG(INFO) << "GameData saved successfully to file: " << path;
        return true;
    } catch (const std::exception &ex) {
        LOG(ERROR) << "Exception while saving: " << ex.what();
        outError = std::string("Exception while saving: ") + ex.what();
        return false;
    }
}

GameData GameDataManager::createNew(const std::string &gameName, const std::string &timeUnit) {
    DLOG(INFO) << "Creating new GameData: " << gameName << " with time unit: " << timeUnit;
    GameData gd;
    gd.gameName = gameName;
    gd.gameDataFilePath = "";
    gd.time_unit = timeUnit;
    gd.schema_version = 2;
    Resource nothing;
    nothing.name = "Nothing";
    gd.resources["nothing"] = nothing;
    _data = gd;
    return _data;
}

// Access current in-memory config (thread-unsafe reference; protect yourself if using multithreaded)
GameData& GameDataManager::current() {
    return _data;
}

bool GameDataManager::addResource(const Resource &r) {
    std::string key = slugify(r.name);
    // don't allow adding key "nothing"
    if (key == "nothing") {
        LOG(WARNING) << "Cannot add reserved resource 'nothing'.";
        return false;
    }
    if (_data.resources.count(key)) {
        LOG(WARNING) << "Resource with key '" << key << "' already exists.";
        return false;
    }// already exists
    _data.resources[key] = r;
    DLOG(INFO) << "Resource added successfully: " << key;
    return true;
}

bool GameDataManager::editResource(const std::string& key_name, const Resource &r, std::string &outError) {
    if (key_name == "nothing") {
        LOG(WARNING) << "Cannot edit reserved resource 'nothing'.";
        outError = "Cannot edit reserved resource 'nothing'.";
        return false;
    }
    if (!_data.resources.count(key_name)) {
        LOG(WARNING) << "Resource key_name not found: " << key_name;
        outError = "Resource key_name not found.";
        return false;
    }
    std::string key = slugify(r.name);
    if (_data.resources.count(key) && key != key_name) {
        LOG(WARNING) << "Cannot change to that name; another resource uses it: " << key;
        outError = "Cannot change to that name; another resource uses it.";
    }
    // update
    _data.resources.erase(key_name);
    _data.resources[key] = r;
    // update any recipes that referenced the old key_name
    for (auto &recPair : _data.recipes) {
        Recipe &rec = recPair.second;
        for (auto &p : rec.input_ports) {
            if (p.resource_key == key_name) p.resource_key = key;
        }
        for (auto &p : rec.output_ports) {
            if (p.resource_key == key_name) p.resource_key = key;
        }
    }
    DLOG(INFO) << "Resource edited successfully: " << key;
    return true;
}

bool GameDataManager::deleteResource(const std::string& key_name, std::string &outError) {
    if (key_name == "nothing") {
        LOG(WARNING) << "Cannot delete reserved resource 'nothing'.";
        outError = "Cannot delete reserved resource 'nothing'.";
        return false;
    }
    if (!_data.resources.count(key_name)) {
        LOG(WARNING) << "Resource key_name not found: " << key_name;
        outError = "Resource key_name not found.";
        return false;
    }
    // check references in recipes
    for (const auto &recPair : _data.recipes) {
        const Recipe &rec = recPair.second;
        for (const auto &p : rec.input_ports) {
            if (p.resource_key == key_name) {
                LOG(WARNING) << "Resource is used in a recipe input: " << _data.recipes.find(rec.name)->first << "; cannot delete.";
                outError = "Resource is used in a recipe input (" + _data.recipes.find(rec.name)->first + "); cannot delete.";
                return false;
            }
        }
        for (const auto &p : rec.output_ports) {
            if (p.resource_key == key_name) {
                LOG(WARNING) << "Resource is used in a recipe output: " << _data.recipes.find(rec.name)->first << "; cannot delete.";
                outError = "Resource is used in a recipe output (" + _data.recipes.find(rec.name)->first + "); cannot delete.";
                return false;
            }
        }
    }
    _data.resources.erase(key_name);
    DLOG(INFO) << "Resource deleted successfully: " << key_name;
    return true;
}

bool GameDataManager::addMachine(const Machine &m) {
    std::string key = slugify(m.name);
    if (_data.machines.count(key)) {
        LOG(WARNING) << "Machine with key '" << key << "' already exists.";
        return false;
    } // already exists
    _data.machines[key] = m;
    DLOG(INFO) << "Machine added successfully: " << key;
    return true;
}

bool GameDataManager::editMachine(const std::string& key_name, const Machine &m, std::string &outError) {
    if (!_data.machines.count(key_name)) {
        LOG(WARNING) << "Machine key_name not found: " << key_name;
        outError = "Machine key_name not found.";
        return false;
    }
    std::string key = slugify(m.name);
    if (_data.machines.count(key) && key != key_name) {
        LOG(WARNING) << "Cannot change to that name; another machine uses it: " << key;
        outError = "Cannot change to that name; another machine uses it.";
    }
    // update
    _data.machines.erase(key_name);
    _data.machines[key] = m;
    // update any recipes that referenced the old key_name
    for (auto &recPair : _data.recipes) {
        Recipe &rec = recPair.second;
        for (auto &mk : rec.produced_in_machines_keys) {
            if (mk == key_name) mk = key;
        }
    }
    DLOG(INFO) << "Machine edited successfully: " << key;
    return true;
}

bool GameDataManager::deleteMachine(const std::string& key_name, std::string &outError) {
    if (!_data.machines.count(key_name)) {
        LOG(WARNING) << "Machine key_name not found: " << key_name;
        outError = "Machine key_name not found.";
        return false;
    }
    // check references in recipes
    for (const auto &recPair : _data.recipes) {
        const Recipe &rec = recPair.second;
        for (const auto &mk : rec.produced_in_machines_keys) {
            if (mk == key_name) {
                LOG(WARNING) << "Machine is used in a recipe: " << _data.recipes.find(rec.name)->first << "; cannot delete.";
                outError = "Machine is used in a recipe (" + _data.recipes.find(rec.name)->first + "); cannot delete.";
                return false;
            }
        }
    }
    _data.machines.erase(key_name);
    DLOG(INFO) << "Machine deleted successfully: " << key_name;
    return true;
}

bool GameDataManager::addRecipe(const Recipe &r) {
    std::string key = slugify(r.name);
    if (_data.recipes.count(key)) {
        LOG(WARNING) << "Recipe with key '" << key << "' already exists.";
        return false;
    } // already exists
    Recipe rr = r;
    // if no input or output ports, add a "nothing" port to avoid issues
    if (rr.input_ports.empty()) {
        rr.input_ports.push_back(RecipePort{0.0, "nothing"});
    }
    if (rr.output_ports.empty()) {
        rr.output_ports.push_back(RecipePort{0.0, "nothing"});
    }
    _data.recipes[key] = rr;
    DLOG(INFO) << "Recipe added successfully: " << key;
    return true;
}

bool GameDataManager::editRecipe(const std::string& key_name, const Recipe &r, std::string &outError) {
    if (!_data.recipes.count(key_name)) {
        LOG(WARNING) << "Recipe key_name not found: " << key_name;
        outError = "Recipe key_name not found.";
        return false;
    }
    std::string key = slugify(r.name);
    if (_data.recipes.count(key) && key != key_name) {
        LOG(WARNING) << "Cannot change to that name; another recipe uses it: " << key;
        outError = "Cannot change to that name; another recipe uses it.";
        return false;
    }
    // update
    Recipe rr = r;
    // if no input or output ports, add a "nothing" port to avoid issues
    if (rr.input_ports.empty()) {
        rr.input_ports.push_back(RecipePort{0.0, "nothing"});
    }
    if (rr.output_ports.empty()) {
        rr.output_ports.push_back(RecipePort{0.0, "nothing"});
    }
    _data.recipes.erase(key_name);
    _data.recipes[key] = rr;
    DLOG(INFO) << "Recipe edited successfully: " << key;
    return true;
}

bool GameDataManager::deleteRecipe(const std::string& key_name, std::string &outError) {
    if (!_data.recipes.count(key_name)) {
        LOG(WARNING) << "Recipe key_name not found: " << key_name;
        outError = "Recipe key_name not found.";
        return false;
    }
    _data.recipes.erase(key_name);
    DLOG(INFO) << "Recipe deleted successfully: " << key_name;
    return true;
}

std::vector<std::string> GameDataManager::validate(const GameData &gd) {
    std::vector<std::string> messages;
    // check nothing resource exists
    if (!gd.resources.count("nothing")) {
        messages.emplace_back("Missing required resource with key_name 'nothing'.");
    }

    for (const auto &m : gd.machines) {
        if (m.second.base_crafting_speed <= 0.0) {
            messages.push_back("Machine '" + m.second.name + "' has non-positive base_crafting_speed.");
        }
    }
    for (const auto &r : gd.recipes) {
        if (r.second.time_seconds < 0.0) messages.push_back("Recipe '" + r.second.name + "' has negative time.");
        for (const auto &p : r.second.input_ports) {
            if (p.amount < 0.0) messages.push_back("Recipe '" + r.second.name + "' has negative ingredient amount.");
            if (gd.resources.find(p.resource_key) == gd.resources.end()) {
                messages.push_back("Recipe '" + r.second.name + "' references unknown input resource key '" + p.resource_key + "'.");
            }
        }
        for (const auto &p : r.second.output_ports) {
            if (p.amount <= 0.0) messages.push_back("Recipe '" + r.second.name + "' has non-positive product amount.");
            if (gd.resources.find(p.resource_key) == gd.resources.end()) {
                messages.push_back("Recipe '" + r.second.name + "' references unknown output resource key '" + p.resource_key + "'.");
            }
        }
        if (r.second.produced_in_machines_keys.empty()) {
            messages.push_back("Recipe '" + r.second.name + "' is not assigned to any machines.");
        }
        for (const auto& mkey : r.second.produced_in_machines_keys) {
            if (gd.machines.count(mkey) == 0) {
                messages.push_back("Recipe '" + r.second.name + "' references unknown machine key '" + mkey + "'.");
            }
        }
    }
    DLOG(INFO) << "Validation completed with " << messages.size() << " messages.";
    return messages;
}

GameData GameDataManager::jsonToGameData(const json &j, std::filesystem::path& packageIconRoot, std::string &outError) {
    DLOG(INFO) << "Converting JSON to GameData.";
    outError.clear();
    std::filesystem::path path = _data.gameDataFilePath;
    GameData gd;

    GLuint unknownTexture;
    std::filesystem::path path_unknown = SettingsManager::instance().getSettings().executablePath / "assets" / "icons" / "unknown.png";
    TextureUtils::LoadTextureFromFile(path_unknown.c_str(), &unknownTexture, nullptr, nullptr);
    ImTextureID unknownTextureID = (ImTextureID)(intptr_t)unknownTexture;
    try {
        gd.gameName = j.value("gameName", std::string("unknown"));
        gd.time_unit = j.value("time_unit", std::string("seconds"));
        gd.gameDataFilePath = path;
        // Ensure nothing resource exists
        Resource nothing{ "Nothing"};
        gd.resources["nothing"] = nothing;
        // Items (resources). Accepts arrays named "items", "resources", and "fluids" (fluids appended)
        auto handleResourceArray = [&](const json &arr) {
            if (!arr.is_array()) return;
            for (const auto &rj : arr) {
                std::string name = rj.value("name", std::string());
                std::string key = rj.value("key_name", std::string());
                if (key.empty()) {
                    // skip
                    continue;
                }
                if (gd.resources.count(key)) continue; // skip duplicates
                // avoid duplicating "nothing"
                if (key == "nothing") continue;
                Resource res;
                res.name = name.empty() ? key : name;
                std::filesystem::path texturePath = packageIconRoot / "resources" / (key + ".png");
                if (std::filesystem::exists(texturePath)) {
                    // load texture (defer actual loading to caller)
                    GLuint texture;
                    TextureUtils::LoadTextureFromFile(texturePath.string().c_str(), &texture, nullptr, nullptr);
                    res.texture = (ImTextureID)(intptr_t)texture;
                } else {
                    res.texture = unknownTextureID;
                }
                gd.resources[key] = res;
            }
        };
        if (j.contains("resources")) handleResourceArray(j["resources"]);

        // Machines
        if (j.contains("machines") && j["machines"].is_array()) {
            for (const auto &mj : j["machines"]) {
                std::string name = mj.value("name", std::string());
                std::string key = mj.value("key_name", std::string());
                double eff = mj.value("base_crafting_speed", 1.0);
                if (key.empty()) continue; // skip
                if (gd.machines.count(key)) continue; // skip duplicates
                Machine mm;
                mm.name = name.empty() ? key : name;
                mm.base_crafting_speed = eff <= 0.0 ? 1.0 : eff;
                std::filesystem::path texturePath = packageIconRoot / "machines" / (key + ".png");
                if (std::filesystem::exists(texturePath)) {
                    // load texture (defer actual loading to caller)
                    GLuint texture;
                    TextureUtils::LoadTextureFromFile(texturePath.string().c_str(), &texture, nullptr, nullptr);
                    mm.texture = (ImTextureID)(intptr_t)texture;
                } else {
                    mm.texture = unknownTextureID;
                }
                gd.machines[key] = mm;
            }
        }

        // Recipes
        if (j.contains("recipes") && j["recipes"].is_array()) {
            for (const auto &rj : j["recipes"]) {
                Recipe r;
                std::string key= rj.value("key_name", std::string());
                r.name = rj.value("name", key);
                r.time_seconds = rj.value("time", 0.0);

                if (key.empty()) {
                    // generate key from name
                    key = slugify(r.name);
                    if (key.empty()) {
                        // skip
                        continue;
                    }
                }

                // ingredients
                if (rj.contains("ingredients") && rj["ingredients"].is_array()) {
                    const auto &ing = rj["ingredients"];
                    if (ing.empty()) {
                        // inject nothing
                        r.input_ports.push_back(RecipePort{0.0, "nothing"});
                    } else {
                        for (const auto &pair : ing) {
                            if (!pair.is_array() || pair.size() < 2) continue;
                            std::string kkey = pair[0].get<std::string>();
                            double amount = pair[1].get<double>();
                            if (!gd.resources.count(kkey)) {
                                // auto-create resource
                                Resource res;
                                res.name = kkey;
                                gd.resources[kkey] = res;
                            }
                            r.input_ports.push_back(RecipePort{amount, kkey});
                        }
                    }
                } else {
                    // treat missing field as nothing
                    r.input_ports.push_back(RecipePort{0.0, "nothing"});
                }
                // products
                if (rj.contains("products") && rj["products"].is_array()) {
                    for (const auto &pair : rj["products"]) {
                        if (!pair.is_array() || pair.size() < 2) continue;
                        std::string kkey = pair[0].get<std::string>();
                        double amount = pair[1].get<double>();
                        if (!gd.resources.count(kkey)) {
                            Resource res;
                            res.name = kkey;
                            gd.resources[kkey] = res;
                        }
                        r.output_ports.push_back(RecipePort{amount, kkey});
                    }
                }
                // produced_in
                if (rj.contains("produced_in") && rj["produced_in"].is_array()) {
                    for (const auto &mk : rj["produced_in"]) {
                        std::string kkey = mk.get<std::string>();
                        if (gd.machines.count(kkey)) {
                            r.produced_in_machines_keys.push_back(kkey);
                        }
                    }
                }
                gd.recipes[key] = std::move(r);
            }
        }

        DLOG(INFO) << "JSON parsed successfully into GameData.";

        // perform light validation; if critical issues found, add to outError (but do not fail on warnings)
        auto errors = validate(gd);
        if (!errors.empty()) {
            LOG(WARNING) << "Validation issues found in GameData: " << outError;
        }
        return gd;
    } catch (const std::exception &ex) {
        LOG(ERROR) << "Exception while parsing JSON into GameData: " << ex.what();
        return {};
    }
}

json GameDataManager::gameDataToJson(const GameData &gd) {
    DLOG(INFO) << "Converting GameData to JSON.";
    json j;
    j["gameName"] = gd.gameName;
    j["time_unit"] = gd.time_unit;
    // resources: write all except the reserved "nothing" with id 0
    json resources = json::array();
    for (const auto &res : gd.resources) {
        if (res.first == "nothing") continue; // skip reserved
        json rj;
        rj["name"] = res.second.name;
        rj["key_name"] = res.first;
        resources.push_back(rj);
    }
    if (!resources.empty()) j["resources"] = resources;

    // machines
    if (!gd.machines.empty()) {
        json marr = json::array();
        for (const auto &m : gd.machines) {
            json mj;
            mj["name"] = m.second.name;
            mj["key_name"] = m.first;
            mj["base_crafting_speed"] = m.second.base_crafting_speed;
            marr.push_back(mj);
        }
        j["machines"] = marr;
    }


    // recipes
    if (!gd.recipes.empty()) {
        json rarr = json::array();
        for (const auto &r : gd.recipes) {
            json rj;
            rj["name"] = r.second.name;
            rj["key_name"] = r.first;
            rj["time"] = r.second.time_seconds;

            // ingredients
            json ing = json::array();
            for (const auto &p : r.second.input_ports) {
                // if the input is the reserved nothing, write empty array to be consistent with original style
                if (p.resource_key == "nothing") continue;
                json pair = json::array({ p.resource_key, p.amount });
                ing.push_back(pair);
            }
            // if ing remains empty, write [] to indicate mining/no input
            rj["ingredients"] = ing;

            // products
            json prod = json::array();
            for (const auto &p : r.second.output_ports) {
                // skip nothing outputs
                if (p.resource_key == "nothing") continue;
                json pair = json::array({ p.resource_key, p.amount });
                prod.push_back(pair);
            }
            rj["products"] = prod;

            // machines produced in
            if (!r.second.produced_in_machines_keys.empty()) {
                json mKeyArr = json::array();
                for (const auto& key : r.second.produced_in_machines_keys) {
                    mKeyArr.push_back(key);
                }
                if (!mKeyArr.empty()) rj["produced_in"] = mKeyArr;
            }

            rarr.push_back(rj);
        }
        j["recipes"] = rarr;
    }
    DLOG(INFO) << "GameData converted successfully to JSON.";
    return j;
}