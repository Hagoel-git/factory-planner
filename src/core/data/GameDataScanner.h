#ifndef FACTORY_PLANNER_GAMEDATASCANNER_H
#define FACTORY_PLANNER_GAMEDATASCANNER_H
#include <filesystem>
#include <fstream>
#include <vector>
#include <absl/log/log.h>

struct GameDataPackage {
    std::string gameName;
    std::string dataName;
    std::string uuid;
    std::filesystem::path dataFilePath;
    std::filesystem::path iconRootPath;
};

static std::vector<GameDataPackage> scanForGameData(const std::filesystem::path& gameDataDir) {
    std::vector<GameDataPackage> packages;
    for (const auto& gameDir : std::filesystem::directory_iterator(gameDataDir)) {
        if (!gameDir.is_directory()) continue;

        std::filesystem::path dataFilesPath = gameDir.path() / "game_datas";
        std::filesystem::path iconsPath = gameDir.path() / "icons";

        if (std::filesystem::exists(dataFilesPath) && std::filesystem::is_directory(dataFilesPath)) {
            for (const auto& dataFile : std::filesystem::directory_iterator(dataFilesPath)) {
                if (dataFile.path().extension() == ".gd") {
                    GameDataPackage pkg;
                    pkg.gameName = gameDir.path().filename().string();
                    pkg.dataName = dataFile.path().stem().string();
                    pkg.dataFilePath = dataFile.path();
                    pkg.iconRootPath = iconsPath;
                    try {
                        std::ifstream gdFile(pkg.dataFilePath);
                        if (gdFile.is_open()) {
                            nlohmann::json j = nlohmann::json::parse(gdFile, nullptr, false);
                            if (!j.is_discarded() && j.contains("uuid")) {
                                pkg.uuid = j["uuid"].get<std::string>();
                            }
                        }
                    } catch (const std::exception& e) {
                        LOG(WARNING) << "Failed to scan UUID for " << pkg.dataName << ": " << e.what();
                    }
                    packages.push_back(pkg);
                }
            }
        }
    }
    VLOG(2) << "Found " << packages.size() << " game data packages.";
    return packages;
}
#endif //FACTORY_PLANNER_GAMEDATASCANNER_H