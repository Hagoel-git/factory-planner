#include "GameDataManager.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <random>
#include <absl/log/log.h>
#include <GL/gl.h>

#include <nlohmann/json.hpp>

#include "services/SettingsManager.h"
#include "common/StringUtils.h"
#include "common/TextureUtils.h"
#include "services/NotificationManager.h"
#include "services/TextureManager.h"

using json = nlohmann::json;

static const std::string PREFIX_RES = "res@";
static const std::string PREFIX_MAC = "mac@";
static const std::string PREFIX_REC = "rec@";

static std::string generateUUID() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    const char* digits = "0123456789abcdef";
    std::string uuid = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
    for (char &c : uuid) {
        if (c == 'x') c = digits[dis(gen)];
        else if (c == 'y') c = digits[(dis(gen) & 0x3) | 0x8];
    }
    return uuid;
}

void GameDataManager::registerAlias(const std::string &prefix, const std::string &oldId, const std::string &newId) {
    if (oldId == newId) return;
    // 1. Add direct alias with Namespace
    std::string namespacedOld = prefix + oldId;
    _data.id_aliases[namespacedOld] = newId;

    // 2. Flatten chains: Only update aliases that belong to THIS namespace
    for (auto& pair : _data.id_aliases) {
        // Check if this alias entry belongs to the same category (starts with same prefix)
        if (pair.first.rfind(prefix, 0) == 0) {
            if (pair.second == oldId) {
                pair.second = newId;
            }
        }
    }
    VLOG(3) << "Alias registered: " << oldId << " -> " << newId;
}

bool GameDataManager::renameIconFile(const std::string &category, const std::string &oldKey,
    const std::string &newKey) {
    if (_data.gameDataFilePath.empty()) return false;
    std::filesystem::path root = _data.gameDataFilePath.parent_path().parent_path();
    std::filesystem::path oldIcon = root / "icons" / category / (oldKey + ".png");
    std::filesystem::path newIcon = root / "icons" / category / (newKey + ".png");

    if (std::filesystem::exists(oldIcon)) {
        try {
            if (std::filesystem::exists(newIcon)) {
                std::filesystem::remove(newIcon);
            }
            std::filesystem::rename(oldIcon, newIcon);
            return true;
        } catch (const std::exception& e) {
            LOG(ERROR) << "Failed to rename icon: " << e.what();
            return false;
        }
    }
    return false;
}

bool GameDataManager::copyIconFile(const std::string &category, const std::string &key,
    const std::filesystem::path &sourcePath) {
    if (_data.gameDataFilePath.empty()) return false;
    std::filesystem::path root = _data.gameDataFilePath.parent_path().parent_path();
    std::filesystem::path destDir = root / "icons" / category;
    std::filesystem::path destFile = destDir / (key + ".png");

    try {
        std::filesystem::create_directories(destDir);
        std::filesystem::copy_file(sourcePath, destFile, std::filesystem::copy_options::overwrite_existing);
        return true;
    } catch (const std::exception& e) {
        LOG(ERROR) << "Failed to copy icon: " << e.what();
        return false;
    }
}

bool GameDataManager::performRenameResource(const std::string& oldKey, const std::string& newKey) {
    auto node = _data.resources.extract(oldKey);
    if (node.empty()) return false;
    node.key() = newKey;
    _data.resources.insert(std::move(node));

    registerAlias(PREFIX_RES, oldKey, newKey);

    renameIconFile("resources", oldKey, newKey);

    for (auto &recPair : _data.recipes) {
        Recipe &rec = recPair.second;
        for (auto &p : rec.input_ports) if (p.resource_key == oldKey) p.resource_key = newKey;
        for (auto &p : rec.output_ports) if (p.resource_key == oldKey) p.resource_key = newKey;
    }
    return true;
}

bool GameDataManager::performRenameMachine(const std::string &oldKey, const std::string &newKey) {
    auto node = _data.machines.extract(oldKey);
    if (node.empty()) return false;
    node.key() = newKey;
    _data.machines.insert(std::move(node));

    registerAlias(PREFIX_MAC, oldKey, newKey);
    renameIconFile("machines", oldKey, newKey);

    for (auto &recPair : _data.recipes) {
        Recipe &rec = recPair.second;
        for (auto &mk : rec.produced_in_machines_keys) if (mk == oldKey) mk = newKey;
    }
    return true;
}

