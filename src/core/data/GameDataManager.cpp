#include "GameDataManager.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <random>
#include <absl/log/log.h>

#include <nlohmann/json.hpp>

#include "common/StringUtils.h"
#include "services/NotificationManager.h"
#include "services/TextureManager.h"

using json = nlohmann::json;

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

GameDataManager::GameDataManager() {
    VLOG(2) << "Initializing GameDataManager with default data.";
    m_data.time_unit = "seconds";
    m_data.resources["nothing"] = Resource{"Nothing"};
}

const GameData& GameDataManager::current() const {
    return m_data;
}

GameData& GameDataManager::current() {
    return m_data;
}

void GameDataManager::clear() {
    m_data = {};
    m_data.time_unit = "seconds";
    m_data.resources["nothing"] = Resource{"Nothing"};
}

bool GameDataManager::loadFromFile(const std::filesystem::path &path, std::string &outError) {
    try {
        if (!std::filesystem::exists(path)) {
            LOG(ERROR) << "File does not exist: " << path;
            outError = "File does not exist: " + path.string();
            return false;
        }
        std::ifstream ifs(path);
        if (!ifs) {
            LOG(ERROR) << "Failed to open file: " << path;
            outError = "Failed to open file: " + path.string();
            return false;
        }
        json j;
        ifs >> j;

        std::filesystem::path dataFilePath(path);
        std::filesystem::path packageRoot = dataFilePath.parent_path().parent_path(); // structure: <package_root>/game_datas/<datafile>.gd
        std::filesystem::path iconRoot = packageRoot / "icons";

        m_data.gameDataFilePath = path;
        m_data = jsonToGameData(j, iconRoot, outError);

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
        outError = ex.what();
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
            if (p.amount <= 0.0 && p.resource_key != "nothing") messages.push_back("Recipe '" + r.second.name + "' has non-positive product amount.");
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
    std::filesystem::path path = m_data.gameDataFilePath;
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
                    const auto &prod = rj["products"];
                    if (prod.empty()) {
                        // inject nothing
                        r.output_ports.push_back(RecipePort{0.0, "nothing"});
                    } else {
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
                } else {
                    // treat missing field as nothing
                    r.output_ports.push_back(RecipePort{0.0, "nothing"});
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
            LOG(WARNING) << "Validation issues found in GameData: " << msg;
        }
        return gd;
    } catch (const std::exception &ex) {
        LOG(ERROR) << "Exception while parsing JSON into GameData: " << ex.what();
        NotificationManager::instance().addNotification("Error Loading Game Data",
            "An error occurred while parsing game data: " + std::string(ex.what()),
            NotificationType::Error);
        outError = ex.what();
        return {};
    }
}
