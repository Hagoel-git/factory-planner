#ifndef REPORTGROUPING_H
#define REPORTGROUPING_H

#include "core/data/GameDataV4.h"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gamedata {

struct DiagnosticExample {
    std::string file;
    std::string pointer;
    size_t line = 0;
    size_t col = 0;
    std::string message;
    std::string formatted;
    std::string subject;
    std::string entity;
};

struct DiagnosticGroup {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string rule_id;
    std::optional<std::string> subject;
    std::optional<std::string> pattern;    // "<file_glob> <pointer_pattern>" for pattern groups
    size_t count = 0;
    std::optional<std::string> suggestion; // Extracted from "(did you mean '...')"
    std::vector<DiagnosticExample> examples;
    int stage_rank = 0;
};

struct CascadeSummary {
    std::string root;                      // Canonical "kind:id", e.g. "profile:gt_single"
    std::string rule_id;                   // Primary error rule_id on root, if known
    std::map<std::string, size_t> dropped; // Singular kind -> count
    std::vector<std::string> sample_ids;   // Deterministic sample of dropped ids (deepest stage first)
    size_t total_dropped = 0;
    int stage_rank = 0;
};

struct GroupingOptions {
    size_t max_examples = 3;
};

struct GroupedReport {
    std::vector<DiagnosticGroup> groups;
    std::vector<CascadeSummary> cascades;
    std::vector<std::string> notes;
    size_t error_count = 0;
    size_t warning_count = 0;
    size_t note_count = 0;
    size_t dropped_count = 0;
};

// Returns the §9.1 stage rank for a singular entity kind (e.g. "profile" -> 6, "recipe" -> 8).
int entityKindStageRank(std::string_view singularKind);

// Returns a plural display label for a singular entity kind given a count (e.g. "machine" / "machines").
std::string formatEntityKindCount(std::string_view singularKind, size_t count);

// Checks whether a diagnostic belongs to the given canonical root entity ("kind:id").
bool diagnosticMatchesRootEntity(const LoadDiagnostic& diag, std::string_view rootKey);

// Groups a LoadReport's errors, warnings, cascades (drop_root), and notes deterministically
// per docs/design/gamedata-tool.md §2.3.
GroupedReport groupLoadReport(const LoadReport& report, const GroupingOptions& opts = {});

} // namespace gamedata

#endif // REPORTGROUPING_H
