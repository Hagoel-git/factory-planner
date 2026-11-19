#include "core/data/ReportGrouping.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <tuple>
#include <unordered_map>

namespace gamedata {

namespace {

int severityTier(DiagnosticSeverity sev) {
    switch (sev) {
        case DiagnosticSeverity::Fatal:
            return 0;
        case DiagnosticSeverity::Error:
            return 1;
        case DiagnosticSeverity::Warning:
            return 2;
        case DiagnosticSeverity::Info:
            return 3;
    }
    return 1;
}

bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool isAllDigits(std::string_view s) {
    if (s.empty()) return false;
    for (unsigned char c : s) {
        if (!std::isdigit(c)) return false;
    }
    return true;
}

std::string extractFirstQuotedToken(std::string_view message) {
    // Do not treat "(did you mean 'X'?)" as the offending subject.
    std::string_view search = message;
    size_t dymPos = search.find("(did you mean ");
    if (dymPos != std::string_view::npos) {
        search = search.substr(0, dymPos);
    }

    size_t q1 = search.find('\'');
    if (q1 != std::string_view::npos) {
        size_t q2 = search.find('\'', q1 + 1);
        if (q2 != std::string_view::npos && q2 > q1 + 1) {
            return std::string(search.substr(q1 + 1, q2 - q1 - 1));
        }
    }
    return {};
}

std::string effectiveSubject(const LoadDiagnostic& d) {
    if (!d.subject.empty()) {
        return d.subject;
    }
    return extractFirstQuotedToken(d.message);
}

std::optional<std::string> extractSuggestion(std::string_view message) {
    constexpr std::string_view kMarker = "did you mean '";
    size_t pos = message.find(kMarker);
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    size_t start = pos + kMarker.size();
    size_t end = message.find('\'', start);
    if (end == std::string_view::npos || end <= start) {
        return std::nullopt;
    }
    return std::string(message.substr(start, end - start));
}

std::string normalizePointerPattern(std::string_view pointer) {
    if (pointer.empty() || pointer == "/") {
        return std::string(pointer);
    }
    std::string out;
    out.reserve(pointer.size());

    size_t i = 0;
    while (i < pointer.size()) {
        if (pointer[i] == '/') {
            out.push_back('/');
            ++i;
            continue;
        }
        size_t nextSlash = pointer.find('/', i);
        size_t segEnd = (nextSlash == std::string_view::npos) ? pointer.size() : nextSlash;
        std::string_view seg = pointer.substr(i, segEnd - i);
        if (isAllDigits(seg)) {
            out.push_back('*');
        } else {
            out.append(seg);
        }
        i = segEnd;
    }
    return out;
}

struct FilePathParts {
    std::string dir;  // Generic directory prefix (no trailing slash), or ""
    std::string stem; // Filename without extension
    std::string ext;  // Extension including leading '.', or ""
};

FilePathParts splitFilePath(const std::string& file) {
    FilePathParts parts;
    if (file.empty()) {
        return parts;
    }
    std::filesystem::path p(file);
    std::filesystem::path parent = p.parent_path();
    if (!parent.empty() && parent != ".") {
        parts.dir = parent.generic_string();
    }
    parts.ext = p.extension().generic_string();
    parts.stem = p.stem().generic_string();
    return parts;
}

std::string collapseFileGlob(const std::vector<std::string>& rawFiles) {
    std::set<std::string> uniqueFiles(rawFiles.begin(), rawFiles.end());
    if (uniqueFiles.empty()) {
        return {};
    }
    if (uniqueFiles.size() == 1) {
        return *uniqueFiles.begin();
    }

    std::vector<FilePathParts> parts;
    parts.reserve(uniqueFiles.size());
    for (const auto& f : uniqueFiles) {
        parts.push_back(splitFilePath(f));
    }

    const std::string& dir = parts.front().dir;
    const std::string& ext = parts.front().ext;

    // Longest common prefix of stems
    std::string lcp = parts.front().stem;
    for (size_t i = 1; i < parts.size(); ++i) {
        const std::string& s = parts[i].stem;
        size_t n = std::min(lcp.size(), s.size());
        size_t k = 0;
        while (k < n && lcp[k] == s[k]) {
            ++k;
        }
        lcp.resize(k);
        if (lcp.empty()) break;
    }

    if (!lcp.empty()) {
        // Keep lcp as-is if it ends on a separator ('_' or '-') or if every stem continues with a digit.
        bool allContinueDigit = true;
        for (const auto& p : parts) {
            if (p.stem.size() <= lcp.size() ||
                !std::isdigit(static_cast<unsigned char>(p.stem[lcp.size()]))) {
                allContinueDigit = false;
                break;
            }
        }
        char last = lcp.back();
        if (last != '_' && last != '-' && !allContinueDigit) {
            size_t sep = lcp.find_last_of("_-");
            if (sep != std::string::npos) {
                lcp.resize(sep + 1);
            } else {
                lcp.clear();
            }
        }
    }

    std::string result;
    if (!dir.empty()) {
        result += dir;
        result.push_back('/');
    }
    result += lcp;
    result.push_back('*');
    result += ext;
    return result;
}

int stageRankForDiagnostic(const LoadDiagnostic& d) {
    if (d.severity == DiagnosticSeverity::Fatal || startsWith(d.rule_id, "F-")) {
        return 0;
    }
    if (startsWith(d.rule_id, "V-INC-")) {
        return 0;
    }
    if (d.rule_id == "V-ICON-1" || d.rule_id == "V-W-1") {
        return 10;
    }

    // Prefer JSON pointer section to align with §9.1 validation stages.
    const std::string_view ptr = d.json_pointer;
    if (startsWith(ptr, "/schema_version") || startsWith(ptr, "/game") ||
        startsWith(ptr, "/units") || startsWith(ptr, "/include") ||
        startsWith(ptr, "/icon_root") || ptr == "/") {
        return 0;
    }
    if (startsWith(ptr, "/resource_kinds") || startsWith(ptr, "/attributes")) {
        return 1;
    }
    if (startsWith(ptr, "/resources")) {
        return 2;
    }
    if (startsWith(ptr, "/option_sets") || startsWith(ptr, "/tables")) {
        return 3;
    }
    if (startsWith(ptr, "/globals") || startsWith(ptr, "/parameters")) {
        return 4;
    }
    if (startsWith(ptr, "/metrics")) {
        return 5;
    }
    if (startsWith(ptr, "/profiles")) {
        return 6;
    }
    if (startsWith(ptr, "/machines")) {
        return 7;
    }
    if (startsWith(ptr, "/recipes")) {
        return 8;
    }

    // Rule-id fallback when pointer is empty or non-standard.
    if (startsWith(d.rule_id, "V-ATTR-")) return 1;
    if (startsWith(d.rule_id, "V-TBL-")) return 3;
    if (startsWith(d.rule_id, "V-PAR-")) return 4;
    if (startsWith(d.rule_id, "V-MET-")) return 5;
    if (startsWith(d.rule_id, "V-PROF-")) return 6;
    if (d.rule_id == "V-REF-4") return 7;
    if (d.rule_id == "V-REF-2" || d.rule_id == "V-REF-3" ||
        startsWith(d.rule_id, "V-OVR-") ||
        d.rule_id == "V-NUM-2" || d.rule_id == "V-NUM-3" || d.rule_id == "V-NUM-4") {
        return 8;
    }
    if (d.rule_id == "V-W-2" || d.rule_id == "V-W-3") return 9;
    return 8;
}

DiagnosticExample toExample(const LoadDiagnostic& d, const std::string& subj) {
    DiagnosticExample ex;
    ex.file = d.file;
    ex.pointer = d.json_pointer;
    ex.line = d.line;
    ex.col = d.column;
    ex.message = d.message;
    ex.formatted = d.formatted;
    ex.subject = subj;
    ex.entity = d.entity;
    return ex;
}

struct DiagEntry {
    const LoadDiagnostic* diag = nullptr;
    std::string subject;
};

void buildGroupsForList(
    const std::vector<LoadDiagnostic>& list,
    const GroupingOptions& opts,
    std::vector<DiagnosticGroup>& outGroups) {
    // Step 2: Same subject — Key A = (severity, rule_id, subject)
    struct KeyA {
        int sev = 0;
        std::string rule;
        std::string subject;
        bool operator<(const KeyA& o) const {
            return std::tie(sev, rule, subject) < std::tie(o.sev, o.rule, o.subject);
        }
    };

    std::map<KeyA, std::vector<DiagEntry>> bySubject;
    std::vector<DiagEntry> singletons;

    for (const auto& d : list) {
        std::string subj = effectiveSubject(d);
        if (subj.empty()) {
            singletons.push_back(DiagEntry{&d, {}});
        } else {
            KeyA key{static_cast<int>(d.severity), d.rule_id, subj};
            bySubject[key].push_back(DiagEntry{&d, std::move(subj)});
        }
    }

    for (auto& [key, entries] : bySubject) {
        if (entries.size() >= 2) {
            DiagnosticGroup g;
            g.severity = entries.front().diag->severity;
            g.rule_id = key.rule;
            g.subject = key.subject;
            g.pattern = std::nullopt;
            g.count = entries.size();
            g.stage_rank = stageRankForDiagnostic(*entries.front().diag);

            const size_t limit = std::min(entries.size(), opts.max_examples);
            g.examples.reserve(limit);
            for (size_t i = 0; i < entries.size(); ++i) {
                if (!g.suggestion.has_value()) {
                    g.suggestion = extractSuggestion(entries[i].diag->message);
                }
                if (i < limit) {
                    g.examples.push_back(toExample(*entries[i].diag, entries[i].subject));
                }
            }
            outGroups.push_back(std::move(g));
        } else {
            singletons.push_back(std::move(entries.front()));
        }
    }

    // Step 3: Same pattern — Key B = (severity, rule_id, file_dir, file_ext, pointer_pattern)
    struct KeyB {
        int sev = 0;
        std::string rule;
        std::string dir;
        std::string ext;
        std::string ptr_pattern;
        bool operator<(const KeyB& o) const {
            return std::tie(sev, rule, dir, ext, ptr_pattern) <
                   std::tie(o.sev, o.rule, o.dir, o.ext, o.ptr_pattern);
        }
    };

    std::map<KeyB, std::vector<DiagEntry>> byPattern;
    for (auto& entry : singletons) {
        FilePathParts fp = splitFilePath(entry.diag->file);
        std::string ptrPat = normalizePointerPattern(entry.diag->json_pointer);
        // Keep game.json separate from sibling root shards unless both are game.json.
        std::string dirKey = fp.dir;
        if (dirKey.empty() && entry.diag->file == "game.json") {
            dirKey = "<manifest>";
        }
        KeyB key{
            static_cast<int>(entry.diag->severity),
            entry.diag->rule_id,
            std::move(dirKey),
            std::move(fp.ext),
            std::move(ptrPat)
        };
        byPattern[key].push_back(std::move(entry));
    }

    for (auto& [key, entries] : byPattern) {
        DiagnosticGroup g;
        g.severity = entries.front().diag->severity;
        g.rule_id = key.rule;
        g.count = entries.size();
        g.stage_rank = stageRankForDiagnostic(*entries.front().diag);

        if (entries.size() >= 2) {
            std::vector<std::string> files;
            files.reserve(entries.size());
            for (const auto& e : entries) {
                files.push_back(e.diag->file);
            }
            std::string fileGlob = collapseFileGlob(files);
            std::string fullPattern;
            if (!fileGlob.empty() && !key.ptr_pattern.empty()) {
                fullPattern = fileGlob + " " + key.ptr_pattern;
            } else if (!fileGlob.empty()) {
                fullPattern = fileGlob;
            } else {
                fullPattern = key.ptr_pattern;
            }
            g.subject = std::nullopt;
            g.pattern = std::move(fullPattern);
        } else {
            if (!entries.front().subject.empty()) {
                g.subject = entries.front().subject;
            }
            g.pattern = std::nullopt;
        }

        const size_t limit = std::min(entries.size(), opts.max_examples);
        g.examples.reserve(limit);
        for (size_t i = 0; i < entries.size(); ++i) {
            if (!g.suggestion.has_value()) {
                g.suggestion = extractSuggestion(entries[i].diag->message);
            }
            if (i < limit) {
                g.examples.push_back(toExample(*entries[i].diag, entries[i].subject));
            }
        }
        outGroups.push_back(std::move(g));
    }
}

std::pair<std::string, std::string> splitKindId(std::string_view key) {
    size_t colon = key.find(':');
    if (colon == std::string_view::npos) {
        return {std::string{}, std::string(key)};
    }
    return {std::string(key.substr(0, colon)), std::string(key.substr(colon + 1))};
}

} // namespace

int entityKindStageRank(std::string_view singularKind) {
    if (singularKind == "shard" || singularKind == "include") return 0;
    if (singularKind == "resource_kind" || singularKind == "attribute") return 1;
    if (singularKind == "resource") return 2;
    if (singularKind == "option_set" || singularKind == "table") return 3;
    if (singularKind == "global" || singularKind == "parameter") return 4;
    if (singularKind == "metric") return 5;
    if (singularKind == "profile") return 6;
    if (singularKind == "machine") return 7;
    if (singularKind == "recipe") return 8;
    return 9;
}

std::string formatEntityKindCount(std::string_view singularKind, size_t count) {
    std::string label(singularKind);
    if (count != 1) {
        if (singularKind == "resource_kind") label = "resource kinds";
        else if (singularKind == "option_set") label = "option sets";
        else if (singularKind == "attribute") label = "attributes";
        else if (singularKind == "resource") label = "resources";
        else if (singularKind == "table") label = "tables";
        else if (singularKind == "global") label = "globals";
        else if (singularKind == "parameter") label = "parameters";
        else if (singularKind == "metric") label = "metrics";
        else if (singularKind == "profile") label = "profiles";
        else if (singularKind == "machine") label = "machines";
        else if (singularKind == "recipe") label = "recipes";
        else label += "s";
    } else {
        if (singularKind == "resource_kind") label = "resource kind";
        else if (singularKind == "option_set") label = "option set";
    }
    return label;
}

bool diagnosticMatchesRootEntity(const LoadDiagnostic& diag, std::string_view rootKey) {
    if (rootKey.empty()) {
        return false;
    }
    if (!diag.entity.empty()) {
        return diag.entity == rootKey;
    }

    auto [kind, id] = splitKindId(rootKey);
    if (kind.empty() || id.empty()) {
        return false;
    }

    auto matchPointerSection = [&](std::string_view sectionPrefix) -> bool {
        if (!startsWith(diag.json_pointer, sectionPrefix)) {
            return false;
        }
        std::string_view rest = std::string_view(diag.json_pointer).substr(sectionPrefix.size());
        return rest == id || (startsWith(rest, id) && rest.size() > id.size() && rest[id.size()] == '/');
    };

    if (kind == "profile") return matchPointerSection("/profiles/");
    if (kind == "parameter") return matchPointerSection("/parameters/");
    if (kind == "global") return matchPointerSection("/globals/");
    if (kind == "option_set") return matchPointerSection("/option_sets/");
    if (kind == "table") return matchPointerSection("/tables/");
    if (kind == "metric") return matchPointerSection("/metrics/");
    if (kind == "resource_kind") return matchPointerSection("/resource_kinds/");
    if (kind == "attribute") {
        size_t dot = id.find('.');
        if (dot != std::string::npos) {
            std::string attrPtr = "/attributes/" + id.substr(0, dot) + "/" + id.substr(dot + 1);
            return diag.json_pointer == attrPtr || startsWith(diag.json_pointer, attrPtr + "/");
        }
    }
    return !diag.subject.empty() && diag.subject == id;
}

GroupedReport groupLoadReport(const LoadReport& report, const GroupingOptions& opts) {
    GroupedReport out;
    out.error_count = report.errors.size();
    out.warning_count = report.warnings.size();
    out.note_count = report.notes.size();
    out.dropped_count = report.totalDroppedCount();

    out.notes.reserve(report.notes.size());
    for (const auto& n : report.notes) {
        out.notes.push_back(n.message.empty() ? n.formatted : n.message);
    }

    // Step 1: Cascades from report.drop_root
    struct CascadeAccum {
        std::map<std::string, std::vector<std::string>> ids_by_kind;
    };
    std::map<std::string, CascadeAccum> cascadesByRoot;
    for (const auto& [droppedKey, rootKey] : report.drop_root) {
        if (rootKey.empty() || droppedKey == rootKey) {
            continue;
        }
        auto [kind, id] = splitKindId(droppedKey);
        if (kind.empty() || id.empty()) {
            continue;
        }
        cascadesByRoot[rootKey].ids_by_kind[kind].push_back(id);
    }

    for (auto& [rootKey, accum] : cascadesByRoot) {
        CascadeSummary summary;
        summary.root = rootKey;
        auto [rootKind, rootId] = splitKindId(rootKey);
        (void)rootId;
        summary.stage_rank = entityKindStageRank(rootKind);

        for (const auto& err : report.errors) {
            if (diagnosticMatchesRootEntity(err, rootKey)) {
                summary.rule_id = err.rule_id;
                break;
            }
        }

        // Order kinds by deepest downstream stage first for sample_ids selection.
        std::vector<std::string> kindsByDepth;
        kindsByDepth.reserve(accum.ids_by_kind.size());
        for (auto& [kind, ids] : accum.ids_by_kind) {
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            summary.dropped[kind] = ids.size();
            summary.total_dropped += ids.size();
            kindsByDepth.push_back(kind);
        }

        std::sort(kindsByDepth.begin(), kindsByDepth.end(),
                  [](const std::string& a, const std::string& b) {
                      int ra = entityKindStageRank(a);
                      int rb = entityKindStageRank(b);
                      if (ra != rb) return ra > rb;
                      return a < b;
                  });

        for (const auto& kind : kindsByDepth) {
            for (const auto& id : accum.ids_by_kind[kind]) {
                if (summary.sample_ids.size() >= opts.max_examples) {
                    break;
                }
                summary.sample_ids.push_back(id);
            }
            if (summary.sample_ids.size() >= opts.max_examples) {
                break;
            }
        }

        if (summary.total_dropped > 0) {
            out.cascades.push_back(std::move(summary));
        }
    }

    std::sort(out.cascades.begin(), out.cascades.end(),
              [](const CascadeSummary& a, const CascadeSummary& b) {
                  if (a.stage_rank != b.stage_rank) return a.stage_rank < b.stage_rank;
                  if (a.total_dropped != b.total_dropped) return a.total_dropped > b.total_dropped;
                  return a.root < b.root;
              });

    // Steps 2 & 3: Errors and Warnings
    buildGroupsForList(report.errors, opts, out.groups);
    buildGroupsForList(report.warnings, opts, out.groups);

    // Step 4: Deterministic ordering — fatal first, then §9.1 stage order, then count desc, then rule id.
    std::sort(out.groups.begin(), out.groups.end(),
              [](const DiagnosticGroup& a, const DiagnosticGroup& b) {
                  int ta = severityTier(a.severity);
                  int tb = severityTier(b.severity);
                  if (ta != tb) return ta < tb;
                  if (a.stage_rank != b.stage_rank) return a.stage_rank < b.stage_rank;
                  if (a.count != b.count) return a.count > b.count;
                  if (a.rule_id != b.rule_id) return a.rule_id < b.rule_id;
                  const std::string sa = a.subject.value_or(a.pattern.value_or(""));
                  const std::string sb = b.subject.value_or(b.pattern.value_or(""));
                  return sa < sb;
              });

    return out;
}

} // namespace gamedata
