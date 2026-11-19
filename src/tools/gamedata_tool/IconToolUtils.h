#ifndef ICONTOOLUTILS_H
#define ICONTOOLUTILS_H

#include "core/data/GameDataV4.h"
#include "core/data/IconPak.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gamedata_tool {

struct ReferencedIcon {
    enum class Kind { Resource, Machine, Option };

    Kind kind = Kind::Resource;
    std::string key;          // "resources/<id>" | "machines/<id>" | "options/<set>/<key>"
    std::string name_slug;    // slugify(display_name) for resources/machines; empty for options
    std::string rel_png;      // "<key>.png"
    std::string json_pointer; // "/resources/<i>" | "/machines/<i>" | "/option_sets/<set>/options/<i>"
};

std::vector<ReferencedIcon> collectReferencedIcons(const gamedata::GamePackage& pkg);

struct IconSources {
    std::filesystem::path root_pak_path;
    std::filesystem::path icon_pak_path;
    std::filesystem::path loose_dir;

    bool has_root_pak = false;
    bool has_icon_pak = false;
    bool has_loose_dir = false;

    std::optional<gamedata::IconPakReader> root_pak;
    std::optional<gamedata::IconPakReader> icon_pak;

    std::string root_pak_display = "icons.pak";
    std::string icon_pak_display = "icons/icons.pak";

    bool noIconsMode() const {
        return !has_root_pak && !has_icon_pak && !has_loose_dir;
    }

    // Returns the active pak reader in T8 priority (<package_root>/icons.pak first, then <icon_root>/icons.pak).
    const gamedata::IconPakReader* primaryPak(std::filesystem::path* outPath = nullptr) const {
        if (root_pak.has_value()) {
            if (outPath) *outPath = root_pak_path;
            return &(*root_pak);
        }
        if (icon_pak.has_value()) {
            if (outPath) *outPath = icon_pak_path;
            return &(*icon_pak);
        }
        return nullptr;
    }
};

void openIconSources(
    const gamedata::GamePackage& pkg,
    IconSources& outSources,
    std::vector<gamedata::LoadDiagnostic>& outErrors);

struct ScannedFile {
    std::string rel_path;       // generic UTF-8 path relative to base dir, e.g. "resources/iron-ore.png"
    std::string lower_rel_path; // ASCII-lowercased rel_path
    std::filesystem::path full_path;
    std::filesystem::file_time_type mtime{};
    bool has_png_ext = false;
};

struct ScannedIconDir {
    std::vector<ScannedFile> files; // sorted by rel_path
    std::unordered_map<std::string, size_t> by_exact_rel;
    std::unordered_map<std::string, size_t> by_lower_rel;
    std::optional<std::filesystem::file_time_type> newest_mtime;
};

ScannedIconDir scanIconDirectory(const std::filesystem::path& baseDir);

std::string toAsciiLower(std::string_view s);
std::string formatCountWithCommas(uint64_t value);
std::string makeNoIconsNote(const gamedata::GamePackage& pkg);
std::string formatVIcon1Message(std::string_view label, uint32_t w, uint32_t h, std::string_view parseError);

void checkPackageIconsForValidate(const gamedata::GamePackage& pkg, gamedata::LoadReport& report);

struct PackageIconCoverage {
    std::string mode = "none"; // "none" | "loose" | "pak"
    size_t resources_present = 0;
    size_t resources_total = 0;
    size_t machines_present = 0;
    size_t machines_total = 0;
    size_t options_present = 0;
    size_t options_total = 0;
};

PackageIconCoverage computePackageIconCoverage(const gamedata::GamePackage& pkg);

} // namespace gamedata_tool

#endif // ICONTOOLUTILS_H