bool GameDataManager::performRenameRecipe(const std::string &oldKey, const std::string &newKey) {
    auto node = _data.recipes.extract(oldKey);
    if (node.empty()) return false;
    node.key() = newKey;
    _data.recipes.insert(std::move(node));

    registerAlias(PREFIX_REC, oldKey, newKey);
    return true;
}

GameDataManager::GameDataManager() {
    // initialize an empty GameData with the "nothing" resource
    VLOG(2) << "Initializing GameDataManager with default data.";
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
        NotificationManager::instance().addNotification(
            "Error Loading Game Data",
            "An error occurred while loading game data: " + std::string(ex.what()),
            NotificationType::Error);
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
        NotificationManager::instance().addNotification(
            "Error Saving Game Data",
            "An error occurred while saving game data: " + std::string(ex.what()),
            NotificationType::Error);
        return false;
    }
}

GameData GameDataManager::createNew(const std::string &gameName, const std::string &timeUnit) {
    VLOG(2) << "Creating new GameData: " << gameName << " with time unit: " << timeUnit;
    GameData gd;
    gd.gameName = gameName;
    gd.gameDataFilePath = "";
    gd.time_unit = timeUnit;
    gd.uuid = generateUUID();
    Resource nothing;
    nothing.name = "Nothing";
    gd.resources["nothing"] = nothing;
    _data = gd;
    return _data;
}

GameData& GameDataManager::current() {
    return _data;
}

void GameDataManager::clear() {
    _data = {};
}

bool GameDataManager::addResource(const Resource &r) {
    std::string key = slugify(r.name);
    // don't allow adding key "nothing"
    if (key == "nothing" || key.empty()) {
        LOG(WARNING) << "Cannot add reserved resource 'nothing'.";
        return false;
    }
    if (_data.resources.count(key)) {
        LOG(WARNING) << "Resource with key '" << key << "' already exists.";
        return false;
    }
    _data.resources[key] = r;
    VLOG(2) << "Resource added successfully: " << key;
    return true;
}

bool GameDataManager::editResource(const std::string& current_key, const Resource &r, std::string &outError) {
    if (current_key == "nothing") {
        LOG(WARNING) << "Cannot edit reserved resource 'nothing'.";
        outError = "Cannot edit reserved resource 'nothing'.";
        return false;
    }
    if (!_data.resources.count(current_key)) {
        LOG(WARNING) << "Resource key not found: " << current_key;
        outError = "Resource key not found.";
        return false;
    }

    std::string new_key = slugify(r.name);

    if (new_key != current_key) {
        if (_data.id_aliases.count(PREFIX_RES + new_key)) {
            if (_data.id_aliases.at(PREFIX_RES + new_key) != current_key) {
                LOG(WARNING) << "Name conflicts with a historical alias: " << new_key;
                outError = "Name conflicts with a historical alias.";
                return false;
            }
        }
        if (_data.resources.count(new_key)) {
            LOG(WARNING) << "Cannot change to that name; another resource uses it: " << new_key;
            outError = "Cannot change to that name; another resource uses it.";
            return false;
        }
        performRenameResource(current_key, new_key);
    }

    // The UI sends a partial object (missing texture).
    _data.resources[new_key].name = r.name;
    VLOG(2) << "Resource edited successfully: " << new_key;
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
    VLOG(2) << "Resource deleted successfully: " << key_name;
    return true;
}

bool GameDataManager::setResourceIcon(const std::string &key, const std::filesystem::path &sourcePath,
    std::string &outError) {
    if (!_data.resources.count(key)) {
        LOG(WARNING) << "Resource key not found: " << key;
        outError = "Resource key not found.";
        return false;
    }
    if (copyIconFile("resources", key, sourcePath)) {
        std::filesystem::path root = _data.gameDataFilePath.parent_path().parent_path();
        std::filesystem::path texPath = root / "icons" / "resources" / (key + ".png");
        _data.resources[key].texture = TextureManager::instance().loadTexture(texPath);
        VLOG(2) << "Resource icon set successfully for key: " << key;
        return true;
    }
    LOG(ERROR) << "Failed to copy resource icon for key: " << key;
    outError = "Failed to copy resource icon.";
    return false;
}

