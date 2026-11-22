#include "core/data/RecipeSearchIndex.h"

#include <cctype>
#include <limits>

namespace gamedata {

namespace {

const std::vector<uint32_t> kEmptyIndexList;

void appendAsciiLower(std::string& dst, std::string_view text) {
    const size_t base = dst.size();
    dst.resize(base + text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        dst[base + i] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(text[i])));
    }
}

void pushUniqueSorted(std::vector<uint32_t>& vec, uint32_t idx) {
    if (vec.empty() || vec.back() != idx) {
        vec.push_back(idx);
    }
}

} // namespace

std::string RecipeSearchIndex::toAsciiLower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    appendAsciiLower(out, text);
    return out;
}

RecipeSearchIndex RecipeSearchIndex::build(const GamePackage& pkg) {
    RecipeSearchIndex index;
    const size_t numRecipes = pkg.recipes.size();
    if (numRecipes == 0) {
        return index;
    }

    // Pre-lowercase all resource display names once to avoid repeated conversions over 50k recipes.
    std::vector<std::string> lowerResourceNames(pkg.resources.size());
    for (size_t i = 0; i < pkg.resources.size(); ++i) {
        lowerResourceNames[i] = toAsciiLower(pkg.resources[i].name);
    }

    index.m_spans.reserve(numRecipes);
    index.m_haystackBuf.reserve(numRecipes * 96);
    index.m_producing.reserve(pkg.resources.size());
    index.m_consuming.reserve(pkg.resources.size());

    for (size_t i = 0; i < numRecipes; ++i) {
        const Recipe& recipe = pkg.recipes[i];
        const uint32_t recipeIdx = static_cast<uint32_t>(i);
        const size_t startOffset = index.m_haystackBuf.size();

        appendAsciiLower(index.m_haystackBuf, recipe.name);
        index.m_haystackBuf.push_back('\0');

        for (const auto& port : recipe.in) {
            if (!port.res.has_value() || port.res->empty()) {
                continue;
            }
            const std::string& resId = *port.res;
            pushUniqueSorted(index.m_consuming[resId], recipeIdx);
            if (auto it = pkg.resource_index.find(resId); it != pkg.resource_index.end()) {
                index.m_haystackBuf.append(lowerResourceNames[it->second]);
                index.m_haystackBuf.push_back('\0');
            }
        }

        for (const auto& port : recipe.out) {
            if (!port.res.has_value() || port.res->empty()) {
                continue;
            }
            const std::string& resId = *port.res;
            pushUniqueSorted(index.m_producing[resId], recipeIdx);
            if (auto it = pkg.resource_index.find(resId); it != pkg.resource_index.end()) {
                index.m_haystackBuf.append(lowerResourceNames[it->second]);
                index.m_haystackBuf.push_back('\0');
            }
        }

        const size_t length = index.m_haystackBuf.size() - startOffset;
        Span span;
        span.offset = static_cast<uint32_t>(startOffset);
        span.length = static_cast<uint32_t>(length);
        index.m_spans.push_back(span);
    }

    return index;
}

std::string_view RecipeSearchIndex::haystack(uint32_t recipeIdx) const {
    if (recipeIdx >= m_spans.size()) {
        return {};
    }
    const Span& span = m_spans[recipeIdx];
    return std::string_view(m_haystackBuf.data() + span.offset, span.length);
}

bool RecipeSearchIndex::matchesLower(uint32_t recipeIdx, std::string_view lowerQuery) const {
    if (recipeIdx >= m_spans.size()) {
        return false;
    }
    if (lowerQuery.empty()) {
        return true;
    }
    if (lowerQuery.find('\0') != std::string_view::npos) {
        return false;
    }
    return haystack(recipeIdx).find(lowerQuery) != std::string_view::npos;
}

