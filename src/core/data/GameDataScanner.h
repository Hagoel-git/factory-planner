#ifndef FACTORY_PLANNER_GAMEDATASCANNER_H
#define FACTORY_PLANNER_GAMEDATASCANNER_H
#include <filesystem>
#include <fstream>
#include <vector>
#include <absl/log/log.h>
#include <nlohmann/json.hpp>

struct GameDataPackage {
    std::string gameName;
    std::string dataName;
    std::string uuid;
    std::filesystem::path dataFilePath;
    std::filesystem::path iconRootPath;
};

static std::vector<GameDataPackage> scanForGameData(const std::filesystem::path& gameDataDir) {
    std::vector<GameDataPackage> packages;
    std::error_code ec;
    if (!std::filesystem::exists(gameDataDir, ec) || !std::filesystem::is_directory(gameDataDir, ec)) {
        return packages;
    }
    try {
        for (const auto& gameDir : std::filesystem::directory_iterator(gameDataDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (!gameDir.is_directory(ec)) continue;

            std::filesystem::path dataFilesPath = gameDir.path() / "game_datas";
            std::filesystem::path iconsPath = gameDir.path() / "icons";

            std::error_code inner_ec;
            if (std::filesystem::exists(dataFilesPath, inner_ec) && std::filesystem::is_directory(dataFilesPath, inner_ec)) {
                for (const auto& dataFile : std::filesystem::directory_iterator(dataFilesPath, std::filesystem::directory_options::skip_permission_denied, inner_ec)) {
                    if (inner_ec) break;
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
    } catch (const std::filesystem::filesystem_error& e) {
        LOG(WARNING) << "Filesystem error while scanning game data in " << gameDataDir << ": " << e.what();
    }
    VLOG(2) << "Found " << packages.size() << " game data packages.";
    return packages;
}
#endif //FACTORY_PLANNER_GAMEDATASCANNER_H