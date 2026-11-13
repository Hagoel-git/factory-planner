#include "core/expr/ExprDiagnostic.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <vector>

namespace expr {

size_t levenshteinDistance(std::string_view a, std::string_view b) {
    const size_t m = a.size();
    const size_t n = b.size();
    if (m == 0) {
        return n;
    }
    if (n == 0) {
        return m;
    }

    std::vector<size_t> prev(n + 1);
    std::vector<size_t> curr(n + 1);
    for (size_t j = 0; j <= n; ++j) {
        prev[j] = j;
    }

    for (size_t i = 1; i <= m; ++i) {
        curr[0] = i;
        for (size_t j = 1; j <= n; ++j) {
            size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            curr[j] = std::min({
                prev[j] + 1,        // deletion
                curr[j - 1] + 1,    // insertion
                prev[j - 1] + cost  // substitution
            });
        }
        std::swap(prev, curr);
    }
    return prev[n];
}

std::optional<std::string> findBestSuggestion(
    std::string_view target,
    std::span<const std::string> candidates,
    SuggestionBudget* budget) {
    if (target.empty() || candidates.empty()) {
        return std::nullopt;
    }
    if (budget != nullptr) {
        if (!budget->canSearch()) {
            return std::nullopt;
        }
        budget->consume();
    }

    size_t best_dist = 3; // Only accept distance <= 2
    std::optional<std::string> best_candidate;

    const int target_len = static_cast<int>(target.size());
    for (const std::string& cand : candidates) {
        const int cand_len = static_cast<int>(cand.size());
        if (std::abs(target_len - cand_len) > 2) {
            continue;
        }
        size_t dist = levenshteinDistance(target, cand);
        if (dist >= 1 && dist <= 2) {
            if (dist < best_dist || (dist == best_dist && (!best_candidate || cand < *best_candidate))) {
                best_dist = dist;
                best_candidate = cand;
            }
        }
    }
    return best_candidate;
}

namespace {

std::string extractLine(std::string_view text, size_t target_line) {
    size_t current_line = 1;
    size_t line_start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (current_line == target_line) {
            size_t line_end = i;
            while (line_end < text.size() && text[line_end] != '\n' && text[line_end] != '\r') {
                ++line_end;
            }
            return std::string(text.substr(i, line_end - i));
        }
        if (text[i] == '\n') {
            ++current_line;
            line_start = i + 1;
        }
    }
    if (current_line == target_line && line_start <= text.size()) {
        return std::string(text.substr(line_start));
    }
    return std::string(text);
}

} // namespace

std::string formatCompileError(
    const CompileError& err,
    std::string_view file,
    std::string_view json_pointer,
    bool include_caret) {
    std::ostringstream oss;
    if (!file.empty()) {
        oss << file << ": ";
    }
    if (!json_pointer.empty()) {
        oss << json_pointer << ": ";
    }
    oss << err.line << ":" << err.column << ": " << err.message;

    if (include_caret && !err.expr_source.empty()) {
        std::string src_line = extractLine(err.expr_source, err.line);
        size_t caret_offset = (err.column > 0) ? (err.column - 1) : 0;
        oss << "\n    " << src_line << "\n    " << std::string(caret_offset, ' ') << "^";
    }
    return oss.str();
}

std::string formatRuntimeError(
    std::string_view node_label,
    std::string_view error_msg,
    std::string_view formula_source,
    std::string_view recipe_id) {
    std::ostringstream oss;
    if (!node_label.empty()) {
        oss << "Node \"" << node_label << "\": ";
    }
    oss << error_msg;
    if (!formula_source.empty()) {
        oss << " in " << formula_source;
    }
    if (!recipe_id.empty()) {
        oss << " (recipe '" << recipe_id << "')";
    }
    return oss.str();
}

} // namespace expr
