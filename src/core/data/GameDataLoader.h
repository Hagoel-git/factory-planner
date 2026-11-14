#ifndef GAMEDATALOADER_H
#define GAMEDATALOADER_H

#include "core/data/GameDataV4.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace gamedata {

// Loads and validates a GameData v4 package from disk (§2.1–§2.9, §9.1–§9.2).
// `manifestOrDir` may be either the package directory (containing `game.json`)
// or the direct path to `game.json`.
//
// Returns true iff the package is usable (either `report.ok()` is true, or
// `ignoreErrors` is true and `!report.fatal`).
// When returning false, `outPackage` is left empty.
bool loadGamePackage(
    const std::filesystem::path& manifestOrDir,
    GamePackage& outPackage,
    LoadReport& outReport,
    bool ignoreErrors = false);

// Loads and validates a GameData v4 package from an in-memory manifest JSON string
// and an optional map of relative shard paths -> JSON strings (for unit testing and
// embedded packages). If `packageRoot` is non-empty and a shard path is not found in
// `virtualShards`, falls back to reading from `packageRoot` on disk.
bool loadGamePackageFromMemory(
    std::string_view manifestJson,
    const std::unordered_map<std::string, std::string>& virtualShards,
    GamePackage& outPackage,
    LoadReport& outReport,
    bool ignoreErrors = false,
    const std::filesystem::path& packageRoot = {});

// Checks for missing icon files on disk (`V-W-1`, §2.1, §9.2):
// - icons/resources/<id>.png
// - icons/machines/<id>.png
// - icons/options/<set>/<key>.png
// Appends any missing icon warnings to `inOutReport.warnings`.
void checkMissingIcons(const GamePackage& package, LoadReport& inOutReport);

} // namespace gamedata

#endif // GAMEDATALOADER_H
