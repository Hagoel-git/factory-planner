#include "tools/gamedata_tool/Commands.h"

#include "common/FilesystemUtils.h"
#include "core/data/GameDataLoader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace gamedata_tool {

namespace {

using json = nlohmann::ordered_json;

double medianOf(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    if (n % 2 == 1) {
        return values[n / 2];
    }
    return 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

double roundMs(double ms) {
    return std::round(ms * 100.0) / 100.0;
}

std::string formatWithCommas(uint64_t value) {
    std::string raw = std::to_string(value);
    std::string out;
    out.reserve(raw.size() + raw.size() / 3);
    for (size_t i = 0; i < raw.size(); ++i) {
        if (i > 0 && (raw.size() - i) % 3 == 0) {
            out.push_back(',');
        }
        out.push_back(raw[i]);
    }
    return out;
}

struct IconCoverage {
    std::string mode = "none"; // "none" | "loose" | "pak"
    size_t resources_present = 0;
    size_t resources_total = 0;
    size_t machines_present = 0;
    size_t machines_total = 0;
    size_t options_present = 0;
    size_t options_total = 0;
};

// Isolated icon coverage helper: in Task 4a checks pak presence and loose PNGs under icon_root.
// Task 4b will extend this to read IconPak indices in T8 order.
IconCoverage computeIconCoverage(const gamedata::GamePackage& pkg) {
    IconCoverage cov;
    cov.resources_total = pkg.resources.size();
    cov.machines_total = pkg.machines.size();
    for (const auto& oset : pkg.option_sets) {
        cov.options_total += oset.options.size();
    }

    std::error_code ec;
    const bool hasRootPak = !pkg.package_root.empty() &&
        std::filesystem::is_regular_file(pkg.package_root / "icons.pak", ec);
    const bool hasIconDirPak = !pkg.icon_root.empty() &&
        std::filesystem::is_regular_file(pkg.icon_root / "icons.pak", ec);
    const bool hasLooseDir = !pkg.icon_root.empty() &&
        std::filesystem::is_directory(pkg.icon_root, ec);

    if (hasRootPak || hasIconDirPak) {
        cov.mode = "pak";
        return cov;
    }
    if (!hasLooseDir) {
        cov.mode = "none";
        return cov;
    }

    cov.mode = "loose";
    for (const auto& r : pkg.resources) {
        if (std::filesystem::is_regular_file(pkg.icon_root / "resources" / (r.id + ".png"), ec)) {
            ++cov.resources_present;
        }
    }
    for (const auto& m : pkg.machines) {
        if (std::filesystem::is_regular_file(pkg.icon_root / "machines" / (m.id + ".png"), ec)) {
            ++cov.machines_present;
        }
    }
    for (const auto& oset : pkg.option_sets) {
        for (const auto& opt : oset.options) {
            if (std::filesystem::is_regular_file(
                    pkg.icon_root / "options" / oset.id / (opt.key + ".png"), ec)) {
                ++cov.options_present;
            }
        }
    }
    return cov;
}

std::vector<std::pair<std::string, size_t>> sortedByCountDesc(const std::map<std::string, size_t>& m) {
    std::vector<std::pair<std::string, size_t>> vec(m.begin(), m.end());
    std::sort(vec.begin(), vec.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    return vec;
}

} // namespace

int runStats(const Args& args, std::ostream& out, std::ostream& err) {
    const std::filesystem::path pkgPath = pathFromUtf8(args.target);
    const int repeat = std::max(1, args.repeat);

    gamedata::GamePackage pkg;
    gamedata::LoadReport lastReport;
    std::vector<double> totalSamples;
    std::vector<double> parseSamples;
    std::vector<double> compileSamples;
    totalSamples.reserve(static_cast<size_t>(repeat));
    parseSamples.reserve(static_cast<size_t>(repeat));
    compileSamples.reserve(static_cast<size_t>(repeat));

    for (int i = 0; i < repeat; ++i) {
        gamedata::GamePackage runPkg;
        gamedata::LoadReport runReport;
        if (!gamedata::loadGamePackage(pkgPath, runPkg, runReport, args.ignore_errors)) {
            if (!runReport.errors.empty()) {
                err << "error: failed to load package: " << runReport.errors.front().formatted << "\n";
            } else {
                err << "error: failed to load package '" << args.target << "'\n";
            }
            return runReport.fatal ? kExitFatal : kExitError;
        }
        totalSamples.push_back(runReport.timing_ms.total);
        parseSamples.push_back(runReport.timing_ms.parse);
        compileSamples.push_back(runReport.timing_ms.compile);
        pkg = std::move(runPkg);
        lastReport = std::move(runReport);
    }

    const double medianTotalMs = medianOf(totalSamples);
    const double medianParseMs = medianOf(parseSamples);
    const double medianCompileMs = medianOf(compileSamples);

    // Distributions (deterministic std::map ordering)
    std::map<std::string, size_t> recipesByCategory;
    std::map<std::string, size_t> recipesByMachine;
    std::map<std::string, size_t> recipesByProfile;

    for (const auto& r : pkg.recipes) {
        const std::string catKey = r.category.has_value() ? *r.category : "(explicit)";
        ++recipesByCategory[catKey];
        for (const auto& mid : r.compatible_machine_ids) {
            ++recipesByMachine[mid];
        }
        for (const auto& pid : r.compatible_profile_ids) {
            ++recipesByProfile[pid];
        }
    }

    // Parameters, metrics, and profiles in use
    std::set<std::string> profilesInUse;
    for (const auto& m : pkg.machines) {
        profilesInUse.insert(m.profile);
    }
    std::set<std::string> parametersInUse;
    std::set<std::string> metricsInUse;
    for (const auto& prof : pkg.profiles) {
        if (!profilesInUse.count(prof.id)) continue;
        for (const auto& pid : prof.params) {
            parametersInUse.insert(pid);
        }
        for (const auto& mid : prof.metric_order) {
            metricsInUse.insert(mid);
        }
    }
    for (const auto& r : pkg.recipes) {
        for (const auto& mid : r.overrides.metric_order) {
            metricsInUse.insert(mid);
        }
        for (const auto& p : r.in) {
            if (p.resource_param.has_value()) parametersInUse.insert(*p.resource_param);
        }
        for (const auto& p : r.out) {
            if (p.resource_param.has_value()) parametersInUse.insert(*p.resource_param);
        }
    }

    // File byte totals & recipes per shard file (sorted deterministically)
    std::vector<gamedata::LoadedFileInfo> files = lastReport.loaded_files;
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.path < b.path;
    });
    uint64_t totalJsonBytes = 0;
    for (const auto& f : files) {
        totalJsonBytes += f.bytes;
    }