bool GameDataManager::addMachine(const Machine &m) {
    std::string key = slugify(m.name);
    if (_data.machines.count(key) || key.empty()) {
        LOG(WARNING) << "Machine with key '" << key << "' already exists.";
        return false;
    }
    _data.machines[key] = m;
    VLOG(2) << "Machine added successfully: " << key;
    return true;
}

bool GameDataManager::editMachine(const std::string& current_key, const Machine &m, std::string &outError) {
    if (!_data.machines.count(current_key)) {
        LOG(WARNING) << "Machine key not found: " << current_key;
        outError = "Machine key not found.";
        return false;
    }
    std::string new_key = slugify(m.name);
    if (new_key != current_key) {
        if (_data.id_aliases.count(PREFIX_MAC + new_key)) {
            if (_data.id_aliases.at(PREFIX_RES + new_key) != current_key) {
                LOG(WARNING) << "Name conflicts with a historical alias: " << new_key;
                outError = "Name conflicts with a historical alias.";
                return false;
            }
        }
        if (_data.machines.count(new_key)) {
            LOG(WARNING) << "Cannot change to that name; another machine uses it: " << new_key;
            outError = "Cannot change to that name; another machine uses it.";
            return false;
        }
        performRenameMachine(current_key, new_key);
    }

    _data.machines[new_key].name = m.name;
    _data.machines[new_key].base_crafting_speed = m.base_crafting_speed;
    VLOG(2) << "Machine edited successfully: " << new_key;
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
    VLOG(2) << "Machine deleted successfully: " << key_name;
    return true;
}

bool GameDataManager::setMachineIcon(const std::string &key, const std::filesystem::path &sourcePath,
    std::string &outError) {
    if (!_data.machines.count(key)) {
        LOG(WARNING) << "Machine not found: " << key;
        outError = "Machine not found";
        return false;
    }
    if (copyIconFile("machines", key, sourcePath)) {
        std::filesystem::path root = _data.gameDataFilePath.parent_path().parent_path();
        std::filesystem::path texPath = root / "icons" / "machines" / (key + ".png");
        _data.machines[key].texture = TextureManager::instance().loadTexture(texPath);
        return true;
    }
    LOG(WARNING) << "Failed to copy icon for machine key: " << key;
    outError = "Failed to copy icon";
    return false;
}

bool GameDataManager::addRecipe(const Recipe &r) {
    std::string key = slugify(r.name);
    if (_data.recipes.count(key) || key.empty()) {
        LOG(WARNING) << "Recipe with key '" << key << "' already exists.";
        return false;
    }
    Recipe rr = r;
    // if no input or output ports, add a "nothing" port to avoid issues
    if (rr.input_ports.empty()) {
        rr.input_ports.push_back(RecipePort{0.0, "nothing"});
    }
    if (rr.output_ports.empty()) {
        rr.output_ports.push_back(RecipePort{0.0, "nothing"});
    }
    _data.recipes[key] = rr;
    VLOG(2) << "Recipe added successfully: " << key;
    return true;
}

bool GameDataManager::editRecipe(const std::string& current_key, const Recipe &r, std::string &outError) {
    if (!_data.recipes.count(current_key)) {
        LOG(WARNING) << "Recipe key not found: " << current_key;
        outError = "Recipe key not found.";
        return false;
    }
    std::string new_key = slugify(r.name);

    // update
    Recipe rr = r;
    // if no input or output ports, add a "nothing" port to avoid issues
    if (rr.input_ports.empty()) {
        rr.input_ports.push_back(RecipePort{0.0, "nothing"});
    }
    if (rr.output_ports.empty()) {
        rr.output_ports.push_back(RecipePort{0.0, "nothing"});
    }
    if (new_key != current_key) {
        if (_data.id_aliases.count(PREFIX_REC + new_key)) {
            if (_data.id_aliases.at(PREFIX_RES + new_key) != current_key) {
                LOG(WARNING) << "Name conflicts with a historical alias: " << new_key;
                outError = "Name conflicts with a historical alias.";
                return false;
            }
        }
        if (_data.recipes.count(new_key)) {
            LOG(WARNING) << "Cannot change to that name; another recipe uses it: " << new_key;
            outError = "Cannot change to that name; another recipe uses it.";
            return false;
        }
        performRenameRecipe(current_key, new_key);
    }

    _data.recipes[new_key] = rr;
    VLOG(2) << "Recipe edited successfully: " << new_key;
    return true;
}

