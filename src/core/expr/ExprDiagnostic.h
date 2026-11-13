#ifndef EXPRDIAGNOSTIC_H
#define EXPRDIAGNOSTIC_H

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace expr {

enum class OffendingSymbolKind {
    None,
    Identifier,
    Function,
    Parameter,
    Global,
    Let,
    Table,
    OptionKey,
    Resource,
    Attribute
};

struct CompileError {
    std::string rule_id;       // e.g. "V-EXPR-1" .. "V-EXPR-6", "V-REF-1"
    size_t line = 1;           // 1-based line inside the expression string
    size_t column = 1;         // 1-based column inside the expression string
    std::string message;       // Core diagnostic text
    std::string expr_source;   // Full expression string for caret extraction

    // Secondary-error suppression hooks for the v4 package validator (§9.1):
    OffendingSymbolKind offending_kind = OffendingSymbolKind::None;
    std::string offending_symbol; // e.g. parameter id, resource id, table id, attribute name
    std::string offending_parent; // e.g. option_set id, or "machine"/"recipe"/"port"/"resource"
};

struct SuggestionBudget {
    size_t used = 0;
    size_t max_searches = 25;

    bool canSearch() const { return used < max_searches; }
    void consume() {
        if (used < max_searches) {
            ++used;
        }
    }
};

// Computes standard Levenshtein edit distance (insertion, deletion, substitution cost = 1).
size_t levenshteinDistance(std::string_view a, std::string_view b);

// Finds the closest candidate within edit distance <= 2 (pre-filtered by abs(len(a)-len(b)) <= 2).
// Ties in edit distance are broken lexicographically.
// Respects SuggestionBudget (capped at 25 fuzzy searches per load by default).
std::optional<std::string> findBestSuggestion(
    std::string_view target,
    std::span<const std::string> candidates,
    SuggestionBudget* budget = nullptr);

// Formats a compile error per §5.7:
// "<file>: <json_pointer>: <line>:<col>: <message>\n    <source_line>\n    <spaces>^"
std::string formatCompileError(
    const CompileError& err,
    std::string_view file = {},
    std::string_view json_pointer = {},
    bool include_caret = true);

// Formats a runtime error per §5.7:
// e.g. Node "Macerator #3": division by zero in profiles/gt_single/let/oc (recipe 'macerate-crushed-copper')
std::string formatRuntimeError(
    std::string_view node_label,
    std::string_view error_msg,
    std::string_view formula_source = {},
    std::string_view recipe_id = {});

} // namespace expr

#endif // EXPRDIAGNOSTIC_H
