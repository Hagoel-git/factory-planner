#include "tools/gamedata_tool/Commands.h"

#include "common/FilesystemUtils.h"
#include "common/StringUtils.h"
#include "core/expr/ExprCompiler.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gamedata_tool {

namespace {

using json = nlohmann::ordered_json;

void appendU32BE(std::string& buf, uint32_t v) {
    buf.push_back(static_cast<char>((v >> 24) & 0xFF));
    buf.push_back(static_cast<char>((v >> 16) & 0xFF));
    buf.push_back(static_cast<char>((v >> 8) & 0xFF));
    buf.push_back(static_cast<char>(v & 0xFF));
}

uint32_t computeCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint32_t>(data[i]);
        for (int b = 0; b < 8; ++b) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t computeAdler32(const uint8_t* data, size_t len) {
    uint32_t s1 = 1;
    uint32_t s2 = 0;
    for (size_t i = 0; i < len; ++i) {
        s1 = (s1 + data[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    return (s2 << 16) | s1;
}

void appendPngChunk(std::string& png, const char type[4], const std::string& payload) {
    appendU32BE(png, static_cast<uint32_t>(payload.size()));
    const size_t crcStart = png.size();
    png.append(type, 4);
    png.append(payload);
    const uint32_t crc = computeCrc32(
        reinterpret_cast<const uint8_t*>(png.data() + crcStart),
        png.size() - crcStart);
    appendU32BE(png, crc);
}

// Builds a valid, deterministic 16x16 RGBA square PNG with a solid color and subtle border.
std::string makePlaceholderPng(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    constexpr uint32_t kSize = 16;
    std::string png;
    png.reserve(1200);

    static constexpr std::array<uint8_t, 8> kSignature = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
    };
    png.append(reinterpret_cast<const char*>(kSignature.data()), kSignature.size());

    // IHDR
    std::string ihdr;
    ihdr.reserve(13);
    appendU32BE(ihdr, kSize);
    appendU32BE(ihdr, kSize);
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // RGBA
    ihdr.push_back(0); // compression
    ihdr.push_back(0); // filter
    ihdr.push_back(0); // interlace
    appendPngChunk(png, "IHDR", ihdr);

    // Raw scanlines: filter byte 0 + RGBA pixels
    std::string raw;
    raw.reserve(kSize * (1 + kSize * 4));
    for (uint32_t y = 0; y < kSize; ++y) {
        raw.push_back(0); // filter: None
        for (uint32_t x = 0; x < kSize; ++x) {
            const bool border = (x == 0 || y == 0 || x + 1 == kSize || y + 1 == kSize);
            raw.push_back(static_cast<char>(border ? (r / 2) : r));
            raw.push_back(static_cast<char>(border ? (g / 2) : g));
            raw.push_back(static_cast<char>(border ? (b / 2) : b));
            raw.push_back(static_cast<char>(a));
        }
    }

    // Zlib stream with a single stored (uncompressed) DEFLATE block
    std::string zlib;
    zlib.reserve(raw.size() + 11);
    zlib.push_back(static_cast<char>(0x78)); // CMF
    zlib.push_back(static_cast<char>(0x01)); // FLG (0x7801 % 31 == 0)
    zlib.push_back(static_cast<char>(0x01)); // BFINAL=1, BTYPE=00
    const uint16_t len = static_cast<uint16_t>(raw.size());
    const uint16_t nlen = static_cast<uint16_t>(~len);
    zlib.push_back(static_cast<char>(len & 0xFF));
    zlib.push_back(static_cast<char>((len >> 8) & 0xFF));
    zlib.push_back(static_cast<char>(nlen & 0xFF));
    zlib.push_back(static_cast<char>((nlen >> 8) & 0xFF));
    zlib.append(raw);
    const uint32_t adler = computeAdler32(
        reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
    appendU32BE(zlib, adler);

    appendPngChunk(png, "IDAT", zlib);
    appendPngChunk(png, "IEND", {});
    return png;
}

} // namespace

int runInit(const Args& args, std::ostream& out, std::ostream& err) {
    const std::filesystem::path dir = pathFromUtf8(args.target);
    std::error_code ec;

    if (std::filesystem::exists(dir, ec)) {
        if (!std::filesystem::is_directory(dir, ec)) {
            err << "error: target path exists and is not a directory: " << pathToUtf8(dir) << "\n";
            return kExitFatal;
        }
        if (!args.init_force && !std::filesystem::is_empty(dir, ec)) {
            err << "error: target directory is not empty: " << pathToUtf8(dir)
                << " (pass --force to overwrite)\n";
            return kExitError;
        }
    }

    std::string gameId = args.init_id;
    if (gameId.empty()) {
        std::filesystem::path normDir = dir.lexically_normal();
        std::string dirName = normDir.filename().string();
        if (dirName.empty() || dirName == "." || dirName == "..") {
            dirName = normDir.parent_path().filename().string();
        }
        gameId = slugify(dirName);
        if (gameId.empty() || !expr::isValidSlugId(gameId)) {
            gameId = "my-game";
        }
    } else if (!expr::isValidSlugId(gameId)) {
        err << "error: invalid --id '" << gameId << "' (must match [a-z0-9_-]+)\n";
        return kExitUsage;
    }

    const std::string gameName = args.init_name.empty() ? gameId : args.init_name;
    const bool needsTps = (args.init_time == "tick" || args.init_rate == "tick" || args.init_tps.has_value());
    const double tps = args.init_tps.value_or(20.0);

    json units;
    units["recipe_time"] = args.init_time;
    if (needsTps) {
        if (std::floor(tps) == tps) {
            units["ticks_per_second"] = static_cast<int64_t>(tps);
        } else {
            units["ticks_per_second"] = tps;
        }
    }
    units["default_rate"] = args.init_rate;

    const double recipeTime = (args.init_time == "tick") ? (2.0 * tps) : 2.0;

    json manifest;
    manifest["schema_version"] = 4;
    manifest["game"] = {
        {"id", gameId},
        {"name", gameName},
        {"data_version", args.init_data_version}
    };
    manifest["units"] = std::move(units);
    manifest["resource_kinds"] = {
        {"item", {{"label", "Item"}, {"display_unit", ""}, {"display_scale", 1}}},
        {"fluid", {{"label", "Fluid"}, {"display_unit", "mL"}, {"display_scale", 1}}}
    };
    manifest["resources"] = json::array({
        {{"id", "iron-ore"}, {"name", "Iron Ore"}, {"kind", "item"}},
        {{"id", "iron-ingot"}, {"name", "Iron Ingot"}, {"kind", "item"}}
    });
    manifest["machines"] = json::array({
        {
            {"id", "smelter"},
            {"name", "Smelter"},
            {"profile", "default"},
            {"categories", json::array({"smelting"})},
            {"speed", 1}
        }
    });
    manifest["recipes"] = json::array({
        {
            {"id", "smelt-iron"},
            {"name", "Smelt Iron"},
            {"category", "smelting"},
            {"time", recipeTime},
            {"in", json::array({json::array({"iron-ore", 1})})},
            {"out", json::array({json::array({"iron-ingot", 1})})}
        }
    });

    std::vector<std::string> writtenFiles;
    std::string ioErr;

    const std::filesystem::path manifestPath = dir / "game.json";
    if (!atomicWriteFile(manifestPath, manifest.dump(2) + "\n", &ioErr)) {
        err << "error: failed to write " << pathToUtf8(manifestPath) << ": " << ioErr << "\n";
        return kExitFatal;
    }
    writtenFiles.push_back("game.json");

    const std::filesystem::path gitignorePath = dir / ".gitignore";
    const std::string gitignoreContent = "icons/\nicons.pak\n.tmp_icons/\n";
    if (!atomicWriteFile(gitignorePath, gitignoreContent, &ioErr)) {
        err << "error: failed to write " << pathToUtf8(gitignorePath) << ": " << ioErr << "\n";
        return kExitFatal;
    }
    writtenFiles.push_back(".gitignore");

    if (args.init_icons) {
        const std::string resourcePng = makePlaceholderPng(96, 152, 208, 255);
        const std::string machinePng = makePlaceholderPng(216, 152, 72, 255);

        const std::vector<std::pair<std::string, const std::string*>> iconFiles = {
            {"icons/resources/iron-ore.png", &resourcePng},
            {"icons/resources/iron-ingot.png", &resourcePng},
            {"icons/machines/smelter.png", &machinePng}
        };
        for (const auto& [relPath, bytes] : iconFiles) {
            const std::filesystem::path fullPath = dir / relPath;
            if (!atomicWriteFile(fullPath, *bytes, &ioErr)) {
                err << "error: failed to write " << pathToUtf8(fullPath) << ": " << ioErr << "\n";
                return kExitFatal;
            }
            writtenFiles.push_back(relPath);
        }
    }

    if (args.json_output) {
        json j;
        j["ok"] = true;
        j["dir"] = pathToUtf8(dir);
        j["game_id"] = gameId;
        j["files"] = writtenFiles;
        out << j.dump(2) << "\n";
    } else {
        out << "Initialized GameData v4 package '" << gameId << "' in " << pathToUtf8(dir) << "\n";
        for (const auto& f : writtenFiles) {
            out << "  " << f << "\n";
        }
    }

    return kExitOk;
}

} // namespace gamedata_tool
