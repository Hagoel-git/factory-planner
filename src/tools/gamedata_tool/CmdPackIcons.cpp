#include "tools/gamedata_tool/Commands.h"

#include "common/FilesystemUtils.h"
#include "core/data/GameDataLoader.h"
#include "core/data/IconPak.h"
#include "core/data/PngInfo.h"
#include "tools/gamedata_tool/IconToolUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace gamedata_tool {

namespace {

using json = nlohmann::ordered_json;

int runPackIconsList(const Args& args, std::ostream& out, std::ostream& err) {
    const std::filesystem::path pakPath = pathFromUtf8(args.list_pak);
    std::error_code ec;
    if (!std::filesystem::exists(pakPath, ec)) {
        err << "error: icons.pak not found: " << pathToUtf8(pakPath) << "\n";
        return kExitFatal;
    }

    gamedata::IconPakReader reader;
    std::string openErr;
    if (!gamedata::IconPakReader::open(pakPath, reader, &openErr)) {
        err << "error: failed to read '" << pathToUtf8(pakPath) << "': " << openErr << "\n";
        return kExitError;
    }

    if (args.json_output) {
        json doc;
        doc["file"] = pathToUtf8(pakPath);
        doc["version"] = reader.version();
        doc["count"] = reader.size();
        json entriesJson = json::array();
        for (const auto& entry : reader.entries()) {
            entriesJson.push_back({
                {"key", entry.key},
                {"name_slug", entry.name_slug},
                {"width", entry.width},
                {"height", entry.height},
                {"offset", entry.offset},
                {"size", entry.size}
            });
        }
        doc["entries"] = std::move(entriesJson);
        out << doc.dump(2) << "\n";
        return kExitOk;
    }

    out << pathToUtf8(pakPath) << ": " << reader.size()
        << (reader.size() == 1 ? " entry" : " entries")
        << " (version " << reader.version() << ")\n";
    for (const auto& entry : reader.entries()) {
        out << "  " << entry.key
            << "  " << entry.width << "x" << entry.height
            << "  " << entry.size << " B"
            << "  @" << entry.offset;
        if (!entry.name_slug.empty()) {
            out << "  [" << entry.name_slug << "]";
        }
        out << "\n";
    }
    return kExitOk;
}

bool readAllFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& outBytes, std::string& outErr) {
    std::error_code ec;
    const uint64_t fsize = std::filesystem::file_size(path, ec);
    if (ec) {
        outErr = "cannot stat '" + pathToUtf8(path) + "': " + ec.message();
        return false;
    }
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        outErr = "cannot open '" + pathToUtf8(path) + "' for reading";
        return false;
    }
    outBytes.resize(static_cast<size_t>(fsize));
    if (fsize > 0) {
        ifs.read(reinterpret_cast<char*>(outBytes.data()), static_cast<std::streamsize>(fsize));
        if (ifs.gcount() != static_cast<std::streamsize>(fsize)) {
            outErr = "failed reading '" + pathToUtf8(path) + "'";
            return false;
        }
    }
    return true;
}

} // namespace