    const IconCoverage icons = computeIconCoverage(pkg);

    if (args.json_output) {
        json doc;
        doc["package"] = args.target;
        doc["game"] = {
            {"id", pkg.game.id},
            {"name", pkg.game.name},
            {"data_version", pkg.game.data_version}
        };
        doc["counts"] = {
            {"files", files.size()},
            {"resource_kinds", pkg.resource_kinds.size()},
            {"resources", pkg.resources.size()},
            {"option_sets", pkg.option_sets.size()},
            {"tables", pkg.tables.size()},
            {"globals", pkg.globals.size()},
            {"parameters", pkg.parameters.size()},
            {"metrics", pkg.metrics.size()},
            {"profiles", pkg.profiles.size()},
            {"machines", pkg.machines.size()},
            {"recipes", pkg.recipes.size()}
        };
        doc["json_bytes"] = totalJsonBytes;

        json filesJson = json::array();
        for (const auto& f : files) {
            filesJson.push_back({
                {"path", f.path},
                {"bytes", f.bytes},
                {"recipes", f.recipes}
            });
        }
        doc["files"] = std::move(filesJson);

        auto mapToJson = [](const std::map<std::string, size_t>& m) {
            json obj = json::object();
            for (const auto& [k, v] : m) {
                obj[k] = v;
            }
            return obj;
        };
        doc["recipes_by_category"] = mapToJson(recipesByCategory);
        doc["recipes_by_machine"] = mapToJson(recipesByMachine);
        doc["recipes_by_profile"] = mapToJson(recipesByProfile);

        doc["in_use"] = {
            {"profiles", std::vector<std::string>(profilesInUse.begin(), profilesInUse.end())},
            {"parameters", std::vector<std::string>(parametersInUse.begin(), parametersInUse.end())},
            {"metrics", std::vector<std::string>(metricsInUse.begin(), metricsInUse.end())}
        };

        doc["icons"] = {
            {"mode", icons.mode},
            {"resources", {{"present", icons.resources_present}, {"total", icons.resources_total}}},
            {"machines", {{"present", icons.machines_present}, {"total", icons.machines_total}}},
            {"options", {{"present", icons.options_present}, {"total", icons.options_total}}}
        };

        doc["timing_ms"] = {
            {"repeat", repeat},
            {"total", roundMs(medianTotalMs)},
            {"parse", roundMs(medianParseMs)},
            {"compile", roundMs(medianCompileMs)}
        };

        out << doc.dump(2) << "\n";
        return kExitOk;
    }