const std::vector<uint32_t>& RecipeSearchIndex::producing(std::string_view resourceId) const {
    if (resourceId.empty()) {
        return kEmptyIndexList;
    }
    auto it = m_producing.find(std::string(resourceId));
    return (it != m_producing.end()) ? it->second : kEmptyIndexList;
}

const std::vector<uint32_t>& RecipeSearchIndex::consuming(std::string_view resourceId) const {
    if (resourceId.empty()) {
        return kEmptyIndexList;
    }
    auto it = m_consuming.find(std::string(resourceId));
    return (it != m_consuming.end()) ? it->second : kEmptyIndexList;
}

void RecipeSearchState::reset() {
    m_lastIndex = nullptr;
    m_lastRecipeCount = 0;
    m_initialized = false;
    m_lastQueryLower.clear();
    m_lastContextKind = RecipeSearchContextKind::All;
    m_lastContextResourceId.clear();
    m_results.clear();
    m_lastSearchWasIncremental = false;
    m_lastCandidateCount = 0;
}

bool RecipeSearchState::update(
    const RecipeSearchIndex& index,
    std::string_view rawQuery,
    RecipeSearchContextKind contextKind,
    std::string_view contextResourceId) {
    const std::string queryLower = RecipeSearchIndex::toAsciiLower(rawQuery);
    const size_t totalRecipes = index.recipeCount();

    const bool sameIndex = m_initialized &&
                           m_lastIndex == &index &&
                           m_lastRecipeCount == totalRecipes;
    const bool sameContext = sameIndex &&
                             m_lastContextKind == contextKind &&
                             (contextKind == RecipeSearchContextKind::All ||
                              m_lastContextResourceId == contextResourceId);

    if (sameContext && m_lastQueryLower == queryLower) {
        return false;
    }

    // Incremental narrowing: if the new query contains the previous non-empty query as a
    // substring within the exact same context, every match must already be in m_results.
    if (sameContext &&
        !m_lastQueryLower.empty() &&
        !queryLower.empty() &&
        queryLower.find(m_lastQueryLower) != std::string::npos) {
        m_lastSearchWasIncremental = true;
        m_lastCandidateCount = m_results.size();
        size_t writePos = 0;
        for (size_t i = 0; i < m_results.size(); ++i) {
            const uint32_t idx = m_results[i];
            if (index.matchesLower(idx, queryLower)) {
                m_results[writePos++] = idx;
            }
        }
        m_results.resize(writePos);
    } else {
        m_lastSearchWasIncremental = false;
        m_results.clear();

        if (contextKind == RecipeSearchContextKind::All) {
            m_lastCandidateCount = totalRecipes;
            if (queryLower.empty()) {
                m_results.resize(totalRecipes);
                for (size_t i = 0; i < totalRecipes; ++i) {
                    m_results[i] = static_cast<uint32_t>(i);
                }
            } else {
                for (size_t i = 0; i < totalRecipes; ++i) {
                    const uint32_t idx = static_cast<uint32_t>(i);
                    if (index.matchesLower(idx, queryLower)) {
                        m_results.push_back(idx);
                    }
                }
            }
        } else {
            const std::vector<uint32_t>& candidates =
                (contextKind == RecipeSearchContextKind::ProducingResource)
                    ? index.producing(contextResourceId)
                    : index.consuming(contextResourceId);
            m_lastCandidateCount = candidates.size();
            if (queryLower.empty()) {
                m_results = candidates;
            } else {
                m_results.reserve(candidates.size());
                for (uint32_t idx : candidates) {
                    if (index.matchesLower(idx, queryLower)) {
                        m_results.push_back(idx);
                    }
                }
            }
        }
    }

    m_lastIndex = &index;
    m_lastRecipeCount = totalRecipes;
    m_initialized = true;
    m_lastQueryLower = queryLower;
    m_lastContextKind = contextKind;
    m_lastContextResourceId = (contextKind == RecipeSearchContextKind::All)
        ? std::string{}
        : std::string(contextResourceId);
    return true;
}

} // namespace gamedata
