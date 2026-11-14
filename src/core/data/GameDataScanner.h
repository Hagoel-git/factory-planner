#ifndef FACTORY_PLANNER_GAMEDATASCANNER_H
#define FACTORY_PLANNER_GAMEDATASCANNER_H

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <absl/log/log.h>
#include <nlohmann/json.hpp>

struct GameDataPackage {
    std::string gameName;
    std::string dataName;
    std::string uuid; // Stores game.id in v4
    std::string dataVersion;
    std::filesystem::path dataFilePath;
    std::filesystem::path iconRootPath;
};

inline void tryCollectManifestPackage(
    const std::filesystem::path& manifestPath,
    const std::filesystem::path& fallbackDirName,
    std::vector<GameDataPackage>& outPackages) {
    std::error_code ec;
    if (!std::filesystem::exists(manifestPath, ec) || !std::filesystem::is_regular_file(manifestPath, ec)) {
        return;
    }
    try {
        std::ifstream ifs(manifestPath);
        if (!ifs.is_open()) return;
        nlohmann::json j = nlohmann::json::parse(ifs, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return;
        if (j.value("schema_version", 0) != 4) return;

        GameDataPackage pkg;
        pkg.dataFilePath = manifestPath;
        std::filesystem::path pkgRoot = manifestPath.parent_path();
        std::string iconRel = j.value("icon_root", std::string("icons"));
        pkg.iconRootPath = pkgRoot / iconRel;

        if (j.contains("game") && j["game"].is_object()) {
            const auto& gj = j["game"];
            pkg.uuid = gj.value("id", fallbackDirName.filename().string());
            pkg.gameName = gj.value("name", pkg.uuid);
            pkg.dataVersion = gj.value("data_version", std::string("1.0"));
            pkg.dataName = pkg.dataVersion.empty() ? pkg.uuid : (pkg.uuid + " (" + pkg.dataVersion + ")");
        } else {
            pkg.uuid = fallbackDirName.filename().string();
            pkg.gameName = pkg.uuid;
            pkg.dataName = pkg.uuid;
        }
        outPackages.push_back(std::move(pkg));
    } catch (const std::exception& e) {
        LOG(WARNING) << "Failed to scan manifest " << manifestPath << ": " << e.what();
    }
}

inline std::vector<GameDataPackage> scanForGameData(const std::filesystem::path& gameDataDir) {
    std::vector<GameDataPackage> packages;
    std::error_code ec;
    if (!std::filesystem::exists(gameDataDir, ec) || !std::filesystem::is_directory(gameDataDir, ec)) {
        return packages;
    }
    try {
        for (const auto& gameDir : std::filesystem::directory_iterator(
                 gameDataDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (!gameDir.is_directory(ec)) continue;

            // Direct <gameDataDir>/<game>/game.json
            std::filesystem::path directManifest = gameDir.path() / "game.json";
            if (std::filesystem::exists(directManifest, ec)) {
                tryCollectManifestPackage(directManifest, gameDir.path(), packages);
                continue;
            }

            // Versioned sub-directories <gameDataDir>/<game>/<version>/game.json
            std::error_code innerEc;
            for (const auto& subDir : std::filesystem::directory_iterator(
                     gameDir.path(), std::filesystem::directory_options::skip_permission_denied, innerEc)) {
                if (innerEc) break;
                if (!subDir.is_directory(innerEc)) continue;
                std::filesystem::path subManifest = subDir.path() / "game.json";
                if (std::filesystem::exists(subManifest, innerEc)) {
                    tryCollectManifestPackage(subManifest, gameDir.path(), packages);
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        LOG(WARNING) << "Filesystem error while scanning game data in " << gameDataDir << ": " << e.what();
    }
    VLOG(2) << "Found " << packages.size() << " game data packages.";
    return packages;
}

#endif // FACTORY_PLANNER_GAMEDATASCANNER_H
