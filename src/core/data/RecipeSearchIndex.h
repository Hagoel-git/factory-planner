#ifndef RECIPESEARCHINDEX_H
#define RECIPESEARCHINDEX_H

#include "core/data/GameDataV4.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gamedata {

enum class RecipeSearchContextKind {
    All,
    ProducingResource, // Dragging from an input port -> recipes outputting resource
    ConsumingResource  // Dragging from an output port -> recipes inputting resource
};

// Precomputed, cache-friendly search index for a GamePackage's recipes (§4.4).
// Headless: lives in core_gamedata with no ImGui/GL dependency.
class RecipeSearchIndex {
public:
    static RecipeSearchIndex build(const GamePackage& pkg);
    static std::string toAsciiLower(std::string_view text);

    size_t recipeCount() const { return m_spans.size(); }
    bool empty() const { return m_spans.empty(); }

    std::string_view haystack(uint32_t recipeIdx) const;
    bool matchesLower(uint32_t recipeIdx, std::string_view lowerQuery) const;

    const std::vector<uint32_t>& producing(std::string_view resourceId) const;
    const std::vector<uint32_t>& consuming(std::string_view resourceId) const;

private:
    struct Span {
        uint32_t offset = 0;
        uint32_t length = 0;
    };

    std::string m_haystackBuf;
    std::vector<Span> m_spans;
    std::unordered_map<std::string, std::vector<uint32_t>> m_producing;
    std::unordered_map<std::string, std::vector<uint32_t>> m_consuming;
};

// Stateful incremental filter over a RecipeSearchIndex (§4.4).
// Recomputes results only when query or context changes; when the new query
// extends the previous one in the same context, compacts the previous results in-place.
class RecipeSearchState {
public:
    bool update(
        const RecipeSearchIndex& index,
        std::string_view rawQuery,
        RecipeSearchContextKind contextKind = RecipeSearchContextKind::All,
        std::string_view contextResourceId = {});

    void reset();

    const std::vector<uint32_t>& results() const { return m_results; }
    bool lastSearchWasIncremental() const { return m_lastSearchWasIncremental; }
    size_t lastCandidateCount() const { return m_lastCandidateCount; }
    const std::string& lastQueryLower() const { return m_lastQueryLower; }

private:
    const RecipeSearchIndex* m_lastIndex = nullptr;
    size_t m_lastRecipeCount = 0;
    bool m_initialized = false;
    std::string m_lastQueryLower;
    RecipeSearchContextKind m_lastContextKind = RecipeSearchContextKind::All;
    std::string m_lastContextResourceId;
    std::vector<uint32_t> m_results;
    bool m_lastSearchWasIncremental = false;
    size_t m_lastCandidateCount = 0;
};

} // namespace gamedata

#endif // RECIPESEARCHINDEX_H
