#include "tools/gamedata_tool/Commands.h"

#include "common/FilesystemUtils.h"
#include "core/data/GameDataLoader.h"
#include "core/data/PngInfo.h"
#include "tools/gamedata_tool/IconToolUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace gamedata_tool {

namespace {

using json = nlohmann::ordered_json;

struct CaseMismatch {
    std::string expected;
    std::string actual;
};

struct InvalidIcon {
    std::string file;
    std::string key;
    std::string message;
};

} // namespace

int runCheckIcons(const Args& args, std::ostream& out, std::ostream& err) {
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

    if (explicitFrom && !std::filesystem::is_directory(fromDir, ec)) {
        err << "error: --from directory does not exist: " << pathToUtf8(fromDir) << "\n";
        return kExitFatal;
    }

    IconSources sources;
    std::vector<gamedata::LoadDiagnostic> pakOpenErrors;
    openIconSources(pkg, sources, pakOpenErrors);

    if (!explicitFrom && sources.noIconsMode()) {
        const std::string note = makeNoIconsNote(pkg);
        if (args.json_output) {
            json doc;
            doc["package"] = args.target;
            doc["game_id"] = pkg.game.id;
            doc["mode"] = "none";
            doc["from"] = pathToUtf8(fromDir);
            doc["ok"] = true;
            doc["counts"] = {
                {"referenced", collectReferencedIcons(pkg).size()},
                {"present", 0},
                {"missing", 0},
                {"invalid", 0},
                {"unreferenced", 0},
                {"case_mismatches", 0}
            };
            doc["missing"] = json::array();
            doc["invalid"] = json::array();
            doc["unreferenced"] = json::array();
            doc["case_mismatches"] = json::array();
            doc["pak_stale"] = false;
            doc["pak_stale_reasons"] = json::array();
            doc["notes"] = json::array({note});
            out << doc.dump(2) << "\n";
        } else {
            out << "I " << note << "\n";
        }
        return kExitOk;
    }

    const std::vector<ReferencedIcon> refs = collectReferencedIcons(pkg);

    const bool scanLoose = explicitFrom || sources.has_loose_dir;
    ScannedIconDir scanned;
    if (scanLoose) {
        scanned = scanIconDirectory(fromDir);
    }

    std::vector<bool> matchedLoose(scanned.files.size(), false);
    std::vector<std::string> missing;
    std::vector<InvalidIcon> invalid;
    std::vector<CaseMismatch> caseMismatches;
    std::vector<std::string> unreferenced;
    std::vector<std::string> pakErrors;
    size_t presentCount = 0;

    for (const auto& d : pakOpenErrors) {
        pakErrors.push_back(d.message);
        invalid.push_back(InvalidIcon{d.file, d.subject, d.message});
    }

    const std::string loosePrefix = explicitFrom
        ? pathToUtf8(fromDir)
        : "icons";

    auto checkLooseFile = [&](size_t idx, const ReferencedIcon& ref, bool countPresent) {
        matchedLoose[idx] = true;
        const ScannedFile& sf = scanned.files[idx];
        const std::string dispFile = loosePrefix + "/" + sf.rel_path;
        const gamedata::PngParseResult png = gamedata::readPngInfoFromFile(sf.full_path);
        if (!png.ok) {
            invalid.push_back(InvalidIcon{
                dispFile,
                ref.key,
                formatVIcon1Message(dispFile, 0, 0, png.error)
            });
        } else if (!gamedata::isValidIconDimensions(png.info.width, png.info.height)) {
            invalid.push_back(InvalidIcon{
                dispFile,
                ref.key,
                formatVIcon1Message(dispFile, png.info.width, png.info.height, "")
            });
        } else if (countPresent) {
            ++presentCount;
        }
    };

    for (const auto& ref : refs) {
        if (explicitFrom) {
            auto exactIt = scanned.by_exact_rel.find(ref.rel_png);
            if (exactIt != scanned.by_exact_rel.end()) {
                checkLooseFile(exactIt->second, ref, true);
                continue;
            }
            auto lowerIt = scanned.by_lower_rel.find(toAsciiLower(ref.rel_png));
            if (lowerIt != scanned.by_lower_rel.end()) {
                const size_t idx = lowerIt->second;
                matchedLoose[idx] = true;
                caseMismatches.push_back(CaseMismatch{ref.rel_png, scanned.files[idx].rel_path});
                // Also validate PNG constraints on the mismatched file
                checkLooseFile(idx, ref, false);
                continue;
            }
            missing.push_back(ref.key);
            continue;
        }

        // Default mode (no --from): resolve via T8 order, and also check loose dir if present.
        bool resolved = false;
        if (sources.root_pak.has_value()) {
            if (const gamedata::IconPakEntry* entry = sources.root_pak->find(ref.key)) {
                resolved = true;
                if (!gamedata::isValidIconDimensions(entry->width, entry->height)) {
                    invalid.push_back(InvalidIcon{
                        sources.root_pak_display,
                        ref.key,
                        formatVIcon1Message(ref.key, entry->width, entry->height, "")
                    });
                } else {
                    ++presentCount;
                }
            }
        }
        if (!resolved && sources.icon_pak.has_value()) {
            if (const gamedata::IconPakEntry* entry = sources.icon_pak->find(ref.key)) {
                resolved = true;
                if (!gamedata::isValidIconDimensions(entry->width, entry->height)) {
                    invalid.push_back(InvalidIcon{
                        sources.icon_pak_display,
                        ref.key,
                        formatVIcon1Message(ref.key, entry->width, entry->height, "")
                    });
                } else {
                    ++presentCount;
                }
            }
        }

        if (scanLoose) {
            auto exactIt = scanned.by_exact_rel.find(ref.rel_png);
            if (exactIt != scanned.by_exact_rel.end()) {
                if (!resolved) {
                    checkLooseFile(exactIt->second, ref, true);
                    resolved = true;
                } else {
                    matchedLoose[exactIt->second] = true;
                }
            } else {
                auto lowerIt = scanned.by_lower_rel.find(toAsciiLower(ref.rel_png));
                if (lowerIt != scanned.by_lower_rel.end()) {
                    matchedLoose[lowerIt->second] = true;
                    caseMismatches.push_back(CaseMismatch{ref.rel_png, scanned.files[lowerIt->second].rel_path});
                    if (!resolved) {
                        checkLooseFile(lowerIt->second, ref, false);
                        resolved = true;
                    }
                }
            }
        }

        if (!resolved) {
            missing.push_back(ref.key);
        }
    }

    if (scanLoose) {
        for (size_t i = 0; i < scanned.files.size(); ++i) {
            if (!matchedLoose[i]) {
                unreferenced.push_back(scanned.files[i].rel_path);
            }
        }
    }

    // Stale icons.pak check (missing entries present in loose/from dir, or older than newest file)
    bool pakStale = false;
    std::vector<std::string> pakStaleReasons;
    std::filesystem::path activePakPath;
    if (const gamedata::IconPakReader* activePak = sources.primaryPak(&activePakPath);
        activePak != nullptr && scanLoose) {
        size_t missingFromPak = 0;
        for (const auto& ref : refs) {
            if (scanned.by_exact_rel.count(ref.rel_png) > 0 && !activePak->contains(ref.key)) {
                ++missingFromPak;
            }
        }
        if (missingFromPak > 0) {
            pakStale = true;
            pakStaleReasons.push_back(
                "icons.pak is missing " + std::to_string(missingFromPak) +
                (missingFromPak == 1 ? " entry" : " entries") + " present in source directory");
        }
        if (scanned.newest_mtime.has_value()) {
            const auto pakMtime = std::filesystem::last_write_time(activePakPath, ec);
            if (!ec && *scanned.newest_mtime > pakMtime) {
                pakStale = true;
                pakStaleReasons.push_back(
                    "icons.pak is older than newest file in " + pathToUtf8(fromDir));
            }
        }
    }

    std::sort(missing.begin(), missing.end());
    std::sort(unreferenced.begin(), unreferenced.end());
    std::sort(caseMismatches.begin(), caseMismatches.end(), [](const CaseMismatch& a, const CaseMismatch& b) {
        return a.expected < b.expected;
    });
    std::sort(invalid.begin(), invalid.end(), [](const InvalidIcon& a, const InvalidIcon& b) {
        if (a.key != b.key) return a.key < b.key;
        return a.file < b.file;
    });

    const bool hasErrors = !invalid.empty();
    const bool hasWarnings =
        !missing.empty() || !unreferenced.empty() || !caseMismatches.empty() || pakStale;
    const int exitCode = (hasErrors || (args.werror && hasWarnings)) ? kExitError : kExitOk;
    const std::string mode = explicitFrom
        ? "loose"
        : ((sources.has_root_pak || sources.has_icon_pak) ? "pak" : "loose");

    if (args.json_output) {
        json doc;
        doc["package"] = args.target;
        doc["game_id"] = pkg.game.id;
        doc["mode"] = mode;
        doc["from"] = pathToUtf8(fromDir);
        doc["ok"] = (exitCode == kExitOk);
        doc["counts"] = {
            {"referenced", refs.size()},
            {"present", presentCount},
            {"missing", missing.size()},
            {"invalid", invalid.size()},
            {"unreferenced", unreferenced.size()},
            {"case_mismatches", caseMismatches.size()}
        };
        doc["missing"] = missing;

        json invArr = json::array();
        for (const auto& inv : invalid) {
            invArr.push_back({
                {"file", inv.file},
                {"key", inv.key},
                {"message", inv.message}
            });
        }
        doc["invalid"] = std::move(invArr);
        doc["unreferenced"] = unreferenced;

        json cmArr = json::array();
        for (const auto& cm : caseMismatches) {
            cmArr.push_back({
                {"expected", cm.expected},
                {"actual", cm.actual}
            });
        }
        doc["case_mismatches"] = std::move(cmArr);
        doc["pak_stale"] = pakStale;
        doc["pak_stale_reasons"] = pakStaleReasons;
        doc["notes"] = json::array();
        out << doc.dump(2) << "\n";
        return exitCode;
    }

    for (const auto& inv : invalid) {
        out << "E V-ICON-1  " << inv.message << "\n";
    }
    for (const auto& cm : caseMismatches) {
        out << "W CASE      expected '" << cm.expected << "', found '" << cm.actual << "'\n";
    }
    if (!missing.empty()) {
        out << "W V-W-1     " << formatCountWithCommas(missing.size())
            << " missing " << (missing.size() == 1 ? "icon" : "icons");
        const size_t show = std::min<size_t>(missing.size(), 5);
        out << " (e.g. ";
        for (size_t i = 0; i < show; ++i) {
            if (i > 0) out << ", ";
            out << missing[i];
        }
        if (missing.size() > show) out << ", ...";
        out << ")\n";
    }
    if (!unreferenced.empty()) {
        out << "W UNREF     " << formatCountWithCommas(unreferenced.size())
            << " unreferenced " << (unreferenced.size() == 1 ? "file" : "files");
        const size_t show = std::min<size_t>(unreferenced.size(), 5);
        out << " (e.g. ";
        for (size_t i = 0; i < show; ++i) {
            if (i > 0) out << ", ";
            out << unreferenced[i];
        }
        if (unreferenced.size() > show) out << ", ...";
        out << ")\n";
    }
    if (pakStale) {
        for (const auto& reason : pakStaleReasons) {
            out << "W STALE-PAK " << reason << "\n";
        }
    }

    out << "Icons (" << mode << "): " << presentCount << "/" << refs.size() << " present, "
        << missing.size() << " missing, "
        << invalid.size() << " invalid, "
        << unreferenced.size() << " unreferenced, "
        << caseMismatches.size() << " case mismatches"
        << (pakStale ? ", icons.pak stale" : "")
        << ". Exit " << exitCode << ".\n";

    return exitCode;
}

} // namespace gamedata_tool