bool GameDataManager::deleteRecipe(const std::string& key_name, std::string &outError) {
    if (!_data.recipes.count(key_name)) {
        LOG(WARNING) << "Recipe key_name not found: " << key_name;
        outError = "Recipe key_name not found.";
        return false;
    }
    _data.recipes.erase(key_name);
    VLOG(2) << "Recipe deleted successfully: " << key_name;
    return true;
}

bool GameDataManager::duplicateDataFile(std::string &outError) {
    if (_data.gameDataFilePath.empty()) {
        outError = "Current file is not saved to disk.";
        return false;
    }

    std::filesystem::path sourcePath = _data.gameDataFilePath;
    std::filesystem::path parentDir = sourcePath.parent_path();
    std::string stem = sourcePath.stem().string();
    std::string ext = sourcePath.extension().string();

    std::filesystem::path destPath = parentDir / (stem + " - copy" + ext);

    if (std::filesystem::exists(destPath)) {
        int counter = 1;
        do {
            destPath = parentDir / (stem + " - copy (" + std::to_string(counter) + ")" + ext);
            counter++;
            if (counter > 100) {
                outError = "Too many duplicate files exist. Cleanup required.";
                return false;
            }
        } while (std::filesystem::exists(destPath));
    }

    try {
        // 1. Read existing file from disk (ignoring unsaved memory changes to match OS behavior)
        std::ifstream ifs(sourcePath);
        json j;
        ifs >> j;
        ifs.close();

        // 2. Modify UUID so it is a distinct entity
        j["uuid"] = generateUUID();

        // 3. Write to new file
        std::ofstream ofs(destPath);
        ofs << j.dump(2);
        ofs.close();

        LOG(INFO) << "Duplicated game data: " << sourcePath << " -> " << destPath;
        return true;
    } catch (const std::exception& e) {
        outError = std::string("Failed to duplicate file: ") + e.what();
        LOG(ERROR) << outError;
        return false;
    }
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
    return messages;
}

GameData GameDataManager::jsonToGameData(const json &j, std::filesystem::path& packageIconRoot, std::string &outError) {
    VLOG(2) << "Converting JSON to GameData.";
    outError.clear();
    std::filesystem::path path = _data.gameDataFilePath;
    GameData gd;

    try {
        if (!path.empty() && path.has_parent_path() && path.parent_path().filename() == "game_datas") {
            gd.gameName = path.parent_path().parent_path().filename().string();
        }
        gd.time_unit = j.value("time_unit", std::string("seconds"));
        gd.schema_version = j.value("schema_version", 3);
        gd.uuid = j.value("uuid", generateUUID());
        gd.gameDataFilePath = path;

        if (j.contains("id_aliases")) {
            gd.id_aliases = j["id_aliases"].get<std::map<std::string, std::string>>();
        }

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

                res.texture = TextureManager::instance().loadTexture(texturePath);

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
                mm.texture = TextureManager::instance().loadTexture(texturePath);
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

        VLOG(2) << "JSON parsed successfully into GameData.";

        // perform light validation; if critical issues found, add to outError (but do not fail on warnings)
        auto errors = validate(gd);
        if (!errors.empty()) {
            std::string msg;
            for(const auto& e : errors) msg += e + "; ";
            LOG(WARNING) << "Validation issues found in GameData: " << msg;        }
        return gd;
    } catch (const std::exception &ex) {
        LOG(ERROR) << "Exception while parsing JSON into GameData: " << ex.what();
        NotificationManager::instance().addNotification("Error Loading Game Data",
            "An error occurred while parsing game data: " + std::string(ex.what()),
            NotificationType::Error);
        return {};
    }
}

json GameDataManager::gameDataToJson(const GameData &gd) {
    VLOG(2) << "Converting GameData to JSON.";
    json j;
    j["uuid"] = gd.uuid;
    j["schema_version"] = gd.schema_version;
    j["id_aliases"] = gd.id_aliases;
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
    VLOG(2) << "GameData converted successfully to JSON.";
    return j;
}