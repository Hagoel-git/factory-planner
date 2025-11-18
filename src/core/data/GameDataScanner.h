#ifndef FACTORY_PLANNER_GAMEDATASCANNER_H
#define FACTORY_PLANNER_GAMEDATASCANNER_H
#include <filesystem>
#include <vector>
#include <absl/log/log.h>

struct GameDataPackage {
    std::string gameName;
    std::string dataName;
    std::filesystem::path dataFilePath;
    std::filesystem::path iconRootPath;
};

static std::vector<GameDataPackage> ScanForGameData(const std::filesystem::path& gameDataDir) {
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
                    packages.push_back(pkg);
                }
            }
        }
    }
    VLOG(2) << "Found " << packages.size() << " game data packages.";
    return packages;
}
#endif //FACTORY_PLANNER_GAMEDATASCANNER_H