    // Human output
    out << "Package: " << pkg.game.name << " (" << pkg.game.id;
    if (!pkg.game.data_version.empty()) {
        out << " " << pkg.game.data_version;
    }
    out << ")\n";
    out << "JSON:    " << formatWithCommas(totalJsonBytes) << " bytes across "
        << files.size() << (files.size() == 1 ? " file" : " files") << "\n";

    out << "Timing";
    if (repeat > 1) {
        out << " (median of " << repeat << " loads)";
    }
    out << ": " << std::fixed << std::setprecision(2)
        << medianTotalMs << " ms total"
        << " (parse " << medianParseMs << " ms, compile " << medianCompileMs << " ms)\n";

    out << "\nEntities:\n";
    out << "  resource_kinds: " << formatWithCommas(pkg.resource_kinds.size()) << "\n";
    out << "  resources:      " << formatWithCommas(pkg.resources.size()) << "\n";
    out << "  option_sets:    " << formatWithCommas(pkg.option_sets.size()) << "\n";
    out << "  tables:         " << formatWithCommas(pkg.tables.size()) << "\n";
    out << "  globals:        " << formatWithCommas(pkg.globals.size()) << "\n";
    out << "  parameters:     " << formatWithCommas(pkg.parameters.size())
        << " (" << parametersInUse.size() << " in use)\n";
    out << "  metrics:        " << formatWithCommas(pkg.metrics.size())
        << " (" << metricsInUse.size() << " in use)\n";
    out << "  profiles:       " << formatWithCommas(pkg.profiles.size())
        << " (" << profilesInUse.size() << " in use)\n";
    out << "  machines:       " << formatWithCommas(pkg.machines.size()) << "\n";
    out << "  recipes:        " << formatWithCommas(pkg.recipes.size()) << "\n";

    auto printTopSection = [&](const char* title, const std::map<std::string, size_t>& dist, size_t cap = 15) {
        if (dist.empty()) return;
        out << "\n" << title << " (" << dist.size() << "):\n";
        auto sorted = sortedByCountDesc(dist);
        const size_t show = std::min(sorted.size(), cap);
        for (size_t i = 0; i < show; ++i) {
            out << "  " << std::left << std::setw(28) << sorted[i].first << " "
                << formatWithCommas(sorted[i].second) << "\n";
        }
        if (sorted.size() > show) {
            out << "  ... " << (sorted.size() - show) << " more\n";
        }
    };

    printTopSection("Recipes by category", recipesByCategory);
    printTopSection("Recipes by profile", recipesByProfile);
    printTopSection("Recipes by machine", recipesByMachine);

    out << "\nFiles:\n";
    for (const auto& f : files) {
        out << "  " << std::left << std::setw(32) << f.path << " "
            << formatWithCommas(f.bytes) << " B, "
            << formatWithCommas(f.recipes) << " recipes\n";
    }

    out << "\nIcons (mode: " << icons.mode << "):\n";
    if (icons.mode == "loose") {
        out << "  resources: " << icons.resources_present << "/" << icons.resources_total << "\n";
        out << "  machines:  " << icons.machines_present << "/" << icons.machines_total << "\n";
        if (icons.options_total > 0) {
            out << "  options:   " << icons.options_present << "/" << icons.options_total << "\n";
        }
    } else if (icons.mode == "none") {
        out << "  none (using fallback icons)\n";
    } else {
        out << "  icons.pak detected\n";
    }

    return kExitOk;
}

} // namespace gamedata_tool
