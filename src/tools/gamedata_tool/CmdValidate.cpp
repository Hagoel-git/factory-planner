#include "tools/gamedata_tool/Commands.h"

#include "common/FilesystemUtils.h"
#include "core/data/GameDataLoader.h"
#include "core/data/ReportGrouping.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace gamedata_tool {

namespace {

using json = nlohmann::ordered_json;

std::string severityToString(gamedata::DiagnosticSeverity sev) {
    switch (sev) {
        case gamedata::DiagnosticSeverity::Fatal:
            return "fatal";
        case gamedata::DiagnosticSeverity::Error:
            return "error";
        case gamedata::DiagnosticSeverity::Warning:
            return "warning";
        case gamedata::DiagnosticSeverity::Info:
            return "info";
    }
    return "error";
}

char severityBadge(gamedata::DiagnosticSeverity sev) {
    switch (sev) {
        case gamedata::DiagnosticSeverity::Fatal:
            return 'F';
        case gamedata::DiagnosticSeverity::Error:
            return 'E';
        case gamedata::DiagnosticSeverity::Warning:
            return 'W';
        case gamedata::DiagnosticSeverity::Info:
            return 'I';
    }
    return 'E';
}

std::string formatWithCommas(size_t value) {
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

double roundMs(double ms) {
    return std::round(ms * 100.0) / 100.0;
}

std::string formatExampleLoc(const gamedata::DiagnosticExample& ex) {
    std::ostringstream oss;
    if (!ex.file.empty()) {
        oss << ex.file;
    }
    if (!ex.pointer.empty()) {
        if (!ex.file.empty()) oss << ": ";
        oss << ex.pointer;
    }
    if (ex.line > 0) {
        if (!ex.pointer.empty()) {
            oss << " " << ex.line << ":" << ex.col;
        } else if (!ex.file.empty()) {
            oss << ":" << ex.line << ":" << ex.col;
        }
    }
    return oss.str();
}

void printCascadeLine(const gamedata::CascadeSummary& cas, std::ostream& out) {
    size_t colon = cas.root.find(':');
    std::string rootKind = (colon != std::string::npos) ? cas.root.substr(0, colon) : "";
    std::string rootId = (colon != std::string::npos) ? cas.root.substr(colon + 1) : cas.root;

    out << "  \xe2\x94\x94\xe2\x94\x80 dropped: ";
    if (!rootKind.empty()) {
        out << rootKind << " " << rootId;
    } else {
        out << rootId;
    }

    std::vector<std::pair<std::string, size_t>> stages(cas.dropped.begin(), cas.dropped.end());
    std::sort(stages.begin(), stages.end(), [](const auto& a, const auto& b) {
        int ra = gamedata::entityKindStageRank(a.first);
        int rb = gamedata::entityKindStageRank(b.first);
        if (ra != rb) return ra < rb;
        return a.first < b.first;
    });

    for (const auto& [kind, cnt] : stages) {
        out << " \xe2\x86\x92 " << formatWithCommas(cnt) << " "
            << gamedata::formatEntityKindCount(kind, cnt);
    }

    if (!cas.sample_ids.empty()) {
        out << "  (e.g. ";
        for (size_t i = 0; i < cas.sample_ids.size(); ++i) {
            if (i > 0) out << ", ";
            out << cas.sample_ids[i];
        }
        if (cas.total_dropped > cas.sample_ids.size()) {
            out << ", \xe2\x80\xa6";
        }
        out << ")";
    }
    out << "\n";
}

bool groupMatchesCascade(
    const gamedata::DiagnosticGroup& group,
    const gamedata::CascadeSummary& cas) {
    for (const auto& ex : group.examples) {
        gamedata::LoadDiagnostic probe;
        probe.entity = ex.entity;
        probe.json_pointer = ex.pointer;
        probe.subject = ex.subject;
        if (gamedata::diagnosticMatchesRootEntity(probe, cas.root)) {
            return true;
        }
    }
    return false;
}

bool hasAnyIconSource(const gamedata::GamePackage& pkg) {
    std::error_code ec;
    if (!pkg.package_root.empty() &&
        std::filesystem::is_regular_file(pkg.package_root / "icons.pak", ec)) {
        return true;
    }
    if (!pkg.icon_root.empty()) {
        if (std::filesystem::is_regular_file(pkg.icon_root / "icons.pak", ec)) {
            return true;
        }
        if (std::filesystem::is_directory(pkg.icon_root, ec)) {
            return true;
        }
    }
    return false;
}

} // namespace

int runValidate(const Args& args, std::ostream& out, std::ostream& err) {
    (void)err;
    const std::filesystem::path pkgPath = pathFromUtf8(args.target);

    gamedata::GamePackage pkg;
    gamedata::LoadReport report;
    const bool loaded = gamedata::loadGamePackage(pkgPath, pkg, report, args.ignore_errors);

    // "No icons" mode check (T9, §2.2). Detailed IconPak/PngInfo/V-ICON-1/V-W-1 checks are Task 4b.
    if (!args.no_icons && loaded && !report.fatal && !hasAnyIconSource(pkg)) {
        report.addNote(
            "package has no icons: " + formatWithCommas(pkg.resources.size()) +
            " resources and " + formatWithCommas(pkg.machines.size()) +
            " machines use fallback icons");
    }

    gamedata::GroupingOptions groupOpts;
    groupOpts.max_examples = args.examples;
    const gamedata::GroupedReport grouped = gamedata::groupLoadReport(report, groupOpts);

    int exitCode = kExitOk;
    if (report.fatal) {
        exitCode = kExitFatal;
    } else if (!report.errors.empty() || (args.werror && !report.warnings.empty())) {
        exitCode = kExitError;
    }

    const bool okFlag = (exitCode == kExitOk);
    const std::string gameId = !pkg.game.id.empty() ? pkg.game.id : report.game_id;
    const size_t emittedGroups = std::min(grouped.groups.size(), args.max_groups);

    if (args.json_output) {
        json doc;
        doc["report_version"] = 1;
        doc["package"] = args.target;
        doc["game_id"] = gameId;
        doc["ok"] = okFlag;
        doc["fatal"] = report.fatal;
        doc["degraded"] = report.degraded;
        doc["counts"] = {
            {"errors", grouped.error_count},
            {"warnings", grouped.warning_count},
            {"notes", grouped.note_count},
            {"dropped", grouped.dropped_count}
        };

        json groupsJson = json::array();
        for (size_t i = 0; i < emittedGroups; ++i) {
            const auto& g = grouped.groups[i];
            json gj;
            gj["severity"] = severityToString(g.severity);
            gj["rule"] = g.rule_id;
            if (g.subject.has_value()) {
                gj["subject"] = *g.subject;
            } else {
                gj["subject"] = nullptr;
            }
            if (g.pattern.has_value()) {
                gj["pattern"] = *g.pattern;
            } else {
                gj["pattern"] = nullptr;
            }
            gj["count"] = g.count;
            if (g.suggestion.has_value()) {
                gj["suggestion"] = *g.suggestion;
            } else {
                gj["suggestion"] = nullptr;
            }

            json exArr = json::array();
            for (const auto& ex : g.examples) {
                exArr.push_back({
                    {"file", ex.file},
                    {"pointer", ex.pointer},
                    {"line", ex.line},
                    {"col", ex.col},
                    {"message", ex.message}
                });
            }
            gj["examples"] = std::move(exArr);
            groupsJson.push_back(std::move(gj));
        }
        doc["groups"] = std::move(groupsJson);

        json cascadesJson = json::array();
        for (const auto& cas : grouped.cascades) {
            json cj;
            cj["root"] = cas.root;
            cj["rule"] = cas.rule_id;
            json dropObj = json::object();
            for (const auto& [kind, cnt] : cas.dropped) {
                dropObj[kind] = cnt;
            }
            cj["dropped"] = std::move(dropObj);
            cj["sample_ids"] = cas.sample_ids;
            cascadesJson.push_back(std::move(cj));
        }
        doc["cascades"] = std::move(cascadesJson);

        doc["notes"] = grouped.notes;
        doc["timing_ms"] = {
            {"load", roundMs(report.timing_ms.total)},
            {"parse", roundMs(report.timing_ms.parse)},
            {"compile", roundMs(report.timing_ms.compile)}
        };

        if (args.show_all) {
            json diags = json::array();
            auto appendList = [&](const std::vector<gamedata::LoadDiagnostic>& list) {
                for (const auto& d : list) {
                    json dj;
                    dj["severity"] = severityToString(d.severity);
                    dj["rule"] = d.rule_id;
                    dj["file"] = d.file;
                    dj["pointer"] = d.json_pointer;
                    dj["line"] = d.line;
                    dj["col"] = d.column;
                    if (!d.subject.empty()) {
                        dj["subject"] = d.subject;
                    } else {
                        dj["subject"] = nullptr;
                    }
                    dj["message"] = d.message;
                    diags.push_back(std::move(dj));
                }
            };
            appendList(report.errors);
            appendList(report.warnings);
            doc["diagnostics"] = std::move(diags);
        }

        out << doc.dump(2) << "\n";
        return exitCode;
    }

    // Human-readable output (§2.3)
    if (args.show_all) {
        auto printFlat = [&](const std::vector<gamedata::LoadDiagnostic>& list) {
            for (const auto& d : list) {
                out << severityBadge(d.severity) << " " << d.rule_id << "  "
                    << (d.formatted.empty() ? d.message : d.formatted) << "\n";
            }
        };
        printFlat(report.errors);
        printFlat(report.warnings);
    } else {
        std::set<size_t> printedCascades;
        for (size_t i = 0; i < emittedGroups; ++i) {
            const auto& g = grouped.groups[i];
            out << severityBadge(g.severity) << " " << g.rule_id;

            if (g.count == 1 && !g.examples.empty()) {
                const auto& ex = g.examples.front();
                const std::string loc = formatExampleLoc(ex);
                out << "  ";
                if (!loc.empty()) {
                    out << loc << ": ";
                }
                out << ex.message << "\n";
            } else if (g.subject.has_value()) {
                out << " ";
                if (!g.examples.empty()) {
                    // Use first example message up to suggestion for concise summary
                    std::string summary = g.examples.front().message;
                    size_t dym = summary.find(" (did you mean ");
                    if (dym != std::string::npos) {
                        summary.resize(dym);
                    }
                    out << summary;
                } else {
                    out << "'" << *g.subject << "'";
                }
                out << " \xc3\x97" << formatWithCommas(g.count);
                if (g.suggestion.has_value()) {
                    out << " (did you mean '" << *g.suggestion << "'?)";
                }
                if (!g.examples.empty()) {
                    out << " \xe2\x80\x94 e.g. ";
                    for (size_t ei = 0; ei < g.examples.size(); ++ei) {
                        if (ei > 0) out << ", ";
                        out << formatExampleLoc(g.examples[ei]);
                    }
                    if (g.count > g.examples.size()) {
                        out << ", \xe2\x80\xa6";
                    }
                }
                out << "\n";
            } else {
                out << " \xc3\x97" << formatWithCommas(g.count);
                if (g.pattern.has_value() && !g.pattern->empty()) {
                    out << " at " << *g.pattern;
                }
                if (!g.examples.empty()) {
                    out << " \xe2\x80\x94 e.g. ";
                    for (size_t ei = 0; ei < g.examples.size(); ++ei) {
                        if (ei > 0) out << ", ";
                        if (!g.examples[ei].subject.empty()) {
                            out << "'" << g.examples[ei].subject << "'";
                        } else {
                            out << formatExampleLoc(g.examples[ei]);
                        }
                    }
                    if (g.count > g.examples.size()) {
                        out << ", \xe2\x80\xa6";
                    }
                }
                out << "\n";
            }

            // Attach any cascades rooted at this group
            for (size_t ci = 0; ci < grouped.cascades.size(); ++ci) {
                if (!printedCascades.count(ci) && groupMatchesCascade(g, grouped.cascades[ci])) {
                    printCascadeLine(grouped.cascades[ci], out);
                    printedCascades.insert(ci);
                }
            }
        }

        if (grouped.groups.size() > emittedGroups) {
            out << "... " << formatWithCommas(grouped.groups.size() - emittedGroups)
                << " more groups omitted (pass --max-groups to show more)\n";
        }

        for (size_t ci = 0; ci < grouped.cascades.size(); ++ci) {
            if (!printedCascades.count(ci)) {
                printCascadeLine(grouped.cascades[ci], out);
            }
        }
    }

    for (const auto& note : grouped.notes) {
        out << "I " << note << "\n";
    }

    if (args.timing) {
        out << std::fixed << std::setprecision(2)
            << "Timing: load " << report.timing_ms.total << " ms"
            << " (parse " << report.timing_ms.parse << " ms, compile "
            << report.timing_ms.compile << " ms)\n";
    }

    size_t errorGroupCount = 0;
    size_t warningGroupCount = 0;
    for (const auto& g : grouped.groups) {
        if (g.severity == gamedata::DiagnosticSeverity::Warning) {
            ++warningGroupCount;
        } else {
            ++errorGroupCount;
        }
    }
    size_t cascadeDroppedTotal = 0;
    for (const auto& cas : grouped.cascades) {
        cascadeDroppedTotal += cas.total_dropped;
    }

    out << formatWithCommas(errorGroupCount)
        << (errorGroupCount == 1 ? " group" : " groups")
        << " (" << formatWithCommas(grouped.error_count)
        << (grouped.error_count == 1 ? " error" : " errors") << ")";
    if (!grouped.cascades.empty()) {
        out << ", " << formatWithCommas(grouped.cascades.size())
            << (grouped.cascades.size() == 1 ? " cascade" : " cascades")
            << " (" << formatWithCommas(cascadeDroppedTotal) << " dropped)";
    }
    out << ", " << formatWithCommas(warningGroupCount)
        << (warningGroupCount == 1 ? " warning group" : " warning groups");
    if (grouped.note_count > 0) {
        out << ", " << formatWithCommas(grouped.note_count)
            << (grouped.note_count == 1 ? " note" : " notes");
    }
    out << ". Exit " << exitCode << ".\n";

    return exitCode;
}

} // namespace gamedata_tool