int runPackIcons(const Args& args, std::ostream& out, std::ostream& err) {
    if (!args.list_pak.empty()) {
        return runPackIconsList(args, out, err);
    }

    const std::filesystem::path pkgPath = pathFromUtf8(args.target);
    gamedata::GamePackage pkg;
    gamedata::LoadReport report;
    if (!gamedata::loadGamePackage(pkgPath, pkg, report, /*ignoreErrors=*/false)) {
        if (!report.errors.empty()) {
            err << "error: " << report.errors.front().formatted << "\n";
        } else {
            err << "error: failed to load package '" << args.target << "'\n";
        }
        return report.fatal ? kExitFatal : kExitError;
    }

    const bool explicitFrom = !args.from_dir.empty();
    const std::filesystem::path fromDir = explicitFrom ? pathFromUtf8(args.from_dir) : pkg.icon_root;
    const std::filesystem::path outPakPath = !args.out_pak.empty()
        ? pathFromUtf8(args.out_pak)
        : (pkg.package_root / "icons.pak");

    std::error_code ec;
    if (!std::filesystem::is_directory(fromDir, ec)) {
        if (explicitFrom) {
            err << "error: --from directory does not exist: " << pathToUtf8(fromDir) << "\n";
            return kExitFatal;
        }
        const std::string note = "no icons found; icons.pak not written";
        if (args.json_output) {
            json doc;
            doc["package"] = args.target;
            doc["output"] = pathToUtf8(outPakPath);
            doc["written"] = false;
            doc["count"] = 0;
            doc["verified"] = false;
            doc["notes"] = json::array({note});
            out << doc.dump(2) << "\n";
        } else {
            out << "I " << note << "\n";
        }
        return kExitOk;
    }

    const ScannedIconDir scanned = scanIconDirectory(fromDir);
    const std::vector<ReferencedIcon> refs = collectReferencedIcons(pkg);

    struct MatchedIcon {
        ReferencedIcon ref;
        std::filesystem::path source_path;
        std::string rel_path;
        uint16_t width = 0;
        uint16_t height = 0;
    };

    std::vector<MatchedIcon> matched;
    std::vector<std::string> vIcon1Errors;

    for (const auto& ref : refs) {
        auto it = scanned.by_exact_rel.find(ref.rel_png);
        if (it == scanned.by_exact_rel.end()) {
            continue;
        }
        const ScannedFile& sf = scanned.files[it->second];
        const gamedata::PngParseResult png = gamedata::readPngInfoFromFile(sf.full_path);
        const std::string dispFile = (explicitFrom ? pathToUtf8(fromDir) : std::string("icons")) +
                                     "/" + sf.rel_path;
        if (!png.ok) {
            vIcon1Errors.push_back(formatVIcon1Message(dispFile, 0, 0, png.error));
            continue;
        }
        if (!gamedata::isValidIconDimensions(png.info.width, png.info.height)) {
            vIcon1Errors.push_back(formatVIcon1Message(dispFile, png.info.width, png.info.height, ""));
            continue;
        }

        MatchedIcon m;
        m.ref = ref;
        m.source_path = sf.full_path;
        m.rel_path = sf.rel_path;
        m.width = static_cast<uint16_t>(png.info.width);
        m.height = static_cast<uint16_t>(png.info.height);
        matched.push_back(std::move(m));
    }

    if (!vIcon1Errors.empty()) {
        if (args.json_output) {
            json doc;
            doc["package"] = args.target;
            doc["output"] = pathToUtf8(outPakPath);
            doc["written"] = false;
            doc["ok"] = false;
            doc["errors"] = vIcon1Errors;
            out << doc.dump(2) << "\n";
        } else {
            for (const auto& msg : vIcon1Errors) {
                err << "E V-ICON-1  " << msg << "\n";
            }
        }
        return kExitError;
    }

    if (matched.empty()) {
        const std::string note = "no icons found; icons.pak not written";
        if (args.json_output) {
            json doc;
            doc["package"] = args.target;
            doc["output"] = pathToUtf8(outPakPath);
            doc["written"] = false;
            doc["count"] = 0;
            doc["verified"] = false;
            doc["notes"] = json::array({note});
            out << doc.dump(2) << "\n";
        } else {
            out << "I " << note << "\n";
        }
        return kExitOk;
    }

    std::vector<gamedata::IconPakInput> inputs;
    inputs.reserve(matched.size());
    for (const auto& m : matched) {
        gamedata::IconPakInput inp;
        inp.key = m.ref.key;
        inp.name_slug = m.ref.name_slug;
        inp.source_path = m.source_path;
        inp.width = m.width;
        inp.height = m.height;
        inputs.push_back(std::move(inp));
    }

    std::string writeErr;
    if (!gamedata::IconPakWriter::write(outPakPath, std::move(inputs), &writeErr)) {
        err << "error: failed writing '" << pathToUtf8(outPakPath) << "': " << writeErr << "\n";
        return kExitFatal;
    }

    if (args.verify) {
        gamedata::IconPakReader reader;
        std::string openErr;
        if (!gamedata::IconPakReader::open(outPakPath, reader, &openErr)) {
            err << "error: verify failed opening '" << pathToUtf8(outPakPath) << "': " << openErr << "\n";
            return kExitError;
        }
        if (reader.size() != matched.size()) {
            err << "error: verify entry count mismatch (expected " << matched.size()
                << ", got " << reader.size() << ")\n";
            return kExitError;
        }
        std::ifstream stream = reader.openStream();
        if (!stream) {
            err << "error: verify failed opening stream for '" << pathToUtf8(outPakPath) << "'\n";
            return kExitFatal;
        }
        std::vector<uint8_t> srcBytes;
        std::string readErr;
        for (const auto& m : matched) {
            const gamedata::IconPakEntry* entry = reader.find(m.ref.key);
            if (entry == nullptr) {
                err << "error: verify missing key '" << m.ref.key << "' in packed output\n";
                return kExitError;
            }
            if (entry->name_slug != m.ref.name_slug ||
                entry->width != m.width || entry->height != m.height) {
                err << "error: verify metadata mismatch for key '" << m.ref.key << "'\n";
                return kExitError;
            }
            const auto blob = reader.readBlob(*entry, stream, /*validatePng=*/true);
            if (!blob.has_value()) {
                err << "error: verify failed reading blob for key '" << m.ref.key << "'\n";
                return kExitError;
            }
            if (!readAllFileBytes(m.source_path, srcBytes, readErr)) {
                err << "error: verify failed: " << readErr << "\n";
                return kExitFatal;
            }
            if (*blob != srcBytes) {
                err << "error: verify byte-for-byte mismatch for key '" << m.ref.key << "'\n";
                return kExitError;
            }
        }
    }

    const uint64_t pakBytes = std::filesystem::file_size(outPakPath, ec);
    if (args.json_output) {
        json doc;
        doc["package"] = args.target;
        doc["output"] = pathToUtf8(outPakPath);
        doc["written"] = true;
        doc["ok"] = true;
        doc["count"] = matched.size();
        doc["bytes"] = pakBytes;
        doc["verified"] = args.verify;
        doc["notes"] = json::array();
        out << doc.dump(2) << "\n";
    } else {
        out << "Packed " << matched.size() << (matched.size() == 1 ? " icon" : " icons")
            << " into " << pathToUtf8(outPakPath)
            << " (" << formatCountWithCommas(pakBytes) << " bytes)"
            << (args.verify ? " [verified]" : "") << "\n";
    }

    return kExitOk;
}

} // namespace gamedata_tool
