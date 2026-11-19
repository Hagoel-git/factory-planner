#include "tools/gamedata_tool/Commands.h"

#include "common/FilesystemUtils.h"
#include "core/data/GameDataLoader.h"
#include "tools/gamedata_tool/IconToolUtils.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace gamedata_tool {

namespace {

using json = nlohmann::ordered_json;

} // namespace

int runPruneIcons(const Args& args, std::ostream& out, std::ostream& err) {
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
    std::error_code ec;

    if (!std::filesystem::is_directory(fromDir, ec)) {
        if (explicitFrom) {
            err << "error: --from directory does not exist: " << pathToUtf8(fromDir) << "\n";
            return kExitFatal;
        }
        if (args.json_output) {
            json doc;
            doc["package"] = args.target;
            doc["from"] = pathToUtf8(fromDir);
            doc["apply"] = args.apply;
            doc["unreferenced"] = json::array();
            doc["deleted"] = json::array();
            out << doc.dump(2) << "\n";
        } else {
            out << "No icon directory at " << pathToUtf8(fromDir) << "; 0 unreferenced PNGs.\n";
        }
        return kExitOk;
    }

    const std::vector<ReferencedIcon> refs = collectReferencedIcons(pkg);
    std::unordered_set<std::string> referencedRelPngs;
    referencedRelPngs.reserve(refs.size());
    for (const auto& ref : refs) {
        referencedRelPngs.insert(ref.rel_png);
    }

    const ScannedIconDir scanned = scanIconDirectory(fromDir);
    std::vector<std::string> unreferencedRel;
    std::vector<std::string> deletedRel;

    for (const auto& sf : scanned.files) {
        if (!sf.has_png_ext) {
            continue;
        }
        if (referencedRelPngs.count(sf.rel_path) > 0) {
            continue;
        }
        unreferencedRel.push_back(sf.rel_path);
        if (args.apply) {
            if (!std::filesystem::remove(sf.full_path, ec)) {
                err << "error: failed to remove '" << pathToUtf8(sf.full_path) << "': "
                    << ec.message() << "\n";
                return kExitFatal;
            }
            deletedRel.push_back(sf.rel_path);
        }
    }

    if (args.json_output) {
        json doc;
        doc["package"] = args.target;
        doc["from"] = pathToUtf8(fromDir);
        doc["apply"] = args.apply;
        doc["unreferenced"] = unreferencedRel;
        doc["deleted"] = deletedRel;
        out << doc.dump(2) << "\n";
        return kExitOk;
    }

    for (const auto& rel : unreferencedRel) {
        out << (args.apply ? "deleted: " : "would delete: ") << rel << "\n";
    }
    if (args.apply) {
        out << "Deleted " << deletedRel.size() << " unreferenced PNG"
            << (deletedRel.size() == 1 ? "" : "s") << ".\n";
    } else {
        out << "Dry run: " << unreferencedRel.size() << " unreferenced PNG"
            << (unreferencedRel.size() == 1 ? "" : "s")
            << " found (pass --apply to delete).\n";
    }

    return kExitOk;
}

} // namespace gamedata_tool
