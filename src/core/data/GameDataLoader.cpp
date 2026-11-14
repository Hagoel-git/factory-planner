#include "core/data/GameDataLoader.h"

#include "core/expr/ExprCompiler.h"
#include "core/expr/ExprDiagnostic.h"

#include <absl/log/log.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gamedata {

namespace {

using json = nlohmann::ordered_json;

void addUnique(std::vector<std::string>& vec, const std::string& item) {
    if (std::find(vec.begin(), vec.end(), item) == vec.end()) {
        vec.push_back(item);
    }
}

std::string jsonTypeName(const json& j) {
    if (j.is_null()) return "null";
    if (j.is_boolean()) return "bool";
    if (j.is_number()) return "number";
    if (j.is_string()) return "string";
    if (j.is_array()) return "array";
    if (j.is_object()) return "object";
    return "unknown";
}

// ============================================================================
// Lazy JSON Pointer Stack (§2.9 item 3)
// ============================================================================

class JsonPathStack {
public:
    void pushKey(std::string_view key) {
        m_frames.push_back(Frame{false, std::string(key), 0});
    }

    void pushIndex(size_t idx) {
        m_frames.push_back(Frame{true, {}, idx});
    }

    void pop() {
        if (!m_frames.empty()) {
            m_frames.pop_back();
        }
    }

    std::string toString() const {
        if (m_frames.empty()) {
            return "/";
        }
        std::string out;
        for (const auto& f : m_frames) {
            out.push_back('/');
            if (f.is_index) {
                out += std::to_string(f.index);
            } else {
                for (char c : f.key) {
                    if (c == '~') {
                        out += "~0";
                    } else if (c == '/') {
                        out += "~1";
                    } else {
                        out.push_back(c);
                    }
                }
            }
        }
        return out;
    }

    std::string toStringWithChild(std::string_view child_key) const {
        std::string base = m_frames.empty() ? "" : toString();
        base.push_back('/');
        for (char c : child_key) {
            if (c == '~') {
                base += "~0";
            } else if (c == '/') {
                base += "~1";
            } else {
                base.push_back(c);
            }
        }
        return base;
    }

    std::string toStringWithIndex(size_t idx) const {
        std::string base = m_frames.empty() ? "" : toString();
        base.push_back('/');
        base += std::to_string(idx);
        return base;
    }

private:
    struct Frame {
        bool is_index = false;
        std::string key;
        size_t index = 0;
    };
    std::vector<Frame> m_frames;
};

struct PathKeyScope {
    JsonPathStack& stack;
    PathKeyScope(JsonPathStack& s, std::string_view key) : stack(s) { stack.pushKey(key); }
    ~PathKeyScope() { stack.pop(); }
};

struct PathIndexScope {
    JsonPathStack& stack;
    PathIndexScope(JsonPathStack& s, size_t idx) : stack(s) { stack.pushIndex(idx); }
    ~PathIndexScope() { stack.pop(); }
};

// ============================================================================
// SAX Parser with In-Object Duplicate Key Filtering (V-ID-2, F-2, V-INC-2)
// ============================================================================

struct DuplicateKeyInfo {
    std::string json_pointer;
    std::string key;
};

struct SaxParseOutcome {
    bool ok = true;
    json root;
    size_t error_line = 1;
    size_t error_col = 1;
    std::string error_message;
    std::vector<DuplicateKeyInfo> duplicate_keys;
};

void computeLineColFromByteOffset(
    std::string_view text,
    size_t one_based_byte_pos,
    size_t& out_line,
    size_t& out_col) {
    out_line = 1;
    out_col = 1;
    size_t target = (one_based_byte_pos > 0) ? (one_based_byte_pos - 1) : 0;
    if (target > text.size()) {
        target = text.size();
    }
    for (size_t i = 0; i < target; ++i) {
        if (text[i] == '\n') {
            ++out_line;
            out_col = 1;
        } else {
            ++out_col;
        }
    }
}

std::string cleanNlohmannParseErrorMessage(std::string_view raw_what) {
    // Strip "[json.exception.parse_error.101] parse error at line X, column Y: "
    size_t colon = raw_what.find(": ");
    if (colon != std::string_view::npos) {
        return std::string(raw_what.substr(colon + 2));
    }
    return std::string(raw_what);
}

class DomWithDuplicateKeySax : public nlohmann::json_sax<json> {
public:
    explicit DomWithDuplicateKeySax(std::string_view src, SaxParseOutcome& out)
        : m_src(src), m_out(out) {}

    bool null() override { return acceptPrimitive(json(nullptr)); }
    bool boolean(bool val) override { return acceptPrimitive(json(val)); }
    bool number_integer(number_integer_t val) override { return acceptPrimitive(json(val)); }
    bool number_unsigned(number_unsigned_t val) override { return acceptPrimitive(json(val)); }
    bool number_float(number_float_t val, const string_t& /*s*/) override {
        return acceptPrimitive(json(val));
    }
    bool string(string_t& val) override { return acceptPrimitive(json(val)); }
    bool binary(binary_t& val) override { return acceptPrimitive(json(val)); }

    bool start_object(std::size_t /*elements*/) override {
        if (m_skip_depth > 0) {
            ++m_skip_depth;
            return true;
        }
        if (m_skip_next_value) {
            m_skip_next_value = false;
            m_skip_depth = 1;
            return true;
        }
        json* target = attachContainer(json::object());
        m_stack.push_back(ContainerFrame{true, target, {}, {}, 0});
        return true;
    }

    bool key(string_t& val) override {
        if (m_skip_depth > 0) {
            return true;
        }
        if (m_stack.empty() || !m_stack.back().is_object) {
            return false;
        }
        auto& top = m_stack.back();
        top.current_key = val;
        auto [_, inserted] = top.seen_keys.insert(val);
        if (!inserted) {
            m_out.duplicate_keys.push_back(DuplicateKeyInfo{formatCurrentPointer(val), val});
            m_skip_next_value = true;
            return true;
        }
        m_object_element = &((*top.node)[val]);
        return true;
    }

    bool end_object() override {
        if (m_skip_depth > 0) {
            --m_skip_depth;
            return true;
        }
        if (!m_stack.empty()) {
            m_stack.pop_back();
        }
        if (!m_stack.empty() && !m_stack.back().is_object) {
            ++m_stack.back().next_array_index;
        }
        return true;
    }

    bool start_array(std::size_t /*elements*/) override {
        if (m_skip_depth > 0) {
            ++m_skip_depth;
            return true;
        }
        if (m_skip_next_value) {
            m_skip_next_value = false;
            m_skip_depth = 1;
            return true;
        }
        json* target = attachContainer(json::array());
        m_stack.push_back(ContainerFrame{false, target, {}, {}, 0});
        return true;
    }

    bool end_array() override {
        if (m_skip_depth > 0) {
            --m_skip_depth;
            return true;
        }
        if (!m_stack.empty()) {
            m_stack.pop_back();
        }
        if (!m_stack.empty() && !m_stack.back().is_object) {
            ++m_stack.back().next_array_index;
        }
        return true;
    }

    bool parse_error(
        std::size_t position,
        const std::string& /*last_token*/,
        const json::exception& ex) override {
        m_out.ok = false;
        computeLineColFromByteOffset(m_src, position, m_out.error_line, m_out.error_col);
        m_out.error_message = cleanNlohmannParseErrorMessage(ex.what());
        return false;
    }

private:
    struct ContainerFrame {
        bool is_object = true;
        json* node = nullptr;
        std::unordered_set<std::string> seen_keys;
        std::string current_key;
        size_t next_array_index = 0;
    };

    bool acceptPrimitive(json&& val) {
        if (m_skip_depth > 0) {
            return true;
        }
        if (m_skip_next_value) {
            m_skip_next_value = false;
            return true;
        }
        if (m_stack.empty()) {
            m_out.root = std::move(val);
            return true;
        }
        auto& top = m_stack.back();
        if (top.is_object) {
            if (m_object_element != nullptr) {
                *m_object_element = std::move(val);
            }
        } else {
            top.node->push_back(std::move(val));
            ++top.next_array_index;
        }
        return true;
    }

    json* attachContainer(json&& container) {
        if (m_stack.empty()) {
            m_out.root = std::move(container);
            return &m_out.root;
        }
        auto& top = m_stack.back();
        if (top.is_object) {
            *m_object_element = std::move(container);
            return m_object_element;
        }
        top.node->push_back(std::move(container));
        return &top.node->back();
    }

    std::string formatCurrentPointer(std::string_view leaf_key) const {
        std::string ptr;
        for (size_t i = 0; i < m_stack.size(); ++i) {
            const auto& f = m_stack[i];
            if (i + 1 < m_stack.size()) {
                ptr.push_back('/');
                if (f.is_object) {
                    ptr += f.current_key;
                } else {
                    ptr += std::to_string(f.next_array_index);
                }
            }
        }
        ptr.push_back('/');
        ptr += leaf_key;
        return ptr;
    }

    std::string_view m_src;
    SaxParseOutcome& m_out;
    std::vector<ContainerFrame> m_stack;
    json* m_object_element = nullptr;
    size_t m_skip_depth = 0;
    bool m_skip_next_value = false;
};

SaxParseOutcome parseJsonWithDuplicateCheck(std::string_view text) {
    SaxParseOutcome outcome;
    DomWithDuplicateKeySax sax(text, outcome);
    if (!json::sax_parse(text.begin(), text.end(), &sax)) {
        outcome.ok = false;
    }
    return outcome;
}

// ============================================================================
// Raw Entry & Shard Tracking Structures
// ============================================================================

struct RawEntry {
    std::string file;
    std::string json_pointer;
    json value;
};

struct RawArrayItem {
    std::string file;
    std::string json_pointer;
    size_t index_in_file = 0;
    json value;
};

struct ExpressionCacheKey {
    std::string profile_id;
    expr::ExprContext context = expr::ExprContext::PreSolveLet;
    expr::ExpectedType expected_type = expr::ExpectedType::Number;
    bool port_has_base_resource = true;
    std::string source;

    bool operator==(const ExpressionCacheKey& other) const {
        return profile_id == other.profile_id &&
               context == other.context &&
               expected_type == other.expected_type &&
               port_has_base_resource == other.port_has_base_resource &&
               source == other.source;
    }
};

struct ExpressionCacheKeyHash {
    size_t operator()(const ExpressionCacheKey& k) const noexcept {
        size_t h1 = std::hash<std::string>{}(k.profile_id);
        size_t h2 = std::hash<int>{}(static_cast<int>(k.context));
        size_t h3 = std::hash<int>{}(static_cast<int>(k.expected_type));
        size_t h4 = std::hash<bool>{}(k.port_has_base_resource);
        size_t h5 = std::hash<std::string>{}(k.source);
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4);
    }
};

// ============================================================================
// Built-in Field Shadowing Helpers (§2.5, §5.5, V-ATTR-2)
// ============================================================================

bool isBuiltinEntityField(std::string_view entity_kind, std::string_view attr_name) {
    if (entity_kind == "machine") {
        return attr_name == "id" || attr_name == "speed";
    }
    if (entity_kind == "recipe") {
        return attr_name == "id" || attr_name == "time" || attr_name == "time_raw" ||
               attr_name == "input_items_total" || attr_name == "input_fluids_total" ||
               attr_name == "output_items_total" || attr_name == "output_fluids_total";
    }
    if (entity_kind == "port") {
        return attr_name == "dir" || attr_name == "amount" || attr_name == "chance" ||
               attr_name == "catalyst" || attr_name == "index" ||
               attr_name == "base_resource" || attr_name == "resource";
    }
    if (entity_kind == "resource") {
        return attr_name == "id";
    }
    if (entity_kind == "option") {
        return attr_name == "key" || attr_name == "id";
    }
    if (entity_kind == "game") {
        return attr_name == "ticks_per_second" || attr_name == "id";
    }
    return false;
}

bool isSuitableWidgetForType(ParameterType type, UiWidget widget) {
    switch (type) {
        case ParameterType::Number:
        case ParameterType::Int:
            return widget == UiWidget::Slider || widget == UiWidget::Drag ||
                   widget == UiWidget::Input || widget == UiWidget::Stepper;
        case ParameterType::Bool:
            return widget == UiWidget::Checkbox;
        case ParameterType::Enum:
        case ParameterType::ResourceChoice:
            return widget == UiWidget::Combo;
        case ParameterType::Slots:
            return widget == UiWidget::SlotRow;
    }
    return false;
}

std::optional<UiWidget> parseUiWidget(std::string_view s) {
    if (s == "slider") return UiWidget::Slider;
    if (s == "drag") return UiWidget::Drag;
    if (s == "input") return UiWidget::Input;
    if (s == "stepper") return UiWidget::Stepper;
    if (s == "checkbox") return UiWidget::Checkbox;
    if (s == "combo") return UiWidget::Combo;
    if (s == "slot_row") return UiWidget::SlotRow;
    return std::nullopt;
}

// ============================================================================
// PackageLoader Context Class
// ============================================================================

class PackageLoader {
public:
    PackageLoader(
        const std::filesystem::path& package_root,
        const std::unordered_map<std::string, std::string>* virtual_shards,
        GamePackage& out_pkg,
        LoadReport& out_report)
        : m_packageRoot(package_root),
          m_virtualShards(virtual_shards),
          m_pkg(out_pkg),
          m_report(out_report) {}

    bool loadFromManifestText(std::string_view manifest_text, const std::string& manifest_rel_name) {
        // Stage 0: Parse manifest & shards
        if (!runStage0(manifest_text, manifest_rel_name)) {
            return false;
        }
        // Stage 1: game, resource_kinds, attributes
        runStage1();
        // Stage 2: resources
        runStage2();
        // Stage 3: option_sets & tables
        runStage3();
        // Stage 4: parameters & globals
        runStage4();
        // Stage 5: metrics
        runStage5();
        // Stage 6: profiles
        runStage6();
        // Stage 7: machines
        runStage7();
        // Stage 8: recipes
        runStage8();
        // Stage 9: V-W-2 unused warnings & base EvalEnv assembly
        runStage9();

        return true;
    }

    void addFatal(
        std::string rule_id,
        std::string file,
        std::string json_pointer,
        std::string message,
        size_t line = 0,
        size_t col = 0) {
        m_report.fatal = true;
        LoadDiagnostic d;
        d.severity = DiagnosticSeverity::Fatal;
        d.rule_id = std::move(rule_id);
        d.file = std::move(file);
        d.json_pointer = std::move(json_pointer);
        d.line = line;
        d.column = col;
        d.message = std::move(message);
        d.formatted = formatDiagnostic(d);
        m_report.errors.push_back(std::move(d));
    }

private:
    static std::string formatDiagnostic(const LoadDiagnostic& d) {
        std::ostringstream oss;
        if (!d.file.empty()) {
            oss << d.file;
            if (d.json_pointer.empty() && d.line > 0) {
                oss << ":" << d.line << ":" << d.column << ": ";
            } else {
                oss << ": ";
            }
        }
        if (!d.json_pointer.empty()) {
            oss << d.json_pointer << ": ";
            if (d.line > 0) {
                oss << d.line << ":" << d.column << ": ";
            }
        }
        oss << d.message;
        return oss.str();
    }

    void addError(
        std::string rule_id,
        std::string file,
        std::string json_pointer,
        std::string message,
        size_t line = 0,
        size_t col = 0) {
        LoadDiagnostic d;
        d.severity = DiagnosticSeverity::Error;
        d.rule_id = std::move(rule_id);
        d.file = std::move(file);
        d.json_pointer = std::move(json_pointer);
        d.line = line;
        d.column = col;
        d.message = std::move(message);
        d.formatted = formatDiagnostic(d);
        m_report.errors.push_back(std::move(d));
    }

    void addCompileError(
        const expr::CompileError& cerr,
        std::string file,
        std::string json_pointer,
        std::string override_rule_id = {},
        std::string suffix_msg = {}) {
        LoadDiagnostic d;
        d.severity = DiagnosticSeverity::Error;
        d.rule_id = override_rule_id.empty() ? cerr.rule_id : std::move(override_rule_id);
        d.file = std::move(file);
        d.json_pointer = std::move(json_pointer);
        d.line = cerr.line;
        d.column = cerr.column;
        d.message = cerr.message + suffix_msg;
        expr::CompileError adjusted = cerr;
        adjusted.message = d.message;
        d.formatted = expr::formatCompileError(adjusted, d.file, d.json_pointer, true);
        m_report.errors.push_back(std::move(d));
    }

    void addWarning(
        std::string rule_id,
        std::string file,
        std::string json_pointer,
        std::string message) {
        LoadDiagnostic d;
        d.severity = DiagnosticSeverity::Warning;
        d.rule_id = std::move(rule_id);
        d.file = std::move(file);
        d.json_pointer = std::move(json_pointer);
        d.message = std::move(message);
        d.formatted = formatDiagnostic(d);
        m_report.warnings.push_back(std::move(d));
    }

    void markDropped(const std::string& kind, const std::string& id) {
        if (!id.empty()) {
            addUnique(m_report.dropped[kind], id);
        }
    }

    std::string formatUnknownFieldMessage(
        std::string_view key,
        std::span<const std::string> allowed_keys) {
        std::string msg = "unknown field '" + std::string(key) + "'";
        auto sug = expr::findBestSuggestion(key, allowed_keys, &m_suggestionBudget);
        if (sug.has_value()) {
            msg += " (did you mean '" + *sug + "'?)";
        }
        return msg;
    }

    bool checkAllowedKeys(
        const json& obj,
        const std::string& file,
        const std::string& base_ptr,
        std::span<const std::string> allowed_keys) {
        bool ok = true;
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const std::string& k = it.key();
            if (std::find(allowed_keys.begin(), allowed_keys.end(), k) == allowed_keys.end()) {
                std::string ptr = (base_ptr == "/") ? ("/" + k) : (base_ptr + "/" + k);
                addError("V-STRUCT-1", file, ptr, formatUnknownFieldMessage(k, allowed_keys));
                ok = false;
            }
        }
        if (obj.contains("notes") && !obj["notes"].is_string()) {
            std::string ptr = (base_ptr == "/") ? "/notes" : (base_ptr + "/notes");
            addError("V-STRUCT-2", file, ptr,
                     "expected string for 'notes', got " + jsonTypeName(obj["notes"]));
            ok = false;
        }
        return ok;
    }

    // Secondary error suppression check (§9.1):
    // Returns true if the CompileError was caused by referencing an entity that already failed
    // in an earlier validation stage.
    bool isSecondaryCompileError(const expr::CompileError& err) const {
        switch (err.offending_kind) {
            case expr::OffendingSymbolKind::Resource:
                return m_failedResources.count(err.offending_symbol) > 0;
            case expr::OffendingSymbolKind::Table:
                return m_failedTables.count(err.offending_symbol) > 0;
            case expr::OffendingSymbolKind::OptionKey:
                return m_failedOptionSets.count(err.offending_parent) > 0;
            case expr::OffendingSymbolKind::Parameter:
                return m_failedParameters.count(err.offending_symbol) > 0;
            case expr::OffendingSymbolKind::Global:
                return m_failedGlobals.count(err.offending_symbol) > 0;
            case expr::OffendingSymbolKind::Attribute: {
                if (m_failedOptionSets.count(err.offending_parent) > 0) {
                    return true;
                }
                auto it = m_failedAttrs.find(err.offending_parent);
                if (it != m_failedAttrs.end() && it->second.count(err.offending_symbol) > 0) {
                    return true;
                }
                return false;
            }
            default:
                return false;
        }
    }

    // Checks if a compiled expression references any entity that failed in an earlier stage.
    bool referencesFailedEntity(const expr::CompiledExpr& comp) const {
        for (const auto& g : comp.referenced_globals) {
            if (m_failedGlobals.count(g)) return true;
        }
        for (const auto& p : comp.referenced_params) {
            if (m_failedParameters.count(p)) return true;
        }
        for (const auto& t : comp.referenced_tables) {
            if (m_failedTables.count(t)) return true;
        }
        for (const auto& r : comp.referenced_resources) {
            if (m_failedResources.count(r)) return true;
        }
        auto checkAttrs = [&](const std::string& kind, const std::vector<std::string>& attrs) {
            auto it = m_failedAttrs.find(kind);
            if (it == m_failedAttrs.end()) return false;
            for (const auto& a : attrs) {
                if (it->second.count(a)) return true;
            }
            return false;
        };
        if (checkAttrs("machine", comp.referenced_machine_attrs)) return true;
        if (checkAttrs("recipe", comp.referenced_recipe_attrs)) return true;
        if (checkAttrs("port", comp.referenced_port_attrs)) return true;
        if (checkAttrs("resource", comp.referenced_resource_attrs)) return true;
        return false;
    }

    void recordExprUsage(const expr::CompiledExpr& comp, bool in_resource_expr = false) {
        for (const auto& t : comp.referenced_tables) {
            m_usedTables.insert(t);
        }
        for (const auto& r : comp.referenced_resources) {
            m_usedResources.insert(r);
        }
        for (const auto& p : comp.referenced_params) {
            m_usedParameters.insert(p);
        }
        if (in_resource_expr) {
            for (const auto& a : comp.referenced_resource_attrs) {
                m_resourceAttrsUsedInResourceExpr.insert(a);
            }
            for (const auto& a : comp.referenced_machine_attrs) {
                m_machineAttrsUsedInResourceExpr.insert(a);
            }
            for (const auto& a : comp.referenced_recipe_attrs) {
                m_recipeAttrsUsedInResourceExpr.insert(a);
            }
            for (const auto& a : comp.referenced_port_attrs) {
                m_portAttrsUsedInResourceExpr.insert(a);
            }
        }
    }

    // ========================================================================
    // Stage 0: Manifest & Shard Loading (§2.1–§2.9, F-1..F-5, V-INC-1..3)
    // ========================================================================

    static bool pathEscapesRoot(const std::filesystem::path& rel_path) {
        if (rel_path.is_absolute()) {
            return true;
        }
        int depth = 0;
        for (const auto& part : rel_path.lexically_normal()) {
            if (part == "." || part.empty()) {
                continue;
            }
            if (part == "..") {
                --depth;
                if (depth < 0) {
                    return true;
                }
            } else {
                ++depth;
            }
        }
        return depth < 0;
    }

    static bool iconRootEscapesGameDir(const std::filesystem::path& rel_path) {
        if (rel_path.is_absolute()) {
            return true;
        }
        // icon_root may go up at most 1 level (into <game>/, e.g. "../icons" per §2.1)
        int depth = 0;
        for (const auto& part : rel_path.lexically_normal()) {
            if (part == "." || part.empty()) {
                continue;
            }
            if (part == "..") {
                --depth;
                if (depth < -1) {
                    return true;
                }
            } else {
                ++depth;
            }
        }
        return depth < -1;
    }

    void processDuplicateKeysFromSax(
        const std::string& file,
        const std::vector<DuplicateKeyInfo>& dup_keys) {
        for (const auto& dk : dup_keys) {
            addError(
                "V-ID-2", file, dk.json_pointer,
                "duplicate key '" + dk.key + "' in JSON object (keeping first definition)");
        }
    }

    bool collectTopLevelSections(
        const json& root,
        const std::string& file,
        bool is_manifest) {
        static const std::vector<std::string> kManifestKeys = {
            "attributes", "game", "globals", "icon_root", "include", "machines",
            "metrics", "notes", "option_sets", "parameters", "profiles",
            "recipes", "resource_kinds", "resources", "schema_version", "tables", "units"
        };
        static const std::vector<std::string> kShardKeys = {
            "attributes", "globals", "machines", "metrics", "notes",
            "option_sets", "parameters", "profiles", "recipes",
            "resource_kinds", "resources", "tables"
        };
        static const std::unordered_set<std::string> kManifestOnlyKeys = {
            "schema_version", "game", "units", "include", "icon_root"
        };

        if (!is_manifest) {
            // Check V-INC-3 (manifest-only key in shard) and V-STRUCT-1 (unknown top-level key in shard)
            bool shard_ok = true;
            for (auto it = root.begin(); it != root.end(); ++it) {
                const std::string& k = it.key();
                if (kManifestOnlyKeys.count(k)) {
                    addError("V-INC-3", file, "/" + k,
                             "shards cannot define manifest-only key '" + k + "'");
                    shard_ok = false;
                } else if (std::find(kShardKeys.begin(), kShardKeys.end(), k) == kShardKeys.end()) {
                    addError("V-STRUCT-1", file, "/" + k,
                             formatUnknownFieldMessage(k, kShardKeys));
                    shard_ok = false;
                }
            }
            if (!shard_ok) {
                markDropped("shards", file);
                return false;
            }
        } else {
            checkAllowedKeys(root, file, "/", kManifestKeys);
        }

        // Map sections merged by key (V-ID-2 across shards)
        auto mergeMapSection = [&](const char* key_name,
                                   const char* drop_kind,
                                   std::vector<std::string>& order_vec,
                                   std::unordered_map<std::string, RawEntry>& target_map) {
            if (!root.contains(key_name)) {
                return;
            }
            const json& sec = root[key_name];
            std::string sec_ptr = std::string("/") + key_name;
            if (!sec.is_object()) {
                addError("V-STRUCT-2", file, sec_ptr,
                         std::string("expected object for '") + key_name + "', got " + jsonTypeName(sec));
                return;
            }
            for (auto it = sec.begin(); it != sec.end(); ++it) {
                const std::string& id = it.key();
                if (id == "notes" && it.value().is_string()) {
                    continue;
                }
                std::string entry_ptr = sec_ptr + "/" + id;
                auto existing = target_map.find(id);
                if (existing != target_map.end()) {
                    addError("V-ID-2", file, entry_ptr,
                             std::string("duplicate ") + drop_kind + " id '" + id +
                                 "' (first defined at " + existing->second.file + ": " +
                                 existing->second.json_pointer + ")");
                    markDropped(drop_kind, id);
                    continue;
                }
                order_vec.push_back(id);
                target_map.emplace(id, RawEntry{file, entry_ptr, it.value()});
            }
        };

        mergeMapSection("resource_kinds", "resource_kinds", m_resourceKindOrder, m_rawResourceKinds);
        mergeMapSection("tables", "tables", m_tableOrder, m_rawTables);
        mergeMapSection("option_sets", "option_sets", m_optionSetOrder, m_rawOptionSets);
        mergeMapSection("parameters", "parameters", m_parameterOrder, m_rawParameters);
        mergeMapSection("globals", "globals", m_globalOrder, m_rawGlobals);
        mergeMapSection("metrics", "metrics", m_metricOrder, m_rawMetrics);
        mergeMapSection("profiles", "profiles", m_profileOrder, m_rawProfiles);

        // /attributes is merged at the 2nd level: /attributes/<entity_kind>/<attr> (§2.9, R2-12)
        if (root.contains("attributes")) {
            const json& attrs_sec = root["attributes"];
            if (!attrs_sec.is_object()) {
                addError("V-STRUCT-2", file, "/attributes",
                         "expected object for 'attributes', got " + jsonTypeName(attrs_sec));
            } else {
                static const std::vector<std::string> kAttrEntityKinds = {
                    "machine", "notes", "port", "recipe", "resource"
                };
                checkAllowedKeys(attrs_sec, file, "/attributes", kAttrEntityKinds);
                for (const char* ek : {"machine", "recipe", "port", "resource"}) {
                    if (!attrs_sec.contains(ek)) {
                        continue;
                    }
                    const json& ek_obj = attrs_sec[ek];
                    std::string ek_ptr = std::string("/attributes/") + ek;
                    if (!ek_obj.is_object()) {
                        addError("V-STRUCT-2", file, ek_ptr,
                                 std::string("expected object for '") + ek_ptr + "', got " +
                                     jsonTypeName(ek_obj));
                        continue;
                    }
                    auto& target_map = m_rawAttributes[ek];
                    auto& order_vec = m_attributeOrder[ek];
                    for (auto it = ek_obj.begin(); it != ek_obj.end(); ++it) {
                        const std::string& attr_name = it.key();
                        if (attr_name == "notes" && it.value().is_string()) {
                            continue;
                        }
                        std::string attr_ptr = ek_ptr + "/" + attr_name;
                        auto existing = target_map.find(attr_name);
                        if (existing != target_map.end()) {
                            addError("V-ID-2", file, attr_ptr,
                                     std::string("duplicate ") + ek + " attribute '" + attr_name +
                                         "' (first defined at " + existing->second.file + ": " +
                                         existing->second.json_pointer + ")");
                            markDropped("attributes", std::string(ek) + "." + attr_name);
                            continue;
                        }
                        order_vec.push_back(attr_name);
                        target_map.emplace(attr_name, RawEntry{file, attr_ptr, it.value()});
                    }
                }
            }
        }

        // Array sections: resources, machines, recipes
        auto appendArraySection = [&](const char* key_name, std::vector<RawArrayItem>& target_vec) {
            if (!root.contains(key_name)) {
                return;
            }
            const json& arr = root[key_name];
            std::string sec_ptr = std::string("/") + key_name;
            if (!arr.is_array()) {
                addError("V-STRUCT-2", file, sec_ptr,
                         std::string("expected array for '") + key_name + "', got " + jsonTypeName(arr));
                return;
            }
            for (size_t i = 0; i < arr.size(); ++i) {
                target_vec.push_back(RawArrayItem{
                    file,
                    sec_ptr + "/" + std::to_string(i),
                    i,
                    arr[i]
                });
            }
        };

        appendArraySection("resources", m_rawResources);
        appendArraySection("machines", m_rawMachines);
        appendArraySection("recipes", m_rawRecipes);

        return true;
    }

    bool runStage0(std::string_view manifest_text, const std::string& manifest_rel_name) {
        SaxParseOutcome parsed = parseJsonWithDuplicateCheck(manifest_text);
        if (!parsed.ok) {
            addFatal(
                "F-2", manifest_rel_name, "",
                "JSON parse error: " + parsed.error_message,
                parsed.error_line, parsed.error_col);
            return false;
        }
        if (!parsed.root.is_object()) {
            addFatal("F-2", manifest_rel_name, "/", "manifest root must be a JSON object");
            return false;
        }

        const json& m = parsed.root;
        m_manifestJson = m;

        // F-3: schema_version
        if (!m.contains("schema_version")) {
            addFatal("F-3", manifest_rel_name, "/schema_version",
                     "missing required 'schema_version' (this build reads 4)");
            return false;
        }
        if (!m["schema_version"].is_number_integer()) {
            addFatal("F-3", manifest_rel_name, "/schema_version",
                     "unsupported schema version (expected integer 4)");
            return false;
        }
        int ver = m["schema_version"].get<int>();
        if (ver != 4) {
            addFatal("F-3", manifest_rel_name, "/schema_version",
                     "unsupported schema version " + std::to_string(ver) + " (this build reads 4)");
            return false;
        }
        m_pkg.schema_version = 4;

        // F-4: game.id
        if (!m.contains("game") || !m["game"].is_object()) {
            addFatal("F-4", manifest_rel_name, "/game", "missing required 'game' object");
            return false;
        }
        const json& game_obj = m["game"];
        if (!game_obj.contains("id") || !game_obj["id"].is_string()) {
            addFatal("F-4", manifest_rel_name, "/game/id",
                     "required string matching [a-z0-9_-]+");
            return false;
        }
        std::string game_id = game_obj["id"].get<std::string>();
        if (!expr::isValidSlugId(game_id)) {
            addFatal("F-4", manifest_rel_name, "/game/id",
                     "invalid game id '" + game_id + "' (required string matching [a-z0-9_-]+)");
            return false;
        }
        m_pkg.game.id = game_id;

        // F-5: units
        if (!m.contains("units") || !m["units"].is_object()) {
            addFatal("F-5", manifest_rel_name, "/units", "missing required 'units' object");
            return false;
        }
        const json& units_obj = m["units"];
        if (!units_obj.contains("recipe_time") || !units_obj["recipe_time"].is_string()) {
            addFatal("F-5", manifest_rel_name, "/units/recipe_time",
                     "required string ('s' or 'tick')");
            return false;
        }
        std::string rt_str = units_obj["recipe_time"].get<std::string>();
        if (rt_str == "s") {
            m_pkg.units.recipe_time = RecipeTimeUnit::Seconds;
        } else if (rt_str == "tick") {
            m_pkg.units.recipe_time = RecipeTimeUnit::Ticks;
        } else {
            addFatal("F-5", manifest_rel_name, "/units/recipe_time",
                     "'" + rt_str + "' is not one of s, tick");
            return false;
        }

        if (!units_obj.contains("default_rate") || !units_obj["default_rate"].is_string()) {
            addFatal("F-5", manifest_rel_name, "/units/default_rate",
                     "required string ('tick', 's', 'min', or 'h')");
            return false;
        }
        std::string dr_str = units_obj["default_rate"].get<std::string>();
        auto dr_opt = rateUnitFromString(dr_str);
        if (!dr_opt.has_value()) {
            addFatal("F-5", manifest_rel_name, "/units/default_rate",
                     "'" + dr_str + "' is not one of tick, s, min, h");
            return false;
        }
        m_pkg.units.default_rate = *dr_opt;

        if (units_obj.contains("ticks_per_second")) {
            if (!units_obj["ticks_per_second"].is_number()) {
                addFatal("F-5", manifest_rel_name, "/units/ticks_per_second",
                         "ticks_per_second must be a number > 0");
                return false;
            }
            double tps = units_obj["ticks_per_second"].get<double>();
            if (!std::isfinite(tps) || tps <= 0.0) {
                addFatal("F-5", manifest_rel_name, "/units/ticks_per_second",
                         "ticks_per_second must be > 0");
                return false;
            }
            m_pkg.units.ticks_per_second = tps;
            m_pkg.units.has_ticks_per_second = true;
        } else if (m_pkg.units.recipe_time == RecipeTimeUnit::Ticks ||
                   m_pkg.units.default_rate == RateUnit::PerTick) {
            addFatal("F-5", manifest_rel_name, "/units/ticks_per_second",
                     "ticks_per_second is required when recipe_time or default_rate is 'tick'");
            return false;
        }

        // Now process any non-fatal duplicate keys from the manifest SAX pass
        processDuplicateKeysFromSax(manifest_rel_name, parsed.duplicate_keys);

        static const std::vector<std::string> kUnitsKeys = {
            "default_rate", "notes", "recipe_time", "ticks_per_second"
        };
        checkAllowedKeys(units_obj, manifest_rel_name, "/units", kUnitsKeys);

        // icon_root (§2.1, V-INC-3, V-STRUCT-2)
        std::string icon_root_rel = "icons";
        if (m.contains("icon_root")) {
            if (!m["icon_root"].is_string()) {
                addError("V-STRUCT-2", manifest_rel_name, "/icon_root",
                         "expected string for 'icon_root', got " + jsonTypeName(m["icon_root"]));
            } else {
                icon_root_rel = m["icon_root"].get<std::string>();
                if (iconRootEscapesGameDir(std::filesystem::path(icon_root_rel))) {
                    addError("V-INC-3", manifest_rel_name, "/icon_root",
                             "icon_root '" + icon_root_rel + "' escapes the game directory");
                }
            }
        }
        if (!m_packageRoot.empty()) {
            m_pkg.icon_root = (m_packageRoot / icon_root_rel).lexically_normal();
        } else {
            m_pkg.icon_root = std::filesystem::path(icon_root_rel).lexically_normal();
        }

        // Collect manifest sections first
        collectTopLevelSections(m, manifest_rel_name, true);

        // Process include shards (§2.9, V-INC-1..3)
        if (m.contains("include")) {
            const json& inc = m["include"];
            if (!inc.is_array()) {
                addError("V-STRUCT-2", manifest_rel_name, "/include",
                         "expected array for 'include', got " + jsonTypeName(inc));
            } else {
                std::unordered_set<std::string> seen_shard_paths;
                seen_shard_paths.insert("game.json");

                for (size_t i = 0; i < inc.size(); ++i) {
                    std::string ptr = "/include/" + std::to_string(i);
                    if (!inc[i].is_string()) {
                        addError("V-STRUCT-2", manifest_rel_name, ptr,
                                 "expected string in 'include', got " + jsonTypeName(inc[i]));
                        continue;
                    }
                    std::string raw_inc = inc[i].get<std::string>();
                    if (raw_inc.empty()) {
                        addError("V-INC-1", manifest_rel_name, ptr, "empty include path");
                        markDropped("include", raw_inc);
                        continue;
                    }

                    std::filesystem::path rel_inc(raw_inc);
                    if (pathEscapesRoot(rel_inc)) {
                        addError("V-INC-3", manifest_rel_name, ptr,
                                 "include path '" + raw_inc + "' escapes the package directory");
                        markDropped("include", raw_inc);
                        continue;
                    }

                    expandAndLoadIncludeEntry(raw_inc, rel_inc, ptr, manifest_rel_name, seen_shard_paths);
                }
            }
        }

        return true;
    }

    void expandAndLoadIncludeEntry(
        const std::string& raw_inc,
        const std::filesystem::path& rel_inc,
        const std::string& inc_ptr,
        const std::string& manifest_rel_name,
        std::unordered_set<std::string>& seen_shard_paths) {
        std::string norm_rel = rel_inc.lexically_normal().generic_string();
        if (norm_rel == ".") {
            norm_rel = "";
        }

        // 1. Check virtual shards first (for unit tests)
        if (m_virtualShards != nullptr && !m_virtualShards->empty()) {
            auto exact_it = m_virtualShards->find(norm_rel);
            if (exact_it != m_virtualShards->end() &&
                (raw_inc.empty() || raw_inc.back() != '/')) {
                if (seen_shard_paths.insert(norm_rel).second) {
                    loadSingleShardText(norm_rel, exact_it->second);
                }
                return;
            }

            // Check if norm_rel is a virtual directory prefix
            std::string prefix = norm_rel.empty() ? "" : (norm_rel + "/");
            std::vector<std::string> matched_virtual_files;
            for (const auto& [vpath, _] : *m_virtualShards) {
                std::string norm_vpath = std::filesystem::path(vpath).lexically_normal().generic_string();
                if (norm_vpath == "game.json") {
                    continue;
                }
                bool under_dir = prefix.empty() ||
                                 (norm_vpath.size() > prefix.size() &&
                                  norm_vpath.compare(0, prefix.size(), prefix) == 0);
                bool is_json = norm_vpath.size() >= 5 &&
                               norm_vpath.compare(norm_vpath.size() - 5, 5, ".json") == 0;
                if (under_dir && is_json) {
                    matched_virtual_files.push_back(vpath);
                }
            }
            if (!matched_virtual_files.empty()) {
                std::sort(matched_virtual_files.begin(), matched_virtual_files.end());
                for (const std::string& vf : matched_virtual_files) {
                    std::string norm_vf = std::filesystem::path(vf).lexically_normal().generic_string();
                    if (seen_shard_paths.insert(norm_vf).second) {
                        loadSingleShardText(norm_vf, m_virtualShards->at(vf));
                    }
                }
                return;
            }
        }

        // 2. Check disk filesystem if packageRoot is provided
        if (!m_packageRoot.empty()) {
            std::error_code ec;
            std::filesystem::path full_path = m_packageRoot / rel_inc;
            std::filesystem::path canon_root = std::filesystem::weakly_canonical(m_packageRoot, ec);
            std::filesystem::path canon_target = std::filesystem::weakly_canonical(full_path, ec);

            // Verify canon_target is inside canon_root (V-INC-3)
            if (!ec) {
                auto rel_check = canon_target.lexically_relative(canon_root);
                if (rel_check.empty() || pathEscapesRoot(rel_check)) {
                    addError("V-INC-3", manifest_rel_name, inc_ptr,
                             "include path '" + raw_inc + "' escapes the package directory");
                    markDropped("include", raw_inc);
                    return;
                }
            }

            if (!std::filesystem::exists(full_path, ec)) {
                addError("V-INC-1", manifest_rel_name, inc_ptr,
                         "'" + raw_inc + "' not found");
                markDropped("include", raw_inc);
                return;
            }

            if (std::filesystem::is_directory(full_path, ec)) {
                std::vector<std::pair<std::string, std::filesystem::path>> dir_files;
                for (const auto& entry : std::filesystem::recursive_directory_iterator(
                         full_path, std::filesystem::directory_options::skip_permission_denied, ec)) {
                    if (ec) break;
                    if (!entry.is_regular_file(ec)) continue;
                    if (entry.path().extension() != ".json") continue;
                    std::filesystem::path canon_entry = std::filesystem::weakly_canonical(entry.path(), ec);
                    auto rel_to_root = canon_entry.lexically_relative(canon_root);
                    if (rel_to_root.empty() || pathEscapesRoot(rel_to_root)) {
                        addError("V-INC-3", manifest_rel_name, inc_ptr,
                                 "shard '" + entry.path().generic_string() + "' escapes the package directory");
                        continue;
                    }
                    std::string rel_generic = rel_to_root.generic_string();
                    if (rel_generic == "game.json") {
                        continue;
                    }
                    dir_files.emplace_back(rel_generic, entry.path());
                }
                // Sort by relative path in byte order (§2.9)
                std::sort(dir_files.begin(), dir_files.end(),
                          [](const auto& a, const auto& b) { return a.first < b.first; });

                for (const auto& [rel_str, fpath] : dir_files) {
                    if (seen_shard_paths.insert(rel_str).second) {
                        loadSingleShardFile(rel_str, fpath);
                    }
                }
                return;
            }

            if (std::filesystem::is_regular_file(full_path, ec)) {
                std::string rel_generic = canon_target.lexically_relative(canon_root).generic_string();
                if (rel_generic != "game.json" && seen_shard_paths.insert(rel_generic).second) {
                    loadSingleShardFile(rel_generic, full_path);
                }
                return;
            }
        }

        addError("V-INC-1", manifest_rel_name, inc_ptr, "'" + raw_inc + "' not found");
        markDropped("include", raw_inc);
    }

    void loadSingleShardFile(const std::string& rel_name, const std::filesystem::path& full_path) {
        std::ifstream ifs(full_path, std::ios::binary);
        if (!ifs) {
            addError("V-INC-1", rel_name, "", "cannot open shard file '" + rel_name + "'");
            markDropped("shards", rel_name);
            return;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        loadSingleShardText(rel_name, ss.str());
    }

    void loadSingleShardText(const std::string& rel_name, std::string_view text) {
        SaxParseOutcome parsed = parseJsonWithDuplicateCheck(text);
        if (!parsed.ok) {
            addError(
                "V-INC-2", rel_name, "",
                "JSON parse error: " + parsed.error_message,
                parsed.error_line, parsed.error_col);
            markDropped("shards", rel_name);
            return;
        }
        if (!parsed.root.is_object()) {
            addError("V-INC-2", rel_name, "/", "shard root must be a JSON object");
            markDropped("shards", rel_name);
            return;
        }
        processDuplicateKeysFromSax(rel_name, parsed.duplicate_keys);
        collectTopLevelSections(parsed.root, rel_name, false);
    }

    // ========================================================================
    // Stage 1: game, resource_kinds, attributes (§2.2, §2.4, §2.5)
    // ========================================================================

    void runStage1() {
        // 1. Validate game object fields and game.attrs
        const json& game_obj = m_manifestJson["game"];
        static const std::vector<std::string> kGameKeys = {
            "attrs", "data_version", "id", "name", "notes"
        };
        checkAllowedKeys(game_obj, "game.json", "/game", kGameKeys);

        if (game_obj.contains("name")) {
            if (!game_obj["name"].is_string()) {
                addError("V-STRUCT-2", "game.json", "/game/name",
                         "expected string for 'name', got " + jsonTypeName(game_obj["name"]));
                m_pkg.game.name = m_pkg.game.id;
            } else {
                m_pkg.game.name = game_obj["name"].get<std::string>();
            }
        } else {
            m_pkg.game.name = m_pkg.game.id;
        }

        if (game_obj.contains("data_version")) {
            if (!game_obj["data_version"].is_string()) {
                addError("V-STRUCT-2", "game.json", "/game/data_version",
                         "expected string for 'data_version', got " + jsonTypeName(game_obj["data_version"]));
            } else {
                m_pkg.game.data_version = game_obj["data_version"].get<std::string>();
            }
        }

        if (game_obj.contains("attrs")) {
            const json& gattrs = game_obj["attrs"];
            if (!gattrs.is_object()) {
                addError("V-STRUCT-2", "game.json", "/game/attrs",
                         "expected object for 'game.attrs', got " + jsonTypeName(gattrs));
            } else {
                for (auto it = gattrs.begin(); it != gattrs.end(); ++it) {
                    const std::string& k = it.key();
                    std::string ptr = "/game/attrs/" + k;
                    if (!expr::isValidIdentifierId(k)) {
                        addError("V-ID-1", "game.json", ptr,
                                 "invalid game attribute name '" + k + "' (must match [a-z_][a-z0-9_]*)");
                        m_failedAttrs["game"].insert(k);
                        markDropped("attributes", "game." + k);
                        continue;
                    }
                    if (isBuiltinEntityField("game", k) || expr::isReservedIdentifier(k)) {
                        addError("V-ATTR-2", "game.json", ptr,
                                 "'" + k + "' shadows a built-in game field or reserved name");
                        m_failedAttrs["game"].insert(k);
                        markDropped("attributes", "game." + k);
                        continue;
                    }
                    const json& val = it.value();
                    if (val.is_number()) {
                        double d = val.get<double>();
                        if (!std::isfinite(d)) {
                            addError("V-ATTR-2", "game.json", ptr, "game attribute must be a finite number");
                            m_failedAttrs["game"].insert(k);
                            markDropped("attributes", "game." + k);
                            continue;
                        }
                        m_pkg.game.attrs[k] = expr::EvalValue::number(d);
                        m_pkg.game.attr_types[k] = expr::AttrType::Number;
                    } else if (val.is_boolean()) {
                        m_pkg.game.attrs[k] = expr::EvalValue::boolean(val.get<bool>());
                        m_pkg.game.attr_types[k] = expr::AttrType::Bool;
                    } else if (val.is_string()) {
                        m_pkg.game.attrs[k] = expr::EvalValue::string(val.get<std::string>());
                        m_pkg.game.attr_types[k] = expr::AttrType::String;
                    } else {
                        addError("V-ATTR-2", "game.json", ptr,
                                 "game attribute must be number, bool, or string, got " + jsonTypeName(val));
                        m_failedAttrs["game"].insert(k);
                        markDropped("attributes", "game." + k);
                    }
                }
            }
        }

        // 2. Validate resource_kinds (default 'item' always present unless overridden)
        if (m_rawResourceKinds.find("item") == m_rawResourceKinds.end()) {
            ResourceKindDef def_item;
            def_item.id = "item";
            def_item.label = "Item";
            def_item.display_unit = "";
            def_item.display_scale = 1.0;
            m_pkg.resource_kind_index["item"] = m_pkg.resource_kinds.size();
            m_pkg.resource_kinds.push_back(std::move(def_item));
        }

        static const std::vector<std::string> kResourceKindKeys = {
            "display_scale", "display_unit", "label", "notes"
        };

        for (const std::string& kind_id : m_resourceKindOrder) {
            const RawEntry& raw = m_rawResourceKinds.at(kind_id);
            bool ok = true;
            if (!expr::isValidSlugId(kind_id)) {
                addError("V-ID-1", raw.file, raw.json_pointer,
                         "invalid resource_kind id '" + kind_id + "' (must match [a-z0-9_-]+)");
                ok = false;
            }
            if (!raw.value.is_object()) {
                addError("V-STRUCT-2", raw.file, raw.json_pointer,
                         "expected object for resource_kind, got " + jsonTypeName(raw.value));
                ok = false;
            } else {
                if (!checkAllowedKeys(raw.value, raw.file, raw.json_pointer, kResourceKindKeys)) {
                    ok = false;
                }
                ResourceKindDef def;
                def.id = kind_id;
                def.label = kind_id;
                if (raw.value.contains("label")) {
                    if (!raw.value["label"].is_string()) {
                        addError("V-STRUCT-2", raw.file, raw.json_pointer + "/label",
                                 "expected string for 'label', got " + jsonTypeName(raw.value["label"]));
                        ok = false;
                    } else {
                        def.label = raw.value["label"].get<std::string>();
                    }
                }
                if (raw.value.contains("display_unit")) {
                    if (!raw.value["display_unit"].is_string()) {
                        addError("V-STRUCT-2", raw.file, raw.json_pointer + "/display_unit",
                                 "expected string for 'display_unit', got " +
                                     jsonTypeName(raw.value["display_unit"]));
                        ok = false;
                    } else {
                        def.display_unit = raw.value["display_unit"].get<std::string>();
                    }
                }
                if (raw.value.contains("display_scale")) {
                    if (!raw.value["display_scale"].is_number()) {
                        addError("V-STRUCT-2", raw.file, raw.json_pointer + "/display_scale",
                                 "expected number for 'display_scale', got " +
                                     jsonTypeName(raw.value["display_scale"]));
                        ok = false;
                    } else {
                        double sc = raw.value["display_scale"].get<double>();
                        if (!std::isfinite(sc) || sc <= 0.0) {
                            addError("V-NUM-1", raw.file, raw.json_pointer + "/display_scale",
                                     "display_scale must be > 0");
                            ok = false;
                        } else {
                            def.display_scale = sc;
                        }
                    }
                }
                if (ok) {
                    m_pkg.resource_kind_index[kind_id] = m_pkg.resource_kinds.size();
                    m_pkg.resource_kinds.push_back(std::move(def));
                }
            }
            if (!ok) {
                m_failedResourceKinds.insert(kind_id);
                markDropped("resource_kinds", kind_id);
            }
        }

        // 3. Validate attribute declarations (/attributes/<entity_kind>/<attr_name>)
        for (const char* ek : {"machine", "recipe", "port", "resource"}) {
            const auto& order_vec = m_attributeOrder[ek];
            const auto& raw_map = m_rawAttributes[ek];
            for (const std::string& attr_name : order_vec) {
                const RawEntry& raw = raw_map.at(attr_name);
                AttributeDecl decl;
                if (validateAttributeDeclaration(ek, attr_name, raw.value, raw.file, raw.json_pointer, decl)) {
                    m_pkg.attributes[ek][attr_name] = std::move(decl);
                } else {
                    m_failedAttrs[ek].insert(attr_name);
                    markDropped("attributes", std::string(ek) + "." + attr_name);
                }
            }
        }
    }

    bool validateAttributeDeclaration(
        std::string_view entity_kind,
        const std::string& attr_name,
        const json& j,
        const std::string& file,
        const std::string& ptr,
        AttributeDecl& out_decl) {
        bool ok = true;
        if (!expr::isValidIdentifierId(attr_name)) {
            addError("V-ID-1", file, ptr,
                     "invalid attribute name '" + attr_name + "' (must match [a-z_][a-z0-9_]*)");
            ok = false;
        } else if (isBuiltinEntityField(entity_kind, attr_name)) {
            addError("V-ATTR-2", file, ptr,
                     "'" + attr_name + "' is a built-in " + std::string(entity_kind) + " field");
            ok = false;
        } else if (expr::isReservedIdentifier(attr_name)) {
            addError("V-ATTR-2", file, ptr,
                     "'" + attr_name + "' is a reserved identifier or built-in function name");
            ok = false;
        }

        if (!j.is_object()) {
            addError("V-STRUCT-2", file, ptr,
                     "expected object for attribute declaration, got " + jsonTypeName(j));
            return false;
        }

        static const std::vector<std::string> kAttrDeclKeys = {"default", "notes", "type"};
        if (!checkAllowedKeys(j, file, ptr, kAttrDeclKeys)) {
            ok = false;
        }

        if (!j.contains("type") || !j["type"].is_string()) {
            addError("V-ATTR-2", file, ptr + "/type",
                     "attribute declaration requires 'type' ('number', 'bool', or 'string')");
            return false;
        }
        std::string t_str = j["type"].get<std::string>();
        if (t_str == "number") {
            out_decl.type = expr::AttrType::Number;
        } else if (t_str == "bool") {
            out_decl.type = expr::AttrType::Bool;
        } else if (t_str == "string") {
            out_decl.type = expr::AttrType::String;
        } else {
            addError("V-ATTR-2", file, ptr + "/type",
                     "unknown attribute type '" + t_str + "' (expected 'number', 'bool', or 'string')");
            return false;
        }

        if (!j.contains("default")) {
            addError("V-ATTR-2", file, ptr + "/default",
                     "attribute declaration '" + attr_name + "' requires a 'default' value");
            return false;
        }

        const json& def_j = j["default"];
        if (!parseTypedAttrValue(out_decl.type, def_j, file, ptr + "/default", out_decl.default_value)) {
            ok = false;
        }

        out_decl.name = attr_name;
        return ok;
    }

    bool parseTypedAttrValue(
        expr::AttrType expected_type,
        const json& val,
        const std::string& file,
        const std::string& ptr,
        expr::EvalValue& out_val) {
        switch (expected_type) {
            case expr::AttrType::Number: {
                if (!val.is_number()) {
                    addError("V-ATTR-2", file, ptr,
                             "expected number attribute value, got " + jsonTypeName(val));
                    return false;
                }
                double d = val.get<double>();
                if (!std::isfinite(d)) {
                    addError("V-ATTR-2", file, ptr, "attribute number value must be finite");
                    return false;
                }
                out_val = expr::EvalValue::number(d);
                return true;
            }
            case expr::AttrType::Bool: {
                if (!val.is_boolean()) {
                    addError("V-ATTR-2", file, ptr,
                             "expected bool attribute value, got " + jsonTypeName(val));
                    return false;
                }
                out_val = expr::EvalValue::boolean(val.get<bool>());
                return true;
            }
            case expr::AttrType::String: {
                if (!val.is_string()) {
                    addError("V-ATTR-2", file, ptr,
                             "expected string attribute value, got " + jsonTypeName(val));
                    return false;
                }
                out_val = expr::EvalValue::string(val.get<std::string>());
                return true;
            }
        }
        return false;
    }

    bool validateEntityAttrs(
        const std::string& entity_kind,
        const std::string& decl_path_prefix,
        const std::unordered_map<std::string, AttributeDecl>& declared_attrs,
        const std::unordered_set<std::string>& failed_attrs_for_kind,
        const json& attrs_json,
        const std::string& file,
        const std::string& ptr,
        std::unordered_map<std::string, expr::EvalValue>& out_attrs,
        bool& out_cascade_dropped) {
        if (!attrs_json.is_object()) {
            addError("V-STRUCT-2", file, ptr,
                     "expected object for 'attrs', got " + jsonTypeName(attrs_json));
            return false;
        }
        bool ok = true;
        std::vector<std::string> candidates;
        candidates.reserve(declared_attrs.size());
        for (const auto& [k, _] : declared_attrs) {
            candidates.push_back(k);
        }
        std::sort(candidates.begin(), candidates.end());

        for (auto it = attrs_json.begin(); it != attrs_json.end(); ++it) {
            const std::string& attr_name = it.key();
            if (attr_name == "notes" && it.value().is_string()) {
                continue;
            }
            std::string attr_ptr = ptr + "/" + attr_name;
            auto decl_it = declared_attrs.find(attr_name);
            if (decl_it == declared_attrs.end()) {
                if (failed_attrs_for_kind.count(attr_name)) {
                    out_cascade_dropped = true;
                    ok = false;
                    continue;
                }
                // V-ATTR-1: include "did you mean" and ready-to-paste declaration snippet (§9.2)
                std::string inferred_type = "number";
                std::string inferred_def = "0";
                if (it.value().is_boolean()) {
                    inferred_type = "bool";
                    inferred_def = "false";
                } else if (it.value().is_string()) {
                    inferred_type = "string";
                    inferred_def = "\"\"";
                }
                std::string msg = "undeclared " + entity_kind + " attribute '" + attr_name + "' (";
                auto sug = expr::findBestSuggestion(attr_name, candidates, &m_suggestionBudget);
                if (sug.has_value()) {
                    msg += "did you mean '" + *sug + "'? Or ";
                }
                msg += "declare it under " + decl_path_prefix + ": \"" + attr_name +
                       "\": { \"type\": \"" + inferred_type + "\", \"default\": " + inferred_def + " })";
                addError("V-ATTR-1", file, attr_ptr, std::move(msg));
                ok = false;
                continue;
            }

            expr::EvalValue eval_val;
            if (!parseTypedAttrValue(decl_it->second.type, it.value(), file, attr_ptr, eval_val)) {
                ok = false;
                continue;
            }
            out_attrs[attr_name] = std::move(eval_val);
        }
        return ok;
    }

    // ========================================================================
    // Stage 2: Resources (§2.6)
    // ========================================================================

    void runStage2() {
        static const std::vector<std::string> kResourceKeys = {
            "attrs", "id", "kind", "name", "notes", "tags"
        };
        std::unordered_map<std::string, std::pair<std::string, std::string>> first_seen_res;

        for (const RawArrayItem& item : m_rawResources) {
            if (!item.value.is_object()) {
                addError("V-STRUCT-2", item.file, item.json_pointer,
                         "expected object for resource, got " + jsonTypeName(item.value));
                continue;
            }
            const json& rj = item.value;
            bool ok = checkAllowedKeys(rj, item.file, item.json_pointer, kResourceKeys);
            bool cascade = false;

            std::string id;
            if (!rj.contains("id")) {
                addError("V-ID-1", item.file, item.json_pointer + "/id", "missing required resource 'id'");
                ok = false;
            } else if (!rj["id"].is_string()) {
                addError("V-STRUCT-2", item.file, item.json_pointer + "/id",
                         "expected string for resource 'id', got " + jsonTypeName(rj["id"]));
                ok = false;
            } else {
                id = rj["id"].get<std::string>();
                if (!expr::isValidSlugId(id)) {
                    addError("V-ID-1", item.file, item.json_pointer + "/id",
                             "invalid id '" + id + "' (must match [a-z0-9_-]+)");
                    ok = false;
                }
            }

            if (!id.empty()) {
                auto seen_it = first_seen_res.find(id);
                if (seen_it != first_seen_res.end()) {
                    addError("V-ID-2", item.file, item.json_pointer + "/id",
                             "duplicate resource id '" + id + "' (first defined at " +
                                 seen_it->second.first + ": " + seen_it->second.second + ")");
                    markDropped("resources", id);
                    continue;
                }
                first_seen_res[id] = {item.file, item.json_pointer};
            }

            Resource res;
            res.id = id;
            res.name = id;

            if (rj.contains("name")) {
                if (!rj["name"].is_string()) {
                    addError("V-STRUCT-2", item.file, item.json_pointer + "/name",
                             "expected string for 'name', got " + jsonTypeName(rj["name"]));
                    ok = false;
                } else {
                    res.name = rj["name"].get<std::string>();
                }
            }

            if (rj.contains("kind")) {
                if (!rj["kind"].is_string()) {
                    addError("V-STRUCT-2", item.file, item.json_pointer + "/kind",
                             "expected string for 'kind', got " + jsonTypeName(rj["kind"]));
                    ok = false;
                } else {
                    res.kind = rj["kind"].get<std::string>();
                }
            }

            if (m_pkg.findResourceKind(res.kind) == nullptr) {
                if (m_failedResourceKinds.count(res.kind)) {
                    cascade = true;
                    ok = false;
                } else {
                    addError("V-REF-5", item.file, item.json_pointer + "/kind",
                             "unknown resource kind '" + res.kind + "'");
                    ok = false;
                }
            }

            if (rj.contains("tags")) {
                if (!rj["tags"].is_array()) {
                    addError("V-STRUCT-2", item.file, item.json_pointer + "/tags",
                             "expected array for 'tags', got " + jsonTypeName(rj["tags"]));
                    ok = false;
                } else {
                    for (size_t ti = 0; ti < rj["tags"].size(); ++ti) {
                        const json& tj = rj["tags"][ti];
                        std::string tptr = item.json_pointer + "/tags/" + std::to_string(ti);
                        if (!tj.is_string()) {
                            addError("V-STRUCT-2", item.file, tptr,
                                     "expected string tag, got " + jsonTypeName(tj));
                            ok = false;
                        } else {
                            std::string tag_str = tj.get<std::string>();
                            if (!expr::isValidSlugId(tag_str)) {
                                addError("V-ID-1", item.file, tptr,
                                         "invalid tag '" + tag_str + "' (must match [a-z0-9_-]+)");
                                ok = false;
                            } else {
                                if (res.tag_set.insert(tag_str).second) {
                                    res.tags.push_back(tag_str);
                                }
                            }
                        }
                    }
                }
            }

            if (rj.contains("attrs")) {
                if (!validateEntityAttrs(
                        "resource", "/attributes/resource",
                        m_pkg.attributes["resource"], m_failedAttrs["resource"],
                        rj["attrs"], item.file, item.json_pointer + "/attrs",
                        res.attrs, cascade)) {
                    ok = false;
                }
            }

            if (ok && !cascade && !res.id.empty()) {
                m_pkg.resource_index[res.id] = m_pkg.resources.size();
                m_pkg.resources.push_back(std::move(res));
            } else {
                for (const auto& t : res.tags) {
                    m_tagsOfFailedResources.insert(t);
                }
                if (!id.empty()) {
                    m_failedResources.insert(id);
                    markDropped("resources", id);
                }
            }
        }
    }

    // ========================================================================
    // Stage 3: Option Sets & Tables (§3.3, §3.4)
    // ========================================================================

    void runStage3() {
        static const std::vector<std::string> kOptionSetKeys = {"attrs", "notes", "options"};
        static const std::vector<std::string> kOptionKeys = {"attrs", "key", "label", "notes"};

        // 1. Option sets
        for (const std::string& set_id : m_optionSetOrder) {
            const RawEntry& raw = m_rawOptionSets.at(set_id);
            bool ok = true;
            if (!expr::isValidSlugId(set_id)) {
                addError("V-ID-1", raw.file, raw.json_pointer,
                         "invalid option_set id '" + set_id + "' (must match [a-z0-9_-]+)");
                ok = false;
            }
            if (!raw.value.is_object()) {
                addError("V-STRUCT-2", raw.file, raw.json_pointer,
                         "expected object for option_set, got " + jsonTypeName(raw.value));
                m_failedOptionSets.insert(set_id);
                markDropped("option_sets", set_id);
                continue;
            }

            if (!checkAllowedKeys(raw.value, raw.file, raw.json_pointer, kOptionSetKeys)) {
                ok = false;
            }

            OptionSet oset;
            oset.id = set_id;
            std::unordered_set<std::string> failed_opt_attrs;

            if (raw.value.contains("attrs")) {
                const json& attrs_j = raw.value["attrs"];
                std::string attrs_ptr = raw.json_pointer + "/attrs";
                if (!attrs_j.is_object()) {
                    addError("V-STRUCT-2", raw.file, attrs_ptr,
                             "expected object for option_set 'attrs', got " + jsonTypeName(attrs_j));
                    ok = false;
                } else {
                    for (auto it = attrs_j.begin(); it != attrs_j.end(); ++it) {
                        const std::string& aname = it.key();
                        if (aname == "notes" && it.value().is_string()) continue;
                        AttributeDecl decl;
                        if (validateAttributeDeclaration(
                                "option", aname, it.value(), raw.file, attrs_ptr + "/" + aname, decl)) {
                            oset.attrs[aname] = std::move(decl);
                        } else {
                            failed_opt_attrs.insert(aname);
                            ok = false;
                        }
                    }
                }
            }

            if (!raw.value.contains("options")) {
                addError("V-STRUCT-2", raw.file, raw.json_pointer + "/options",
                         "option_set requires 'options' array");
                ok = false;
            } else if (!raw.value["options"].is_array()) {
                addError("V-STRUCT-2", raw.file, raw.json_pointer + "/options",
                         "expected array for 'options', got " + jsonTypeName(raw.value["options"]));
                ok = false;
            } else {
                const json& opts_arr = raw.value["options"];
                if (opts_arr.empty()) {
                    addError("V-PAR-3", raw.file, raw.json_pointer + "/options",
                             "option_set '" + set_id + "' must contain at least one option");
                    ok = false;
                }
                for (size_t oi = 0; oi < opts_arr.size(); ++oi) {
                    const json& oj = opts_arr[oi];
                    std::string opt_ptr = raw.json_pointer + "/options/" + std::to_string(oi);
                    if (!oj.is_object()) {
                        addError("V-STRUCT-2", raw.file, opt_ptr,
                                 "expected object for option, got " + jsonTypeName(oj));
                        ok = false;
                        continue;
                    }
                    if (!checkAllowedKeys(oj, raw.file, opt_ptr, kOptionKeys)) {
                        ok = false;
                    }
                    OptionDef opt;
                    if (!oj.contains("key")) {
                        addError("V-ID-1", raw.file, opt_ptr + "/key", "missing required option 'key'");
                        ok = false;
                    } else if (!oj["key"].is_string()) {
                        addError("V-STRUCT-2", raw.file, opt_ptr + "/key",
                                 "expected string for option 'key', got " + jsonTypeName(oj["key"]));
                        ok = false;
                    } else {
                        opt.key = oj["key"].get<std::string>();
                        if (!expr::isValidSlugId(opt.key)) {
                            addError("V-ID-1", raw.file, opt_ptr + "/key",
                                     "invalid option key '" + opt.key + "' (must match [a-z0-9_-]+)");
                            ok = false;
                        } else if (oset.option_index_by_key.count(opt.key)) {
                            addError("V-PAR-3", raw.file, opt_ptr + "/key",
                                     "duplicate option key '" + opt.key + "' in option_set '" + set_id + "'");
                            ok = false;
                        }
                    }

                    opt.label = opt.key;
                    if (oj.contains("label")) {
                        if (!oj["label"].is_string()) {
                            addError("V-STRUCT-2", raw.file, opt_ptr + "/label",
                                     "expected string for 'label', got " + jsonTypeName(oj["label"]));
                            ok = false;
                        } else {
                            opt.label = oj["label"].get<std::string>();
                        }
                    }

                    if (oj.contains("attrs")) {
                        bool opt_cascade = false;
                        if (!validateEntityAttrs(
                                "option", "/option_sets/" + set_id + "/attrs",
                                oset.attrs, failed_opt_attrs,
                                oj["attrs"], raw.file, opt_ptr + "/attrs",
                                opt.attrs, opt_cascade)) {
                            ok = false;
                        }
                    }

                    if (!opt.key.empty() && !oset.option_index_by_key.count(opt.key)) {
                        oset.option_index_by_key[opt.key] = oset.options.size();
                        oset.options.push_back(std::move(opt));
                    }
                }
            }

            if (ok) {
                m_pkg.option_set_index[set_id] = m_pkg.option_sets.size();
                m_pkg.option_sets.push_back(std::move(oset));
            } else {
                m_failedOptionSets.insert(set_id);
                markDropped("option_sets", set_id);
            }
        }

        // 2. Tables
        for (const std::string& tbl_id : m_tableOrder) {
            const RawEntry& raw = m_rawTables.at(tbl_id);
            bool ok = true;
            if (!expr::isValidSlugId(tbl_id)) {
                addError("V-ID-1", raw.file, raw.json_pointer,
                         "invalid table id '" + tbl_id + "' (must match [a-z0-9_-]+)");
                ok = false;
            }

            TableDef tbl;
            tbl.id = tbl_id;

            if (raw.value.is_array()) {
                tbl.kind = expr::TableKind::Array;
                if (raw.value.empty()) {
                    addError("V-TBL-1", raw.file, raw.json_pointer,
                             "array table '" + tbl_id + "' cannot be empty");
                    ok = false;
                }
                for (size_t i = 0; i < raw.value.size(); ++i) {
                    const json& elem = raw.value[i];
                    std::string eptr = raw.json_pointer + "/" + std::to_string(i);
                    if (!elem.is_number()) {
                        addError("V-TBL-1", raw.file, eptr,
                                 "table values must be finite numbers, got " + jsonTypeName(elem));
                        ok = false;
                    } else {
                        double d = elem.get<double>();
                        if (!std::isfinite(d)) {
                            addError("V-TBL-1", raw.file, eptr, "table values must be finite numbers");
                            ok = false;
                        } else {
                            tbl.array_values.push_back(expr::normalizeZero(d));
                        }
                    }
                }
            } else if (raw.value.is_object()) {
                tbl.kind = expr::TableKind::Object;
                if (raw.value.empty()) {
                    addError("V-TBL-1", raw.file, raw.json_pointer,
                             "object table '" + tbl_id + "' cannot be empty");
                    ok = false;
                }
                for (auto it = raw.value.begin(); it != raw.value.end(); ++it) {
                    const std::string& k = it.key();
                    const json& elem = it.value();
                    std::string eptr = raw.json_pointer + "/" + k;
                    if (!elem.is_number()) {
                        addError("V-TBL-1", raw.file, eptr,
                                 "table values must be finite numbers, got " + jsonTypeName(elem));
                        ok = false;
                    } else {
                        double d = elem.get<double>();
                        if (!std::isfinite(d)) {
                            addError("V-TBL-1", raw.file, eptr, "table values must be finite numbers");
                            ok = false;
                        } else {
                            tbl.object_values[k] = expr::normalizeZero(d);
                        }
                    }
                }
            } else {
                addError("V-TBL-1", raw.file, raw.json_pointer,
                         "table '" + tbl_id + "' must be a non-empty array or object, got " +
                             jsonTypeName(raw.value));
                ok = false;
            }

            if (ok) {
                m_pkg.table_index[tbl_id] = m_pkg.tables.size();
                m_pkg.tables.push_back(std::move(tbl));
            } else {
                m_failedTables.insert(tbl_id);
                markDropped("tables", tbl_id);
            }
        }
    }

    // ========================================================================
    // Stage 4: Parameters & Globals (§3.1, §3.2)
    // ========================================================================

    struct PendingParam {
        std::string id;
        RawEntry raw;
        ParameterDef def;
    };

    bool validateParamStructure(
        const std::string& id,
        const RawEntry& raw,
        bool is_global,
        ParameterDef& out_def,
        bool& out_cascade) {
        const char* drop_kind = is_global ? "globals" : "parameters";
        bool ok = true;

        if (!expr::isValidIdentifierId(id) || expr::isReservedIdentifier(id)) {
            addError("V-ID-1", raw.file, raw.json_pointer,
                     std::string("invalid ") + (is_global ? "global" : "parameter") + " id '" + id +
                         "' (must match [a-z_][a-z0-9_]* and not be a reserved name)");
            ok = false;
        }

        if (!raw.value.is_object()) {
            addError("V-STRUCT-2", raw.file, raw.json_pointer,
                     "expected object, got " + jsonTypeName(raw.value));
            return false;
        }

        const json& pj = raw.value;
        static const std::vector<std::string> kParamKeys = {
            "allow_empty", "allow_none", "allowed_if", "count", "default", "label",
            "max", "min", "notes", "option_set", "resources", "step", "tag",
            "tooltip", "type", "ui", "unit", "visible_if"
        };
        if (!checkAllowedKeys(pj, raw.file, raw.json_pointer, kParamKeys)) {
            ok = false;
        }

        out_def.id = id;
        out_def.label = id;
        if (pj.contains("label")) {
            if (!pj["label"].is_string()) {
                addError("V-STRUCT-2", raw.file, raw.json_pointer + "/label",
                         "expected string for 'label', got " + jsonTypeName(pj["label"]));
                ok = false;
            } else {
                out_def.label = pj["label"].get<std::string>();
            }
        }
        if (pj.contains("tooltip")) {
            if (!pj["tooltip"].is_string()) {
                addError("V-STRUCT-2", raw.file, raw.json_pointer + "/tooltip",
                         "expected string for 'tooltip', got " + jsonTypeName(pj["tooltip"]));
                ok = false;
            } else {
                out_def.tooltip = pj["tooltip"].get<std::string>();
            }
        }

        if (!pj.contains("type")) {
            addError("V-PAR-1", raw.file, raw.json_pointer + "/type", "missing required 'type'");
            return false;
        }
        if (!pj["type"].is_string()) {
            addError("V-STRUCT-2", raw.file, raw.json_pointer + "/type",
                     "expected string for 'type', got " + jsonTypeName(pj["type"]));
            return false;
        }
        std::string t_str = pj["type"].get<std::string>();
        if (t_str == "number") {
            out_def.type = ParameterType::Number;
        } else if (t_str == "int") {
            out_def.type = ParameterType::Int;
        } else if (t_str == "bool") {
            out_def.type = ParameterType::Bool;
        } else if (t_str == "enum") {
            out_def.type = ParameterType::Enum;
        } else if (t_str == "resource_choice") {
            out_def.type = ParameterType::ResourceChoice;
        } else if (t_str == "slots") {
            out_def.type = ParameterType::Slots;
        } else {
            addError("V-PAR-1", raw.file, raw.json_pointer + "/type",
                     "unknown parameter type '" + t_str + "'");
            return false;
        }

        // V-PAR-4: Check field applicability for the parameter's type
        const bool is_num_or_int = (out_def.type == ParameterType::Number ||
                                    out_def.type == ParameterType::Int);
        const bool is_enum_or_slots = (out_def.type == ParameterType::Enum ||
                                       out_def.type == ParameterType::Slots);
        const bool is_enum_or_res = (out_def.type == ParameterType::Enum ||
                                     out_def.type == ParameterType::ResourceChoice);
        const bool is_choice_like = (out_def.type == ParameterType::Enum ||
                                     out_def.type == ParameterType::Slots ||
                                     out_def.type == ParameterType::ResourceChoice);

        auto forbidFieldIf = [&](bool forbidden, const char* field, const char* valid_for) {
            if (forbidden && pj.contains(field)) {
                addError("V-PAR-4", raw.file, raw.json_pointer + "/" + field,
                         std::string("'") + field + "' only applies to " + valid_for);
                ok = false;
            }
        };

        forbidFieldIf(!is_num_or_int, "min", "types 'number' and 'int'");
        forbidFieldIf(!is_num_or_int, "max", "types 'number' and 'int'");
        forbidFieldIf(!is_num_or_int, "step", "types 'number' and 'int'");
        forbidFieldIf(!is_num_or_int, "unit", "types 'number' and 'int'");
        forbidFieldIf(!is_enum_or_slots, "option_set", "types 'enum' and 'slots'");
        forbidFieldIf(!is_choice_like, "allowed_if", "types 'enum', 'slots', and 'resource_choice'");
        forbidFieldIf(out_def.type != ParameterType::ResourceChoice, "resources", "type 'resource_choice'");
        forbidFieldIf(out_def.type != ParameterType::ResourceChoice, "tag", "type 'resource_choice'");
        forbidFieldIf(!is_enum_or_res, "allow_none", "types 'enum' and 'resource_choice'");
        forbidFieldIf(out_def.type != ParameterType::Slots, "count", "type 'slots'");
        forbidFieldIf(out_def.type != ParameterType::Slots, "allow_empty", "type 'slots'");

        auto parseScalarOrExprField = [&](const char* field, ScalarOrExpr& out_se) {
            if (!pj.contains(field)) return;
            const json& fj = pj[field];
            std::string fptr = raw.json_pointer + "/" + field;
            if (fj.is_number()) {
                double d = fj.get<double>();
                if (!std::isfinite(d)) {
                    addError("V-PAR-2", raw.file, fptr, std::string(field) + " must be finite");
                    ok = false;
                    return;
                }
                out_se.is_present = true;
                out_se.is_literal = true;
                out_se.literal_value = expr::normalizeZero(d);
            } else if (fj.is_string()) {
                out_se.is_present = true;
                out_se.is_literal = false;
                out_se.expr_src = fj.get<std::string>();
                // Check if string is actually a plain number literal (e.g. "min": "1", "max": "250")
                expr::ParseResult pr = expr::parse(out_se.expr_src);
                if (pr.ok && pr.root != nullptr) {
                    if (pr.root->kind == expr::AstKind::NumberLit) {
                        out_se.is_literal = true;
                        out_se.literal_value = pr.root->number_value;
                    } else if (pr.root->kind == expr::AstKind::UnaryOp &&
                               pr.root->unary_op == expr::UnaryOpKind::Negate &&
                               pr.root->left != nullptr &&
                               pr.root->left->kind == expr::AstKind::NumberLit) {
                        out_se.is_literal = true;
                        out_se.literal_value = expr::normalizeZero(-pr.root->left->number_value);
                    }
                }
            } else {
                addError("V-STRUCT-2", raw.file, fptr,
                         std::string("expected number or expression string for '") + field +
                             "', got " + jsonTypeName(fj));
                ok = false;
            }
        };

        auto parseBoolOrExprField = [&](const char* field, BoolOrExpr& out_be) {
            if (!pj.contains(field)) return;
            const json& fj = pj[field];
            std::string fptr = raw.json_pointer + "/" + field;
            if (fj.is_boolean()) {
                out_be.is_present = true;
                out_be.is_literal = true;
                out_be.literal_value = fj.get<bool>();
            } else if (fj.is_string()) {
                out_be.is_present = true;
                out_be.is_literal = false;
                out_be.expr_src = fj.get<std::string>();
                if (out_be.expr_src == "true") {
                    out_be.is_literal = true;
                    out_be.literal_value = true;
                } else if (out_be.expr_src == "false") {
                    out_be.is_literal = true;
                    out_be.literal_value = false;
                }
            } else {
                addError("V-STRUCT-2", raw.file, fptr,
                         std::string("expected bool or expression string for '") + field +
                             "', got " + jsonTypeName(fj));
                ok = false;
            }
        };

        parseBoolOrExprField("visible_if", out_def.visible_if);
        if (is_choice_like) {
            parseBoolOrExprField("allowed_if", out_def.allowed_if);
        }

        // Type-specific validation
        if (is_num_or_int) {
            parseScalarOrExprField("min", out_def.min);
            parseScalarOrExprField("max", out_def.max);

            if (pj.contains("unit")) {
                if (!pj["unit"].is_string()) {
                    addError("V-STRUCT-2", raw.file, raw.json_pointer + "/unit",
                             "expected string for 'unit', got " + jsonTypeName(pj["unit"]));
                    ok = false;
                } else {
                    out_def.unit = pj["unit"].get<std::string>();
                }
            }

            if (pj.contains("step")) {
                if (!pj["step"].is_number()) {
                    addError("V-STRUCT-2", raw.file, raw.json_pointer + "/step",
                             "expected number for 'step', got " + jsonTypeName(pj["step"]));
                    ok = false;
                } else {
                    double st = pj["step"].get<double>();
                    if (!std::isfinite(st) || st <= 0.0) {
                        addError("V-PAR-2", raw.file, raw.json_pointer + "/step",
                                 "step must be > 0");
                        ok = false;
                    } else if (out_def.type == ParameterType::Int && std::floor(st) != st) {
                        addError("V-PAR-2", raw.file, raw.json_pointer + "/step",
                                 "int parameter requires an integer step");
                        ok = false;
                    } else {
                        out_def.step = st;
                        out_def.has_step = true;
                    }
                }
            }

            if (out_def.min.is_present && out_def.min.is_literal &&
                out_def.max.is_present && out_def.max.is_literal) {
                if (out_def.min.literal_value > out_def.max.literal_value) {
                    addError("V-PAR-2", raw.file, raw.json_pointer + "/min",
                             "literal min exceeds literal max");
                    ok = false;
                }
            }

            if (!pj.contains("default")) {
                addError("V-PAR-1", raw.file, raw.json_pointer + "/default",
                         "required for type '" + t_str + "'");
                ok = false;
            } else if (!pj["default"].is_number()) {
                addError("V-PAR-1", raw.file, raw.json_pointer + "/default",
                         "default for type '" + t_str + "' must be a number, got " +
                             jsonTypeName(pj["default"]));
                ok = false;
            } else {
                double def_v = pj["default"].get<double>();
                if (!std::isfinite(def_v)) {
                    addError("V-PAR-1", raw.file, raw.json_pointer + "/default",
                             "default must be finite");
                    ok = false;
                } else if (out_def.type == ParameterType::Int && std::floor(def_v) != def_v) {
                    addError("V-PAR-2", raw.file, raw.json_pointer + "/default",
                             "int parameter default must be an integer");
                    ok = false;
                } else {
                    if (out_def.min.is_present && out_def.min.is_literal &&
                        def_v < out_def.min.literal_value) {
                        addError("V-PAR-2", raw.file, raw.json_pointer + "/default",
                                 "default is below literal min");
                        ok = false;
                    }
                    if (out_def.max.is_present && out_def.max.is_literal &&
                        def_v > out_def.max.literal_value) {
                        addError("V-PAR-2", raw.file, raw.json_pointer + "/default",
                                 "default exceeds literal max");
                        ok = false;
                    }
                    out_def.has_default = true;
                    out_def.default_value = ParamValue::number(def_v);
                }
            }
        } else if (out_def.type == ParameterType::Bool) {
            if (!pj.contains("default")) {
                addError("V-PAR-1", raw.file, raw.json_pointer + "/default",
                         "required for type 'bool'");
                ok = false;
            } else if (!pj["default"].is_boolean()) {
                addError("V-PAR-1", raw.file, raw.json_pointer + "/default",
                         "default for type 'bool' must be bool, got " + jsonTypeName(pj["default"]));
                ok = false;
            } else {
                out_def.has_default = true;
                out_def.default_value = ParamValue::boolean(pj["default"].get<bool>());
            }
        } else if (out_def.type == ParameterType::Enum) {
            if (pj.contains("allow_none")) {
                if (!pj["allow_none"].is_boolean()) {
                    addError("V-STRUCT-2", raw.file, raw.json_pointer + "/allow_none",
                             "expected bool for 'allow_none', got " + jsonTypeName(pj["allow_none"]));
                    ok = false;
                } else {
                    out_def.allow_none = pj["allow_none"].get<bool>();
                }
            }

            const OptionSet* oset = nullptr;
            if (!pj.contains("option_set") || !pj["option_set"].is_string()) {
                addError("V-REF-5", raw.file, raw.json_pointer + "/option_set",
                         "enum parameter requires 'option_set'");
                ok = false;
            } else {
                out_def.option_set = pj["option_set"].get<std::string>();
                oset = m_pkg.findOptionSet(out_def.option_set);
                if (oset == nullptr) {
                    if (m_failedOptionSets.count(out_def.option_set)) {
                        out_cascade = true;
                        ok = false;
                    } else {
                        addError("V-REF-5", raw.file, raw.json_pointer + "/option_set",
                                 "unknown option_set '" + out_def.option_set + "'");
                        ok = false;
                    }
                } else {
                    m_usedOptionSets.insert(out_def.option_set);
                }
            }

            if (pj.contains("default")) {
                const json& def_j = pj["default"];
                if (def_j.is_null()) {
                    if (!out_def.allow_none) {
                        addError("V-PAR-1", raw.file, raw.json_pointer + "/default",
                                 "default cannot be null when allow_none is false");
                        ok = false;
                    } else {
                        out_def.has_default = true;
                        out_def.default_value = ParamValue::null();
                    }
                } else if (def_j.is_string()) {
                    std::string def_key = def_j.get<std::string>();
                    if (oset != nullptr && oset->findOption(def_key) == nullptr) {
                        addError("V-PAR-3", raw.file, raw.json_pointer + "/default",
                                 "'" + def_key + "' is not an option of '" + out_def.option_set + "'");
                        ok = false;
                    } else {
                        out_def.has_default = true;
                        out_def.default_value = ParamValue::string(def_key);
                    }
                } else {
                    addError("V-PAR-1", raw.file, raw.json_pointer + "/default",
                             "default for type 'enum' must be string or null, got " + jsonTypeName(def_j));
                    ok = false;
                }
            }
        } else if (out_def.type == ParameterType::ResourceChoice) {
            if (pj.contains("allow_none")) {
                if (!pj["allow_none"].is_boolean()) {
                    addError("V-STRUCT-2", raw.file, raw.json_pointer + "/allow_none",
                             "expected bool for 'allow_none', got " + jsonTypeName(pj["allow_none"]));
                    ok = false;
                } else {
                    out_def.allow_none = pj["allow_none"].get<bool>();
                }
            }

            const bool has_res_list = pj.contains("resources");
            const bool has_tag = pj.contains("tag");
            if (has_res_list == has_tag) {
                addError("V-REF-5", raw.file, raw.json_pointer,
                         "resource_choice parameter must specify exactly one of 'resources' or 'tag'");
                ok = false;
            } else if (has_res_list) {
                if (!pj["resources"].is_array()) {
                    addError("V-STRUCT-2", raw.file, raw.json_pointer + "/resources",
                             "expected array for 'resources', got " + jsonTypeName(pj["resources"]));
                    ok = false;
                } else if (pj["resources"].empty()) {
                    addError("V-REF-5", raw.file, raw.json_pointer + "/resources",
                             "resource_choice 'resources' list cannot be empty");
                    ok = false;
                } else {
                    for (size_t ri = 0; ri < pj["resources"].size(); ++ri) {
                        const json& r_elem = pj["resources"][ri];
                        std::string r_ptr = raw.json_pointer + "/resources/" + std::to_string(ri);
                        if (!r_elem.is_string()) {
                            addError("V-STRUCT-2", raw.file, r_ptr,
                                     "expected resource id string, got " + jsonTypeName(r_elem));
                            ok = false;
                            continue;
                        }
                        std::string r_id = r_elem.get<std::string>();
                        if (m_pkg.findResource(r_id) == nullptr) {
                            if (m_failedResources.count(r_id)) {
                                out_cascade = true;
                                ok = false;
                            } else {
                                addError("V-REF-1", raw.file, r_ptr,
                                         "unknown resource '" + r_id + "'");
                                ok = false;
                            }
                        } else {
                            out_def.resources.push_back(r_id);
                            addUnique(out_def.candidate_resources, r_id);
                            m_usedResources.insert(r_id);
                        }
                    }
                }
            } else if (has_tag) {
                if (!pj["tag"].is_string()) {
                    addError("V-STRUCT-2", raw.file, raw.json_pointer + "/tag",
                             "expected string for 'tag', got " + jsonTypeName(pj["tag"]));
                    ok = false;
                } else {
                    out_def.tag = pj["tag"].get<std::string>();
                    if (!expr::isValidSlugId(out_def.tag)) {
                        addError("V-ID-1", raw.file, raw.json_pointer + "/tag",
                                 "invalid tag '" + out_def.tag + "' (must match [a-z0-9_-]+)");
                        ok = false;
                    } else {
                        for (const auto& res : m_pkg.resources) {
                            if (res.tag_set.count(out_def.tag)) {
                                out_def.candidate_resources.push_back(res.id);
                                m_usedResources.insert(res.id);
                            }
                        }
                        if (out_def.candidate_resources.empty()) {
                            if (m_tagsOfFailedResources.count(out_def.tag)) {
                                out_cascade = true;
                                ok = false;
                            } else {
                                addError("V-REF-5", raw.file, raw.json_pointer + "/tag",
                                         "no resource has tag '" + out_def.tag + "'");
                                ok = false;
                            }
                        }
                    }
                }
            }

            if (pj.contains("default")) {
                const json& def_j = pj["default"];
                if (def_j.is_null()) {
                    if (!out_def.allow_none) {
                        addError("V-PAR-1", raw.file, raw.json_pointer + "/default",
                                 "default cannot be null when allow_none is false");
                        ok = false;
                    } else {
                        out_def.has_default = true;
                        out_def.default_value = ParamValue::null();
                    }
                } else if (def_j.is_string()) {
                    std::string def_res = def_j.get<std::string>();
                    if (m_pkg.findResource(def_res) == nullptr) {
                        if (m_failedResources.count(def_res)) {
                            out_cascade = true;
                            ok = false;
                        } else {
                            addError("V-REF-1", raw.file, raw.json_pointer + "/default",
                                     "unknown resource '" + def_res + "'");
                            ok = false;
                        }
                    } else if (!out_def.candidate_resources.empty() &&
                               std::find(out_def.candidate_resources.begin(),
                                         out_def.candidate_resources.end(),
                                         def_res) == out_def.candidate_resources.end()) {
                        addError("V-REF-5", raw.file, raw.json_pointer + "/default",
                                 "default resource '" + def_res + "' is not in the candidate set for '" + id + "'");
                        ok = false;
                    } else {
                        out_def.has_default = true;
                        out_def.default_value = ParamValue::string(def_res);
                    }
                } else {
                    addError("V-PAR-1", raw.file, raw.json_pointer + "/default",
                             "default for type 'resource_choice' must be string or null, got " +
                                 jsonTypeName(def_j));
                    ok = false;
                }
            }
        } else if (out_def.type == ParameterType::Slots) {
            if (pj.contains("allow_empty")) {
                if (!pj["allow_empty"].is_boolean()) {
                    addError("V-STRUCT-2", raw.file, raw.json_pointer + "/allow_empty",
                             "expected bool for 'allow_empty', got " + jsonTypeName(pj["allow_empty"]));
                    ok = false;
                } else {
                    out_def.allow_empty = pj["allow_empty"].get<bool>();
                }
            }

            const OptionSet* oset = nullptr;
            if (!pj.contains("option_set") || !pj["option_set"].is_string()) {
                addError("V-REF-5", raw.file, raw.json_pointer + "/option_set",
                         "slots parameter requires 'option_set'");
                ok = false;
            } else {
                out_def.option_set = pj["option_set"].get<std::string>();
                oset = m_pkg.findOptionSet(out_def.option_set);
                if (oset == nullptr) {
                    if (m_failedOptionSets.count(out_def.option_set)) {
                        out_cascade = true;
                        ok = false;
                    } else {
                        addError("V-REF-5", raw.file, raw.json_pointer + "/option_set",
                                 "unknown option_set '" + out_def.option_set + "'");
                        ok = false;
                    }
                } else {
                    m_usedOptionSets.insert(out_def.option_set);
                }
            }

            if (!pj.contains("count")) {
                addError("V-PAR-1", raw.file, raw.json_pointer + "/count",
                         "slots parameter requires 'count'");
                ok = false;
            } else {
                parseScalarOrExprField("count", out_def.count);
                if (out_def.count.is_present && out_def.count.is_literal) {
                    double c = out_def.count.literal_value;
                    if (std::floor(c) != c || c < 0.0 || c > 64.0) {
                        addError("V-PAR-2", raw.file, raw.json_pointer + "/count",
                                 "slots count must be an integer in [0, 64]");
                        ok = false;
                    }
                }
            }

            if (pj.contains("default")) {
                const json& def_j = pj["default"];
                if (!def_j.is_array()) {
                    addError("V-PAR-1", raw.file, raw.json_pointer + "/default",
                             "default for type 'slots' must be an array, got " + jsonTypeName(def_j));
                    ok = false;
                } else {
                    if (out_def.count.is_present && out_def.count.is_literal &&
                        def_j.size() > static_cast<size_t>(std::llround(out_def.count.literal_value))) {
                        addError("V-PAR-2", raw.file, raw.json_pointer + "/default",
                                 "slots default array exceeds literal count");
                        ok = false;
                    }
                    ParamValue::SlotsArray slots_arr;
                    for (size_t si = 0; si < def_j.size(); ++si) {
                        const json& se = def_j[si];
                        std::string sptr = raw.json_pointer + "/default/" + std::to_string(si);
                        if (se.is_null()) {
                            if (!out_def.allow_empty) {
                                addError("V-PAR-3", raw.file, sptr,
                                         "slot default cannot be null when allow_empty is false");
                                ok = false;
                            }
                            slots_arr.push_back(std::nullopt);
                        } else if (se.is_string()) {
                            std::string skey = se.get<std::string>();
                            if (oset != nullptr && oset->findOption(skey) == nullptr) {
                                addError("V-PAR-3", raw.file, sptr,
                                         "'" + skey + "' is not an option of '" + out_def.option_set + "'");
                                ok = false;
                            }
                            slots_arr.push_back(skey);
                        } else {
                            addError("V-PAR-1", raw.file, sptr,
                                     "slots default element must be string or null, got " + jsonTypeName(se));
                            ok = false;
                        }
                    }
                    out_def.has_default = true;
                    out_def.default_value = ParamValue::slots(std::move(slots_arr));
                }
            }
        }

        // UI hints (V-PAR-5 warning on unsuitable widget)
        out_def.ui.widget = defaultWidgetForParameterType(out_def.type);
        if (pj.contains("ui")) {
            const json& uij = pj["ui"];
            std::string ui_ptr = raw.json_pointer + "/ui";
            if (!uij.is_object()) {
                addError("V-STRUCT-2", raw.file, ui_ptr,
                         "expected object for 'ui', got " + jsonTypeName(uij));
                ok = false;
            } else {
                static const std::vector<std::string> kUiKeys = {
                    "format", "group", "notes", "placement", "widget"
                };
                if (!checkAllowedKeys(uij, raw.file, ui_ptr, kUiKeys)) {
                    ok = false;
                }
                if (uij.contains("format")) {
                    if (!uij["format"].is_string()) {
                        addError("V-STRUCT-2", raw.file, ui_ptr + "/format",
                                 "expected string for 'ui.format', got " + jsonTypeName(uij["format"]));
                        ok = false;
                    } else {
                        out_def.ui.format = uij["format"].get<std::string>();
                    }
                }
                if (uij.contains("group")) {
                    if (!uij["group"].is_string()) {
                        addError("V-STRUCT-2", raw.file, ui_ptr + "/group",
                                 "expected string for 'ui.group', got " + jsonTypeName(uij["group"]));
                        ok = false;
                    } else {
                        out_def.ui.group = uij["group"].get<std::string>();
                    }
                }
                if (uij.contains("placement")) {
                    if (!uij["placement"].is_string()) {
                        addError("V-STRUCT-2", raw.file, ui_ptr + "/placement",
                                 "expected string for 'ui.placement', got " + jsonTypeName(uij["placement"]));
                        ok = false;
                    } else {
                        std::string pl = uij["placement"].get<std::string>();
                        if (pl == "body") {
                            out_def.ui.placement = UiPlacement::Body;
                        } else if (pl == "panel") {
                            out_def.ui.placement = UiPlacement::Panel;
                        } else {
                            addError("V-STRUCT-2", raw.file, ui_ptr + "/placement",
                                     "ui.placement must be 'body' or 'panel', got '" + pl + "'");
                            ok = false;
                        }
                    }
                }
                if (uij.contains("widget")) {
                    if (!uij["widget"].is_string()) {
                        addError("V-STRUCT-2", raw.file, ui_ptr + "/widget",
                                 "expected string for 'ui.widget', got " + jsonTypeName(uij["widget"]));
                        ok = false;
                    } else {
                        std::string w_str = uij["widget"].get<std::string>();
                        auto w_opt = parseUiWidget(w_str);
                        UiWidget fallback_w = defaultWidgetForParameterType(out_def.type);
                        if (!w_opt.has_value() || !isSuitableWidgetForType(out_def.type, *w_opt)) {
                            addWarning("V-PAR-5", raw.file, ui_ptr + "/widget",
                                       "'" + w_str + "' unsuitable for " +
                                           parameterTypeToString(out_def.type) + ", using '" +
                                           uiWidgetToString(fallback_w) + "'");
                            out_def.ui.widget = fallback_w;
                        } else {
                            out_def.ui.widget = *w_opt;
                        }
                    }
                }
            }
        }

        (void)drop_kind;
        return ok;
    }

    static expr::ParamCompileInfo makeParamCompileInfo(const ParameterDef& def) {
        switch (def.type) {
            case ParameterType::Number:
            case ParameterType::Int:
                return {expr::ExprType::number(), false};
            case ParameterType::Bool:
                return {expr::ExprType::boolean(), false};
            case ParameterType::Enum:
                return {expr::ExprType::option(def.option_set), def.allow_none};
            case ParameterType::ResourceChoice:
                return {expr::ExprType::resource(), def.allow_none};
            case ParameterType::Slots:
                return {expr::ExprType::slots(def.option_set), false};
        }
        return {expr::ExprType::number(), false};
    }

    expr::CompileEnv buildBaseCompileEnv() {
        expr::CompileEnv env;
        env.suggestion_budget = &m_suggestionBudget;
        env.game_attrs = m_pkg.game.attr_types;
        for (const auto& [k, decl] : m_pkg.attributes["machine"]) {
            env.machine_attrs[k] = decl.type;
        }
        for (const auto& [k, decl] : m_pkg.attributes["recipe"]) {
            env.recipe_attrs[k] = decl.type;
        }
        for (const auto& [k, decl] : m_pkg.attributes["port"]) {
            env.port_attrs[k] = decl.type;
        }
        for (const auto& [k, decl] : m_pkg.attributes["resource"]) {
            env.resource_attrs[k] = decl.type;
        }
        for (const auto& oset : m_pkg.option_sets) {
            expr::OptionSetCompileInfo info;
            for (const auto& opt : oset.options) {
                info.option_keys.push_back(opt.key);
            }
            for (const auto& [ak, adecl] : oset.attrs) {
                info.attrs[ak] = adecl.type;
            }
            env.option_sets[oset.id] = std::move(info);
        }
        for (const auto& tbl : m_pkg.tables) {
            env.tables[tbl.id] = tbl.kind;
        }
        for (const auto& res : m_pkg.resources) {
            env.resources.insert(res.id);
        }
        for (const auto& g : m_pkg.globals) {
            env.globals[g.id] = makeParamCompileInfo(g);
        }
        for (const auto& p : m_pkg.parameters) {
            env.parameters[p.id] = makeParamCompileInfo(p);
        }
        return env;
    }

    bool compileParamStaticExprs(
        ParameterDef& def,
        const RawEntry& raw,
        bool is_global,
        const expr::CompileEnv& base_env,
        std::vector<std::string>& out_referenced_globals,
        std::vector<std::string>& out_referenced_params,
        bool& out_cascade) {
        bool ok = true;

        auto compileField = [&](const char* field,
                                std::string_view expr_src,
                                expr::ExpectedType expected_t,
                                bool allow_opt,
                                bool allow_port,
                                expr::CompiledExpr& out_compiled) {
            expr::CompileEnv env = base_env;
            env.context = is_global ? expr::ExprContext::GlobalStatic : expr::ExprContext::ParamStatic;
            env.check_profile_params = false;
            if (is_global) {
                env.current_global_id = def.id;
            } else {
                env.current_param_id = def.id;
            }
            env.allow_option = allow_opt;
            if (allow_opt) {
                if (def.type == ParameterType::ResourceChoice) {
                    env.option_type = expr::ExprType::resource();
                } else {
                    env.option_type = expr::ExprType::option(def.option_set);
                }
            }
            env.allow_port_in_static = allow_port;

            expr::CompileResult res = expr::compile(expr_src, env, expected_t);
            if (!res.ok) {
                if (isSecondaryCompileError(res.error)) {
                    out_cascade = true;
                    ok = false;
                    return;
                }
                addCompileError(res.error, raw.file, raw.json_pointer + "/" + field);
                ok = false;
                return;
            }
            if (referencesFailedEntity(res.expr)) {
                out_cascade = true;
                ok = false;
                return;
            }
            recordExprUsage(res.expr);
            for (const auto& rg : res.expr.referenced_globals) {
                addUnique(out_referenced_globals, rg);
            }
            for (const auto& rp : res.expr.referenced_params) {
                addUnique(out_referenced_params, rp);
            }
            if (res.expr.references_port) {
                def.uses_port_in_static = true;
                def.is_port_bound = true;
            }
            out_compiled = std::move(res.expr);
        };

        const bool allow_port = (!is_global && def.type == ParameterType::ResourceChoice);

        if (def.min.is_present) {
            compileField("min", def.min.is_literal ? std::to_string(def.min.literal_value) : def.min.expr_src,
                         expr::ExpectedType::Number, false, false, def.min.compiled_expr);
        }
        if (def.max.is_present) {
            compileField("max", def.max.is_literal ? std::to_string(def.max.literal_value) : def.max.expr_src,
                         expr::ExpectedType::Number, false, false, def.max.compiled_expr);
        }
        if (def.count.is_present) {
            compileField("count", def.count.is_literal ? std::to_string(def.count.literal_value) : def.count.expr_src,
                         expr::ExpectedType::Number, false, false, def.count.compiled_expr);
        }
        if (def.visible_if.is_present && !def.visible_if.is_literal) {
            compileField("visible_if", def.visible_if.expr_src,
                         expr::ExpectedType::Bool, false, allow_port, def.visible_if.compiled_expr);
        }
        if (def.allowed_if.is_present && !def.allowed_if.is_literal) {
            compileField("allowed_if", def.allowed_if.expr_src,
                         expr::ExpectedType::Bool, true, allow_port, def.allowed_if.compiled_expr);
        }

        return ok && !out_cascade;
    }

    void runStage4() {
        // 1. Validate global structures first
        std::vector<PendingParam> pending_globals;
        for (const std::string& gid : m_globalOrder) {
            const RawEntry& raw = m_rawGlobals.at(gid);
            ParameterDef def;
            bool cascade = false;
            if (validateParamStructure(gid, raw, true, def, cascade) && !cascade) {
                pending_globals.push_back(PendingParam{gid, raw, std::move(def)});
            } else {
                m_failedGlobals.insert(gid);
                markDropped("globals", gid);
            }
        }

        // 2. Validate parameter structures
        std::vector<PendingParam> pending_params;
        for (const std::string& pid : m_parameterOrder) {
            const RawEntry& raw = m_rawParameters.at(pid);
            ParameterDef def;
            bool cascade = false;
            if (validateParamStructure(pid, raw, false, def, cascade) && !cascade) {
                pending_params.push_back(PendingParam{pid, raw, std::move(def)});
            } else {
                m_failedParameters.insert(pid);
                markDropped("parameters", pid);
            }
        }

        // Build tentative CompileEnv containing all structurally valid globals & parameters
        expr::CompileEnv base_env = buildBaseCompileEnv();
        for (const auto& pg : pending_globals) {
            base_env.globals[pg.id] = makeParamCompileInfo(pg.def);
        }
        for (const auto& pp : pending_params) {
            base_env.parameters[pp.id] = makeParamCompileInfo(pp.def);
        }

        // 3. Compile globals static expressions + cycle & transitive closure check
        std::unordered_map<std::string, std::vector<std::string>> global_deps;
        std::unordered_map<std::string, PendingParam> compiled_globals_map;

        for (auto& pg : pending_globals) {
            std::vector<std::string> ref_globals;
            std::vector<std::string> ref_params;
            bool cascade = false;
            if (compileParamStaticExprs(pg.def, pg.raw, true, base_env, ref_globals, ref_params, cascade)) {
                global_deps[pg.id] = std::move(ref_globals);
                compiled_globals_map.emplace(pg.id, std::move(pg));
            } else {
                m_failedGlobals.insert(pg.id);
                markDropped("globals", pg.id);
            }
        }

        // Transitive closure of globals referencing failed globals
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto it = compiled_globals_map.begin(); it != compiled_globals_map.end();) {
                const std::string& gid = it->first;
                bool refs_failed = false;
                for (const std::string& dep : global_deps[gid]) {
                    if (m_failedGlobals.count(dep)) {
                        refs_failed = true;
                        break;
                    }
                }
                if (refs_failed) {
                    m_failedGlobals.insert(gid);
                    markDropped("globals", gid);
                    global_deps.erase(gid);
                    it = compiled_globals_map.erase(it);
                    changed = true;
                } else {
                    ++it;
                }
            }
        }

        // Check global cycles (V-EXPR-6)
        expr::DependencyOrderResult g_order = expr::resolveDependencyOrder(global_deps);
        if (!g_order.ok) {
            if (!g_order.cycled_nodes.empty()) {
                const std::string& first_cycled = g_order.cycled_nodes.front();
                const auto& raw = compiled_globals_map.at(first_cycled).raw;
                addError("V-EXPR-6", raw.file, raw.json_pointer, g_order.cycle_message);
            }
            for (const std::string& cg : g_order.cycled_nodes) {
                m_failedGlobals.insert(cg);
                markDropped("globals", cg);
                compiled_globals_map.erase(cg);
            }
            for (const std::string& dg : g_order.downstream_of_cycle) {
                m_failedGlobals.insert(dg);
                markDropped("globals", dg);
                compiled_globals_map.erase(dg);
            }
        }
        m_pkg.global_eval_order = std::move(g_order.order);

        for (const std::string& gid : m_globalOrder) {
            auto it = compiled_globals_map.find(gid);
            if (it != compiled_globals_map.end()) {
                m_pkg.global_index[gid] = m_pkg.globals.size();
                m_pkg.globals.push_back(std::move(it->second.def));
            }
        }

        // Update base_env.globals to only surviving globals
        base_env.globals.clear();
        for (const auto& g : m_pkg.globals) {
            base_env.globals[g.id] = makeParamCompileInfo(g);
        }

        // 4. Compile parameters static expressions + transitive closure check (§9.1)
        std::unordered_map<std::string, std::vector<std::string>> param_deps;
        std::unordered_map<std::string, PendingParam> compiled_params_map;

        for (auto& pp : pending_params) {
            std::vector<std::string> ref_globals;
            std::vector<std::string> ref_params;
            bool cascade = false;
            if (compileParamStaticExprs(pp.def, pp.raw, false, base_env, ref_globals, ref_params, cascade)) {
                param_deps[pp.id] = std::move(ref_params);
                compiled_params_map.emplace(pp.id, std::move(pp));
            } else {
                m_failedParameters.insert(pp.id);
                markDropped("parameters", pp.id);
            }
        }

        // Transitive closure of parameters referencing failed parameters (§9.1)
        changed = true;
        while (changed) {
            changed = false;
            for (auto it = compiled_params_map.begin(); it != compiled_params_map.end();) {
                const std::string& pid = it->first;
                bool refs_failed = false;
                for (const std::string& dep : param_deps[pid]) {
                    if (m_failedParameters.count(dep)) {
                        refs_failed = true;
                        break;
                    }
                }
                if (refs_failed) {
                    m_failedParameters.insert(pid);
                    markDropped("parameters", pid);
                    param_deps.erase(pid);
                    it = compiled_params_map.erase(it);
                    changed = true;
                } else {
                    ++it;
                }
            }
        }

        for (const std::string& pid : m_parameterOrder) {
            auto it = compiled_params_map.find(pid);
            if (it != compiled_params_map.end()) {
                m_pkg.parameter_index[pid] = m_pkg.parameters.size();
                m_pkg.parameters.push_back(std::move(it->second.def));
            }
        }
    }

    // ========================================================================
    // Stage 5: Metrics (§6.1, V-MET-1)
    // ========================================================================

    void runStage5() {
        static const std::vector<std::string> kMetricKeys = {
            "aggregate", "display", "format", "is_rate", "label", "notes", "split_sign", "unit"
        };

        for (const std::string& mid : m_metricOrder) {
            const RawEntry& raw = m_rawMetrics.at(mid);
            bool ok = true;
            if (!expr::isValidSlugId(mid)) {
                addError("V-ID-1", raw.file, raw.json_pointer,
                         "invalid metric id '" + mid + "' (must match [a-z0-9_-]+)");
                ok = false;
            }
            if (!raw.value.is_object()) {
                addError("V-STRUCT-2", raw.file, raw.json_pointer,
                         "expected object for metric, got " + jsonTypeName(raw.value));
                m_failedMetrics.insert(mid);
                markDropped("metrics", mid);
                continue;
            }

            const json& mj = raw.value;
            if (!checkAllowedKeys(mj, raw.file, raw.json_pointer, kMetricKeys)) {
                ok = false;
            }

            MetricDef mdef;
            mdef.id = mid;
            mdef.label = mid;
            if (mj.contains("label")) {
                if (!mj["label"].is_string()) {
                    addError("V-STRUCT-2", raw.file, raw.json_pointer + "/label",
                             "expected string for 'label', got " + jsonTypeName(mj["label"]));
                    ok = false;
                } else {
                    mdef.label = mj["label"].get<std::string>();
                }
            }
            if (mj.contains("unit")) {
                if (!mj["unit"].is_string()) {
                    addError("V-STRUCT-2", raw.file, raw.json_pointer + "/unit",
                             "expected string for 'unit', got " + jsonTypeName(mj["unit"]));
                    ok = false;
                } else {
                    mdef.unit = mj["unit"].get<std::string>();
                }
            }
            if (mj.contains("format")) {
                if (!mj["format"].is_string()) {
                    addError("V-STRUCT-2", raw.file, raw.json_pointer + "/format",
                             "expected string for 'format', got " + jsonTypeName(mj["format"]));
                    ok = false;
                } else {
                    mdef.format = mj["format"].get<std::string>();
                }
            }
            if (mj.contains("is_rate")) {
                if (!mj["is_rate"].is_boolean()) {
                    addError("V-STRUCT-2", raw.file, raw.json_pointer + "/is_rate",
                             "expected bool for 'is_rate', got " + jsonTypeName(mj["is_rate"]));
                    ok = false;
                } else {
                    mdef.is_rate = mj["is_rate"].get<bool>();
                }
            }

            if (mj.contains("display")) {
                if (!mj["display"].is_array()) {
                    addError("V-STRUCT-2", raw.file, raw.json_pointer + "/display",
                             "expected array for 'display', got " + jsonTypeName(mj["display"]));
                    ok = false;
                } else {
                    mdef.display.clear();
                    for (size_t di = 0; di < mj["display"].size(); ++di) {
                        const json& de = mj["display"][di];
                        std::string dptr = raw.json_pointer + "/display/" + std::to_string(di);
                        if (!de.is_string()) {
                            addError("V-STRUCT-2", raw.file, dptr,
                                     "expected string in 'display', got " + jsonTypeName(de));
                            ok = false;
                        } else {
                            std::string dstr = de.get<std::string>();
                            if (dstr == "body") {
                                mdef.display.push_back(MetricDisplayLocation::Body);
                            } else if (dstr == "tooltip") {
                                mdef.display.push_back(MetricDisplayLocation::Tooltip);
                            } else if (dstr == "totals") {
                                mdef.display.push_back(MetricDisplayLocation::Totals);
                            } else {
                                addError("V-MET-1", raw.file, dptr,
                                         "invalid metric display location '" + dstr +
                                             "' (expected 'body', 'tooltip', or 'totals')");
                                ok = false;
                            }
                        }
                    }
                }
            }

            if (mj.contains("aggregate")) {
                if (!mj["aggregate"].is_string()) {
                    addError("V-STRUCT-2", raw.file, raw.json_pointer + "/aggregate",
                             "expected string for 'aggregate', got " + jsonTypeName(mj["aggregate"]));
                    ok = false;
                } else {
                    std::string astr = mj["aggregate"].get<std::string>();
                    if (astr == "sum") {
                        mdef.aggregate = MetricAggregate::Sum;
                    } else if (astr == "max") {
                        mdef.aggregate = MetricAggregate::Max;
                    } else if (astr == "min") {
                        mdef.aggregate = MetricAggregate::Min;
                    } else if (astr == "none") {
                        mdef.aggregate = MetricAggregate::None;
                    } else {
                        addError("V-MET-1", raw.file, raw.json_pointer + "/aggregate",
                                 "invalid metric aggregate '" + astr +
                                     "' (expected 'sum', 'max', 'min', or 'none')");
                        ok = false;
                    }
                }
            }

            if (mj.contains("split_sign")) {
                if (!mj["split_sign"].is_boolean()) {
                    addError("V-STRUCT-2", raw.file, raw.json_pointer + "/split_sign",
                             "expected bool for 'split_sign', got " + jsonTypeName(mj["split_sign"]));
                    ok = false;
                } else {
                    mdef.split_sign = mj["split_sign"].get<bool>();
                    if (mdef.split_sign && mdef.aggregate != MetricAggregate::Sum) {
                        addError("V-MET-1", raw.file, raw.json_pointer + "/split_sign",
                                 "requires aggregate 'sum'");
                        ok = false;
                    }
                }
            }

            if (ok) {
                m_pkg.metric_index[mid] = m_pkg.metrics.size();
                m_pkg.metrics.push_back(std::move(mdef));
            } else {
                m_failedMetrics.insert(mid);
                markDropped("metrics", mid);
            }
        }
    }

    // ========================================================================
    // Stage 6: Profiles (§4.1–§4.3)
    // ========================================================================

    bool compileProfileMetricEntry(
        const std::string& metric_id,
        const json& entry_j,
        const std::string& file,
        const std::string& ptr,
        const expr::CompileEnv& base_env,
        ProfileMetricEntry& out_entry,
        bool& out_cascade) {
        if (!entry_j.is_object()) {
            addError("V-STRUCT-2", file, ptr,
                     "expected object for metric entry, got " + jsonTypeName(entry_j));
            return false;
        }
        static const std::vector<std::string> kMetricEntryKeys = {
            "enabled", "notes", "per_craft", "per_machine", "value"
        };
        bool ok = checkAllowedKeys(entry_j, file, ptr, kMetricEntryKeys);

        int form_count = 0;
        if (entry_j.contains("per_machine")) ++form_count;
        if (entry_j.contains("per_craft")) ++form_count;
        if (entry_j.contains("value")) ++form_count;

        if (form_count != 1) {
            addError("V-PROF-3", file, ptr,
                     "give exactly one of per_machine, per_craft, value");
            return false;
        }

        out_entry.metric_id = metric_id;
        out_entry.enabled.is_present = true;
        out_entry.enabled.is_literal = true;
        out_entry.enabled.literal_value = true;

        if (entry_j.contains("enabled")) {
            const json& en_j = entry_j["enabled"];
            std::string en_ptr = ptr + "/enabled";
            if (en_j.is_boolean()) {
                out_entry.enabled.is_literal = true;
                out_entry.enabled.literal_value = en_j.get<bool>();
            } else if (en_j.is_string()) {
                out_entry.enabled.is_literal = false;
                out_entry.enabled.expr_src = en_j.get<std::string>();
                expr::CompileEnv env = base_env;
                env.context = expr::ExprContext::PreSolveAfterCraftTime;
                expr::CompileResult res = expr::compile(
                    out_entry.enabled.expr_src, env, expr::ExpectedType::Bool);
                if (!res.ok) {
                    if (isSecondaryCompileError(res.error)) {
                        out_cascade = true;
                        return false;
                    }
                    addCompileError(res.error, file, en_ptr);
                    ok = false;
                } else if (referencesFailedEntity(res.expr)) {
                    out_cascade = true;
                    return false;
                } else {
                    recordExprUsage(res.expr);
                    out_entry.enabled.compiled_expr = std::move(res.expr);
                }
            } else {
                addError("V-STRUCT-2", file, en_ptr,
                         "expected bool or expression string for 'enabled', got " + jsonTypeName(en_j));
                ok = false;
            }
        }

        const char* form_key = "per_machine";
        expr::ExprContext form_ctx = expr::ExprContext::PreSolveAfterCraftTime;
        if (entry_j.contains("per_machine")) {
            out_entry.form = MetricFormKind::PerMachine;
            form_key = "per_machine";
            form_ctx = expr::ExprContext::PreSolveAfterCraftTime;
        } else if (entry_j.contains("per_craft")) {
            out_entry.form = MetricFormKind::PerCraft;
            form_key = "per_craft";
            form_ctx = expr::ExprContext::PreSolveAfterCraftTime;
        } else {
            out_entry.form = MetricFormKind::Value;
            form_key = "value";
            form_ctx = expr::ExprContext::PostSolveMetricValue;
        }

        const json& form_j = entry_j[form_key];
        std::string form_ptr = ptr + "/" + form_key;
        if (form_j.is_number()) {
            double d = form_j.get<double>();
            if (!std::isfinite(d)) {
                addError("V-NUM-1", file, form_ptr, "metric value must be finite");
                return false;
            }
            out_entry.expr_src = std::to_string(d);
        } else if (form_j.is_string()) {
            out_entry.expr_src = form_j.get<std::string>();
        } else {
            addError("V-STRUCT-2", file, form_ptr,
                     std::string("expected expression string for '") + form_key + "', got " +
                         jsonTypeName(form_j));
            return false;
        }

        expr::CompileEnv env = base_env;
        env.context = form_ctx;
        expr::CompileResult res = expr::compile(
            out_entry.expr_src, env, expr::ExpectedType::Number);
        if (!res.ok) {
            if (isSecondaryCompileError(res.error)) {
                out_cascade = true;
                return false;
            }
            addCompileError(res.error, file, form_ptr);
            ok = false;
        } else if (referencesFailedEntity(res.expr)) {
            out_cascade = true;
            return false;
        } else {
            recordExprUsage(res.expr);
            out_entry.compiled_expr = std::move(res.expr);
        }

        return ok && !out_cascade;
    }

    static Profile makeBuiltinDefaultProfile() {
        Profile p;
        p.id = "default";
        p.craft_time_expr = "recipe.time / machine.speed";
        p.input_amount_expr = "port.amount * port.chance";
        p.output_amount_expr = "port.amount * port.chance";
        p.port_enabled.is_present = true;
        p.port_enabled.is_literal = true;
        p.port_enabled.literal_value = true;
        p.port_enabled.expr_src = "true";
        return p;
    }

    void runStage6() {
        // Ensure built-in "default" profile is in raw profiles if not explicitly defined
        if (m_rawProfiles.find("default") == m_rawProfiles.end()) {
            json def_j = {
                {"params", json::array()},
                {"let", json::object()},
                {"requires", json::array()},
                {"craft_time", "recipe.time / machine.speed"},
                {"input_amount", "port.amount * port.chance"},
                {"output_amount", "port.amount * port.chance"},
                {"port_enabled", "true"},
                {"extra_ports", json::array()},
                {"metrics", json::object()}
            };
            m_profileOrder.insert(m_profileOrder.begin(), "default");
            m_rawProfiles.emplace("default", RawEntry{"<builtin>", "/profiles/default", std::move(def_j)});
        }

        static const std::vector<std::string> kProfileKeys = {
            "craft_time", "extends", "extra_ports", "input_amount", "input_enabled",
            "let", "metrics", "notes", "output_amount", "output_enabled", "params",
            "port_enabled", "requires"
        };

        // 1. Validate profile IDs, object structure, and extends graph (V-PROF-1, V-PROF-2)
        std::unordered_map<std::string, std::string> parent_of;
        std::unordered_map<std::string, std::vector<std::string>> extends_deps;

        for (const std::string& pid : m_profileOrder) {
            const RawEntry& raw = m_rawProfiles.at(pid);
            bool ok = true;
            if (!expr::isValidSlugId(pid)) {
                addError("V-ID-1", raw.file, raw.json_pointer,
                         "invalid profile id '" + pid + "' (must match [a-z0-9_-]+)");
                ok = false;
            }
            if (!raw.value.is_object()) {
                addError("V-STRUCT-2", raw.file, raw.json_pointer,
                         "expected object for profile, got " + jsonTypeName(raw.value));
                m_failedProfiles.insert(pid);
                markDropped("profiles", pid);
                continue;
            }
            if (!checkAllowedKeys(raw.value, raw.file, raw.json_pointer, kProfileKeys)) {
                ok = false;
            }

            std::string ext;
            if (raw.value.contains("extends")) {
                if (!raw.value["extends"].is_string()) {
                    addError("V-STRUCT-2", raw.file, raw.json_pointer + "/extends",
                             "expected string for 'extends', got " + jsonTypeName(raw.value["extends"]));
                    ok = false;
                } else {
                    ext = raw.value["extends"].get<std::string>();
                }
            } else if (pid != "default") {
                ext = "default";
            }

            if (!ok) {
                m_failedProfiles.insert(pid);
                markDropped("profiles", pid);
                continue;
            }

            parent_of[pid] = ext;
            if (ext.empty()) {
                extends_deps[pid] = {};
            } else {
                extends_deps[pid] = {ext};
            }
        }

        // Check for unknown extends target (V-PROF-1)
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto it = extends_deps.begin(); it != extends_deps.end();) {
                const std::string& pid = it->first;
                const auto& deps = it->second;
                if (!deps.empty()) {
                    const std::string& parent_id = deps[0];
                    if (m_failedProfiles.count(parent_id)) {
                        m_failedProfiles.insert(pid);
                        markDropped("profiles", pid);
                        it = extends_deps.erase(it);
                        changed = true;
                        continue;
                    }
                    if (m_rawProfiles.find(parent_id) == m_rawProfiles.end()) {
                        const RawEntry& raw = m_rawProfiles.at(pid);
                        addError("V-PROF-1", raw.file, raw.json_pointer + "/extends",
                                 "unknown profile '" + parent_id + "'");
                        m_failedProfiles.insert(pid);
                        markDropped("profiles", pid);
                        it = extends_deps.erase(it);
                        changed = true;
                        continue;
                    }
                }
                ++it;
            }
        }

        // Check for extends cycles (V-PROF-2)
        expr::DependencyOrderResult prof_order = expr::resolveDependencyOrder(extends_deps);
        if (!prof_order.ok) {
            if (!prof_order.cycled_nodes.empty()) {
                const std::string& first_cycled = prof_order.cycled_nodes.front();
                const RawEntry& raw = m_rawProfiles.at(first_cycled);
                addError("V-PROF-2", raw.file, raw.json_pointer + "/extends",
                         "inheritance " + prof_order.cycle_message);
            }
            for (const std::string& cp : prof_order.cycled_nodes) {
                m_failedProfiles.insert(cp);
                markDropped("profiles", cp);
            }
            for (const std::string& dp : prof_order.downstream_of_cycle) {
                m_failedProfiles.insert(dp);
                markDropped("profiles", dp);
            }
        }

        // 2. Resolve inheritance and compile each profile in topological order
        expr::CompileEnv base_env = buildBaseCompileEnv();
        std::unordered_map<std::string, json> merged_profile_json;
        std::unordered_map<std::string, Profile> valid_profiles_map;

        for (const std::string& pid : prof_order.order) {
            const RawEntry& raw = m_rawProfiles.at(pid);
            const std::string& parent_id = parent_of[pid];
            if (!parent_id.empty() && m_failedProfiles.count(parent_id)) {
                m_failedProfiles.insert(pid);
                markDropped("profiles", pid);
                continue;
            }

            Profile prof = (parent_id.empty())
                ? makeBuiltinDefaultProfile()
                : valid_profiles_map.at(parent_id);
            prof.id = pid;
            prof.extends = parent_id;

            bool cascade = false;
            if (mergeAndCompileProfile(prof, raw, base_env, cascade) && !cascade) {
                valid_profiles_map.emplace(pid, std::move(prof));
            } else {
                m_failedProfiles.insert(pid);
                markDropped("profiles", pid);
            }
        }

        for (const std::string& pid : m_profileOrder) {
            auto it = valid_profiles_map.find(pid);
            if (it != valid_profiles_map.end()) {
                m_pkg.profile_index[pid] = m_pkg.profiles.size();
                m_pkg.profiles.push_back(std::move(it->second));
            }
        }
    }

    bool mergeAndCompileProfile(
        Profile& prof,
        const RawEntry& raw,
        const expr::CompileEnv& base_env,
        bool& out_cascade) {
        const json& pj = raw.value;
        bool ok = true;

        // 1. Merge params (append new ones in UI order, deduplicating existing ones, §4.2)
        if (pj.contains("params")) {
            if (!pj["params"].is_array()) {
                addError("V-STRUCT-2", raw.file, raw.json_pointer + "/params",
                         "expected array for 'params', got " + jsonTypeName(pj["params"]));
                return false;
            }
            for (size_t i = 0; i < pj["params"].size(); ++i) {
                const json& pe = pj["params"][i];
                std::string pptr = raw.json_pointer + "/params/" + std::to_string(i);
                if (!pe.is_string()) {
                    addError("V-STRUCT-2", raw.file, pptr,
                             "expected parameter id string, got " + jsonTypeName(pe));
                    ok = false;
                    continue;
                }
                std::string param_id = pe.get<std::string>();
                if (m_pkg.findParameter(param_id) == nullptr) {
                    if (m_failedParameters.count(param_id)) {
                        out_cascade = true;
                        ok = false;
                    } else {
                        addError("V-EXPR-5", raw.file, pptr,
                                 "unknown parameter '" + param_id + "' in profile '" + prof.id + "'");
                        ok = false;
                    }
                    continue;
                }
                addUnique(prof.params, param_id);
                m_usedParameters.insert(param_id);
            }
        }

        if (!ok || out_cascade) {
            return false;
        }

        // Verify static parameter expression ordering in prof.params (V-EXPR-5, §5.4)
        for (size_t idx = 0; idx < prof.params.size(); ++idx) {
            const std::string& pid = prof.params[idx];
            const ParameterDef* pdef = m_pkg.findParameter(pid);
            if (pdef == nullptr) continue;

            auto checkParamRefs = [&](const expr::CompiledExpr& cexpr) {
                for (const std::string& ref_p : cexpr.referenced_params) {
                    auto it = std::find(prof.params.begin(), prof.params.end(), ref_p);
                    if (it == prof.params.end()) {
                        addError("V-EXPR-5", raw.file, raw.json_pointer + "/params",
                                 "parameter '" + pid + "' references 'param." + ref_p +
                                     "', which is not listed in profile '" + prof.id + "' params");
                        ok = false;
                    } else {
                        size_t ref_idx = static_cast<size_t>(std::distance(prof.params.begin(), it));
                        if (ref_idx >= idx) {
                            addError("V-EXPR-5", raw.file, raw.json_pointer + "/params",
                                     "parameter '" + pid + "' references 'param." + ref_p +
                                         "', which must be listed earlier in profile '" + prof.id + "' params");
                            ok = false;
                        }
                    }
                }
            };

            if (pdef->min.compiled_expr.isValid()) checkParamRefs(pdef->min.compiled_expr);
            if (pdef->max.compiled_expr.isValid()) checkParamRefs(pdef->max.compiled_expr);
            if (pdef->count.compiled_expr.isValid()) checkParamRefs(pdef->count.compiled_expr);
            if (pdef->visible_if.compiled_expr.isValid()) checkParamRefs(pdef->visible_if.compiled_expr);
            if (pdef->allowed_if.compiled_expr.isValid()) checkParamRefs(pdef->allowed_if.compiled_expr);
        }

        if (!ok) {
            return false;
        }

        // 2. Merge and compile `let` (§4.1, §4.2, V-ID-1, V-EXPR-1..6)
        if (pj.contains("let")) {
            const json& let_j = pj["let"];
            std::string let_ptr = raw.json_pointer + "/let";
            if (!let_j.is_object()) {
                addError("V-STRUCT-2", raw.file, let_ptr,
                         "expected object for 'let', got " + jsonTypeName(let_j));
                return false;
            }
            for (auto it = let_j.begin(); it != let_j.end(); ++it) {
                const std::string& lname = it.key();
                if (lname == "notes" && it.value().is_string()) continue;
                std::string lptr = let_ptr + "/" + lname;
                if (!expr::isValidIdentifierId(lname) || expr::isReservedIdentifier(lname)) {
                    addError("V-ID-1", raw.file, lptr,
                             "invalid let variable name '" + lname +
                                 "' (must match [a-z_][a-z0-9_]* and not be a reserved name)");
                    ok = false;
                    continue;
                }
                if (!it.value().is_string()) {
                    addError("V-STRUCT-2", raw.file, lptr,
                             "expected expression string for let '" + lname + "', got " +
                                 jsonTypeName(it.value()));
                    ok = false;
                    continue;
                }
                prof.let[lname] = it.value().get<std::string>();
            }
        }

        if (!ok) {
            return false;
        }

        expr::CompileEnv prof_env = base_env;
        prof_env.profile_id = prof.id;
        prof_env.check_profile_params = true;
        prof_env.profile_params = prof.params;

        expr::LetCompileResult let_res = expr::compileLetMap(prof.let, prof_env);
        if (!let_res.ok) {
            for (const auto& cerr : let_res.errors) {
                if (isSecondaryCompileError(cerr)) {
                    out_cascade = true;
                    return false;
                }
                std::string err_ptr = raw.json_pointer + "/let";
                if (!cerr.offending_symbol.empty() && prof.let.count(cerr.offending_symbol)) {
                    err_ptr += "/" + cerr.offending_symbol;
                }
                addCompileError(cerr, raw.file, err_ptr);
            }
            return false;
        }

        for (const auto& [lname, cexpr] : let_res.compiled_lets) {
            if (referencesFailedEntity(cexpr)) {
                out_cascade = true;
                return false;
            }
            // V-PROF-4: port-bound parameter (uses_port_in_static) cannot be read directly in profile let
            for (const std::string& rp : cexpr.referenced_params) {
                const ParameterDef* pdef = m_pkg.findParameter(rp);
                if (pdef != nullptr && pdef->uses_port_in_static) {
                    addError("V-PROF-4", raw.file, raw.json_pointer + "/let/" + lname,
                             "port-bound parameter '" + rp + "' cannot be used outside a recipe port's resource_param");
                    return false;
                }
            }
            recordExprUsage(cexpr);
        }

        prof.let_evaluation_order = std::move(let_res.evaluation_order);
        prof.compiled_lets = std::move(let_res.compiled_lets);
        prof.let_types = std::move(let_res.let_types);
        prof_env.let_types = prof.let_types;

        auto checkPortBoundParamRefs = [&](const expr::CompiledExpr& cexpr, const std::string& ptr) {
            for (const std::string& rp : cexpr.referenced_params) {
                const ParameterDef* pdef = m_pkg.findParameter(rp);
                if (pdef != nullptr && pdef->uses_port_in_static) {
                    addError("V-PROF-4", raw.file, ptr,
                             "port-bound parameter '" + rp + "' cannot be used outside a recipe port's resource_param");
                    ok = false;
                }
            }
        };

        // 3. Merge and compile `requires` (appended, §4.2)
        if (pj.contains("requires")) {
            const json& req_arr = pj["requires"];
            std::string reqs_ptr = raw.json_pointer + "/requires";
            if (!req_arr.is_array()) {
                addError("V-STRUCT-2", raw.file, reqs_ptr,
                         "expected array for 'requires', got " + jsonTypeName(req_arr));
                ok = false;
            } else {
                static const std::vector<std::string> kRequireKeys = {"expr", "message", "notes"};
                for (size_t i = 0; i < req_arr.size(); ++i) {
                    const json& rj = req_arr[i];
                    std::string rptr = reqs_ptr + "/" + std::to_string(i);
                    if (!rj.is_object()) {
                        addError("V-STRUCT-2", raw.file, rptr,
                                 "expected object for requirement, got " + jsonTypeName(rj));
                        ok = false;
                        continue;
                    }
                    if (!checkAllowedKeys(rj, raw.file, rptr, kRequireKeys)) {
                        ok = false;
                    }
                    if (!rj.contains("expr") || !rj["expr"].is_string()) {
                        addError("V-STRUCT-2", raw.file, rptr + "/expr",
                                 "requirement requires string 'expr'");
                        ok = false;
                        continue;
                    }
                    if (!rj.contains("message") || !rj["message"].is_string()) {
                        addError("V-STRUCT-2", raw.file, rptr + "/message",
                                 "requirement requires string 'message'");
                        ok = false;
                        continue;
                    }
                    RequireRule rule;
                    rule.expr_src = rj["expr"].get<std::string>();
                    rule.message = rj["message"].get<std::string>();

                    expr::CompileEnv env = prof_env;
                    env.context = expr::ExprContext::PreSolveLet;
                    expr::CompileResult res = expr::compile(rule.expr_src, env, expr::ExpectedType::Bool);
                    if (!res.ok) {
                        if (isSecondaryCompileError(res.error)) {
                            out_cascade = true;
                            return false;
                        }
                        addCompileError(res.error, raw.file, rptr + "/expr");
                        ok = false;
                        continue;
                    }
                    if (referencesFailedEntity(res.expr)) {
                        out_cascade = true;
                        return false;
                    }
                    checkPortBoundParamRefs(res.expr, rptr + "/expr");
                    recordExprUsage(res.expr);
                    rule.compiled_expr = std::move(res.expr);
                    prof.requires_rules.push_back(std::move(rule));
                }
            }
        }

        // 4. Compile craft_time, input_amount, output_amount, port_enabled, input_enabled, output_enabled
        auto compileReplacedStringFormula = [&](const char* field,
                                                expr::ExprContext ctx,
                                                expr::ExpectedType expected_t,
                                                std::string& in_out_src,
                                                expr::CompiledExpr& out_compiled) {
            std::string fptr = raw.json_pointer + "/" + field;
            if (pj.contains(field)) {
                if (!pj[field].is_string()) {
                    addError("V-STRUCT-2", raw.file, fptr,
                             std::string("expected expression string for '") + field +
                                 "', got " + jsonTypeName(pj[field]));
                    ok = false;
                    return;
                }
                in_out_src = pj[field].get<std::string>();
            }
            expr::CompileEnv env = prof_env;
            env.context = ctx;
            expr::CompileResult res = expr::compile(in_out_src, env, expected_t);
            if (!res.ok) {
                if (isSecondaryCompileError(res.error)) {
                    out_cascade = true;
                    ok = false;
                    return;
                }
                addCompileError(res.error, raw.file, fptr);
                ok = false;
                return;
            }
            if (referencesFailedEntity(res.expr)) {
                out_cascade = true;
                ok = false;
                return;
            }
            checkPortBoundParamRefs(res.expr, fptr);
            recordExprUsage(res.expr);
            out_compiled = std::move(res.expr);
        };

        compileReplacedStringFormula(
            "craft_time", expr::ExprContext::PreSolveCraftTime,
            expr::ExpectedType::Number, prof.craft_time_expr, prof.compiled_craft_time);
        compileReplacedStringFormula(
            "input_amount", expr::ExprContext::RecipePortFormula,
            expr::ExpectedType::Number, prof.input_amount_expr, prof.compiled_input_amount);
        compileReplacedStringFormula(
            "output_amount", expr::ExprContext::RecipePortFormula,
            expr::ExpectedType::Number, prof.output_amount_expr, prof.compiled_output_amount);

        auto compileReplacedBoolFormula = [&](const char* field, BoolOrExpr& in_out_be) {
            std::string fptr = raw.json_pointer + "/" + field;
            if (pj.contains(field)) {
                const json& fj = pj[field];
                if (fj.is_boolean()) {
                    in_out_be.is_present = true;
                    in_out_be.is_literal = true;
                    in_out_be.literal_value = fj.get<bool>();
                    in_out_be.expr_src = in_out_be.literal_value ? "true" : "false";
                } else if (fj.is_string()) {
                    in_out_be.is_present = true;
                    in_out_be.is_literal = false;
                    in_out_be.expr_src = fj.get<std::string>();
                } else {
                    addError("V-STRUCT-2", raw.file, fptr,
                             std::string("expected bool or expression string for '") + field +
                                 "', got " + jsonTypeName(fj));
                    ok = false;
                    return;
                }
            }
            if (in_out_be.is_present && !in_out_be.is_literal) {
                expr::CompileEnv env = prof_env;
                env.context = expr::ExprContext::RecipePortFormula;
                expr::CompileResult res = expr::compile(in_out_be.expr_src, env, expr::ExpectedType::Bool);
                if (!res.ok) {
                    if (isSecondaryCompileError(res.error)) {
                        out_cascade = true;
                        ok = false;
                        return;
                    }
                    addCompileError(res.error, raw.file, fptr);
                    ok = false;
                    return;
                }
                if (referencesFailedEntity(res.expr)) {
                    out_cascade = true;
                    ok = false;
                    return;
                }
                checkPortBoundParamRefs(res.expr, fptr);
                recordExprUsage(res.expr);
                in_out_be.compiled_expr = std::move(res.expr);
            }
        };

        compileReplacedBoolFormula("port_enabled", prof.port_enabled);
        compileReplacedBoolFormula("input_enabled", prof.input_enabled);
        compileReplacedBoolFormula("output_enabled", prof.output_enabled);

        // 5. Merge and compile extra_ports (§4.2, §4.3, V-PROF-4, V-REF-1)
        if (pj.contains("extra_ports")) {
            const json& ep_arr = pj["extra_ports"];
            std::string eps_ptr = raw.json_pointer + "/extra_ports";
            if (!ep_arr.is_array()) {
                addError("V-STRUCT-2", raw.file, eps_ptr,
                         "expected array for 'extra_ports', got " + jsonTypeName(ep_arr));
                ok = false;
            } else {
                static const std::vector<std::string> kExtraPortKeys = {
                    "amount", "dir", "enabled", "id", "notes", "resource",
                    "resource_expr", "resource_param"
                };
                std::unordered_set<std::string> seen_in_this_child;
                for (size_t i = 0; i < ep_arr.size(); ++i) {
                    const json& epj = ep_arr[i];
                    std::string ep_ptr = eps_ptr + "/" + std::to_string(i);
                    if (!epj.is_object()) {
                        addError("V-STRUCT-2", raw.file, ep_ptr,
                                 "expected object for extra_port, got " + jsonTypeName(epj));
                        ok = false;
                        continue;
                    }
                    if (!checkAllowedKeys(epj, raw.file, ep_ptr, kExtraPortKeys)) {
                        ok = false;
                    }

                    ExtraPortDef ep;
                    if (!epj.contains("id") || !epj["id"].is_string()) {
                        addError("V-ID-1", raw.file, ep_ptr + "/id", "missing required extra_port 'id'");
                        ok = false;
                        continue;
                    }
                    ep.id = epj["id"].get<std::string>();
                    if (!expr::isValidSlugId(ep.id)) {
                        addError("V-ID-1", raw.file, ep_ptr + "/id",
                                 "invalid extra_port id '" + ep.id + "' (must match [a-z0-9_-]+)");
                        ok = false;
                    }
                    if (!seen_in_this_child.insert(ep.id).second) {
                        addError("V-PROF-4", raw.file, ep_ptr + "/id",
                                 "duplicate extra_port id '" + ep.id + "' in profile '" + prof.id + "'");
                        ok = false;
                    }

                    if (!epj.contains("dir") || !epj["dir"].is_string()) {
                        addError("V-PROF-4", raw.file, ep_ptr + "/dir",
                                 "extra_port requires 'dir' ('in' or 'out')");
                        ok = false;
                    } else {
                        ep.dir = epj["dir"].get<std::string>();
                        if (ep.dir != "in" && ep.dir != "out") {
                            addError("V-PROF-4", raw.file, ep_ptr + "/dir",
                                     "extra_port 'dir' must be 'in' or 'out', got '" + ep.dir + "'");
                            ok = false;
                        }
                    }
                    ep.slot_key = (ep.dir == "out" ? "xout:" : "xin:") + ep.id;

                    int res_spec_count = 0;
                    if (epj.contains("resource")) ++res_spec_count;
                    if (epj.contains("resource_param")) ++res_spec_count;
                    if (epj.contains("resource_expr")) ++res_spec_count;
                    if (res_spec_count != 1) {
                        addError("V-PROF-4", raw.file, ep_ptr,
                                 "extra_port must specify exactly one of 'resource', 'resource_param', or 'resource_expr'");
                        ok = false;
                    } else if (epj.contains("resource")) {
                        if (!epj["resource"].is_string()) {
                            addError("V-STRUCT-2", raw.file, ep_ptr + "/resource",
                                     "expected string for 'resource', got " + jsonTypeName(epj["resource"]));
                            ok = false;
                        } else {
                            std::string rid = epj["resource"].get<std::string>();
                            if (m_pkg.findResource(rid) == nullptr) {
                                if (m_failedResources.count(rid)) {
                                    out_cascade = true;
                                    return false;
                                }
                                addError("V-REF-1", raw.file, ep_ptr + "/resource",
                                         "unknown resource '" + rid + "'");
                                ok = false;
                            } else {
                                ep.resource = rid;
                                m_usedResources.insert(rid);
                            }
                        }
                    } else if (epj.contains("resource_param")) {
                        if (!epj["resource_param"].is_string()) {
                            addError("V-STRUCT-2", raw.file, ep_ptr + "/resource_param",
                                     "expected string for 'resource_param', got " +
                                         jsonTypeName(epj["resource_param"]));
                            ok = false;
                        } else {
                            std::string rparam = epj["resource_param"].get<std::string>();
                            const ParameterDef* pdef = m_pkg.findParameter(rparam);
                            bool in_prof = std::find(prof.params.begin(), prof.params.end(), rparam) !=
                                           prof.params.end();
                            if (pdef == nullptr) {
                                if (m_failedParameters.count(rparam)) {
                                    out_cascade = true;
                                    return false;
                                }
                                addError("V-PROF-4", raw.file, ep_ptr + "/resource_param",
                                         "'" + rparam + "' is not a parameter of profile '" + prof.id + "'");
                                ok = false;
                            } else if (!in_prof || pdef->type != ParameterType::ResourceChoice) {
                                addError("V-PROF-4", raw.file, ep_ptr + "/resource_param",
                                         "'" + rparam + "' is not a resource_choice parameter of profile '" +
                                             prof.id + "'");
                                ok = false;
                            } else if (pdef->uses_port_in_static) {
                                addError("V-PROF-4", raw.file, ep_ptr + "/resource_param",
                                         "port-bound parameter '" + rparam +
                                             "' cannot be used on an extra_port");
                                ok = false;
                            } else {
                                ep.resource_param = rparam;
                                m_usedParameters.insert(rparam);
                            }
                        }
                    } else if (epj.contains("resource_expr")) {
                        if (!epj["resource_expr"].is_string()) {
                            addError("V-STRUCT-2", raw.file, ep_ptr + "/resource_expr",
                                     "expected expression string for 'resource_expr', got " +
                                         jsonTypeName(epj["resource_expr"]));
                            ok = false;
                        } else {
                            ep.resource_expr = epj["resource_expr"].get<std::string>();
                            expr::CompileEnv env = prof_env;
                            env.context = expr::ExprContext::PreSolveAfterCraftTime;
                            expr::CompileResult res = expr::compile(
                                *ep.resource_expr, env, expr::ExpectedType::ResourceOrString);
                            if (!res.ok) {
                                if (isSecondaryCompileError(res.error)) {
                                    out_cascade = true;
                                    return false;
                                }
                                addCompileError(res.error, raw.file, ep_ptr + "/resource_expr");
                                ok = false;
                            } else if (referencesFailedEntity(res.expr)) {
                                out_cascade = true;
                                return false;
                            } else {
                                checkPortBoundParamRefs(res.expr, ep_ptr + "/resource_expr");
                                recordExprUsage(res.expr, true);
                                ep.compiled_resource_expr = std::move(res.expr);
                            }
                        }
                    }

                    if (!epj.contains("amount")) {
                        addError("V-STRUCT-2", raw.file, ep_ptr + "/amount",
                                 "extra_port requires 'amount' expression");
                        ok = false;
                    } else if (epj["amount"].is_number()) {
                        ep.amount_expr = std::to_string(epj["amount"].get<double>());
                    } else if (epj["amount"].is_string()) {
                        ep.amount_expr = epj["amount"].get<std::string>();
                    } else {
                        addError("V-STRUCT-2", raw.file, ep_ptr + "/amount",
                                 "expected expression string for 'amount', got " +
                                     jsonTypeName(epj["amount"]));
                        ok = false;
                    }

                    if (!ep.amount_expr.empty()) {
                        expr::CompileEnv env = prof_env;
                        env.context = expr::ExprContext::PreSolveAfterCraftTime;
                        expr::CompileResult res = expr::compile(
                            ep.amount_expr, env, expr::ExpectedType::Number);
                        if (!res.ok) {
                            if (isSecondaryCompileError(res.error)) {
                                out_cascade = true;
                                return false;
                            }
                            addCompileError(res.error, raw.file, ep_ptr + "/amount");
                            ok = false;
                        } else if (referencesFailedEntity(res.expr)) {
                            out_cascade = true;
                            return false;
                        } else {
                            checkPortBoundParamRefs(res.expr, ep_ptr + "/amount");
                            recordExprUsage(res.expr);
                            ep.compiled_amount = std::move(res.expr);
                        }
                    }

                    ep.enabled.is_present = true;
                    ep.enabled.is_literal = true;
                    ep.enabled.literal_value = true;
                    if (epj.contains("enabled")) {
                        const json& en_j = epj["enabled"];
                        if (en_j.is_boolean()) {
                            ep.enabled.is_literal = true;
                            ep.enabled.literal_value = en_j.get<bool>();
                        } else if (en_j.is_string()) {
                            ep.enabled.is_literal = false;
                            ep.enabled.expr_src = en_j.get<std::string>();
                            expr::CompileEnv env = prof_env;
                            env.context = expr::ExprContext::PreSolveAfterCraftTime;
                            expr::CompileResult res = expr::compile(
                                ep.enabled.expr_src, env, expr::ExpectedType::Bool);
                            if (!res.ok) {
                                if (isSecondaryCompileError(res.error)) {
                                    out_cascade = true;
                                    return false;
                                }
                                addCompileError(res.error, raw.file, ep_ptr + "/enabled");
                                ok = false;
                            } else if (referencesFailedEntity(res.expr)) {
                                out_cascade = true;
                                return false;
                            } else {
                                checkPortBoundParamRefs(res.expr, ep_ptr + "/enabled");
                                recordExprUsage(res.expr);
                                ep.enabled.compiled_expr = std::move(res.expr);
                            }
                        } else {
                            addError("V-STRUCT-2", raw.file, ep_ptr + "/enabled",
                                     "expected bool or expression string for 'enabled', got " +
                                         jsonTypeName(en_j));
                            ok = false;
                        }
                    }

                    // Merge into prof.extra_ports: replace at parent's position if id exists, else append (§4.2)
                    auto existing_it = std::find_if(
                        prof.extra_ports.begin(), prof.extra_ports.end(),
                        [&](const ExtraPortDef& existing) { return existing.id == ep.id; });
                    if (existing_it != prof.extra_ports.end()) {
                        *existing_it = std::move(ep);
                    } else {
                        prof.extra_ports.push_back(std::move(ep));
                    }
                }
            }
        }

        // 6. Merge and compile metrics (§4.2, §6.2, V-PROF-3)
        if (pj.contains("metrics")) {
            const json& met_j = pj["metrics"];
            std::string mets_ptr = raw.json_pointer + "/metrics";
            if (!met_j.is_object()) {
                addError("V-STRUCT-2", raw.file, mets_ptr,
                         "expected object for 'metrics', got " + jsonTypeName(met_j));
                ok = false;
            } else {
                for (auto it = met_j.begin(); it != met_j.end(); ++it) {
                    const std::string& mid = it.key();
                    if (mid == "notes" && it.value().is_string()) continue;
                    std::string mptr = mets_ptr + "/" + mid;

                    // §9.1: When a Metric failed validation (V-MET-1), remove only that metric entry
                    // from profile.metrics without dropping the profile!
                    if (m_failedMetrics.count(mid)) {
                        prof.metrics.erase(mid);
                        prof.metric_order.erase(
                            std::remove(prof.metric_order.begin(), prof.metric_order.end(), mid),
                            prof.metric_order.end());
                        continue;
                    }

                    if (m_pkg.findMetric(mid) == nullptr) {
                        addError("V-PROF-3", raw.file, mptr,
                                 "unknown metric '" + mid + "' in profile '" + prof.id + "'");
                        ok = false;
                        continue;
                    }

                    m_usedMetrics.insert(mid);
                    ProfileMetricEntry mentry;
                    if (!compileProfileMetricEntry(
                            mid, it.value(), raw.file, mptr, prof_env, mentry, out_cascade)) {
                        if (out_cascade) return false;
                        ok = false;
                        continue;
                    }
                    checkPortBoundParamRefs(mentry.compiled_expr, mptr);
                    if (mentry.enabled.compiled_expr.isValid()) {
                        checkPortBoundParamRefs(mentry.enabled.compiled_expr, mptr + "/enabled");
                    }
                    addUnique(prof.metric_order, mid);
                    prof.metrics[mid] = std::move(mentry);
                }
            }
        }

        return ok && !out_cascade;
    }

    // ========================================================================
    // Stage 7: Machines (§2.7)
    // ========================================================================

    void runStage7() {
        static const std::vector<std::string> kMachineKeys = {
            "attrs", "categories", "id", "name", "notes", "profile", "speed"
        };
        std::unordered_map<std::string, std::pair<std::string, std::string>> first_seen_mach;

        for (const RawArrayItem& item : m_rawMachines) {
            if (!item.value.is_object()) {
                addError("V-STRUCT-2", item.file, item.json_pointer,
                         "expected object for machine, got " + jsonTypeName(item.value));
                continue;
            }
            const json& mj = item.value;
            bool ok = checkAllowedKeys(mj, item.file, item.json_pointer, kMachineKeys);
            bool cascade = false;

            std::string id;
            if (!mj.contains("id")) {
                addError("V-ID-1", item.file, item.json_pointer + "/id", "missing required machine 'id'");
                ok = false;
            } else if (!mj["id"].is_string()) {
                addError("V-STRUCT-2", item.file, item.json_pointer + "/id",
                         "expected string for machine 'id', got " + jsonTypeName(mj["id"]));
                ok = false;
            } else {
                id = mj["id"].get<std::string>();
                if (!expr::isValidSlugId(id)) {
                    addError("V-ID-1", item.file, item.json_pointer + "/id",
                             "invalid machine id '" + id + "' (must match [a-z0-9_-]+)");
                    ok = false;
                }
            }

            if (!id.empty()) {
                auto seen_it = first_seen_mach.find(id);
                if (seen_it != first_seen_mach.end()) {
                    addError("V-ID-2", item.file, item.json_pointer + "/id",
                             "duplicate machine id '" + id + "' (first defined at " +
                                 seen_it->second.first + ": " + seen_it->second.second + ")");
                    markDropped("machines", id);
                    continue;
                }
                first_seen_mach[id] = {item.file, item.json_pointer};
            }

            Machine mach;
            mach.id = id;
            mach.name = id;

            if (mj.contains("name")) {
                if (!mj["name"].is_string()) {
                    addError("V-STRUCT-2", item.file, item.json_pointer + "/name",
                             "expected string for 'name', got " + jsonTypeName(mj["name"]));
                    ok = false;
                } else {
                    mach.name = mj["name"].get<std::string>();
                }
            }

            if (mj.contains("profile")) {
                if (!mj["profile"].is_string()) {
                    addError("V-STRUCT-2", item.file, item.json_pointer + "/profile",
                             "expected string for 'profile', got " + jsonTypeName(mj["profile"]));
                    ok = false;
                } else {
                    mach.profile = mj["profile"].get<std::string>();
                }
            }

            if (m_pkg.findProfile(mach.profile) == nullptr) {
                if (m_failedProfiles.count(mach.profile)) {
                    cascade = true;
                    ok = false;
                } else {
                    std::vector<std::string> prof_cands;
                    for (const auto& p : m_pkg.profiles) prof_cands.push_back(p.id);
                    std::string msg = "unknown profile '" + mach.profile + "'";
                    auto sug = expr::findBestSuggestion(mach.profile, prof_cands, &m_suggestionBudget);
                    if (sug.has_value()) msg += " (did you mean '" + *sug + "'?)";
                    addError("V-REF-4", item.file, item.json_pointer + "/profile", std::move(msg));
                    ok = false;
                }
            }

            if (mj.contains("categories")) {
                if (!mj["categories"].is_array()) {
                    addError("V-STRUCT-2", item.file, item.json_pointer + "/categories",
                             "expected array for 'categories', got " + jsonTypeName(mj["categories"]));
                    ok = false;
                } else {
                    for (size_t ci = 0; ci < mj["categories"].size(); ++ci) {
                        const json& ce = mj["categories"][ci];
                        std::string cptr = item.json_pointer + "/categories/" + std::to_string(ci);
                        if (!ce.is_string()) {
                            addError("V-STRUCT-2", item.file, cptr,
                                     "expected string category, got " + jsonTypeName(ce));
                            ok = false;
                        } else {
                            std::string cat = ce.get<std::string>();
                            if (!expr::isValidSlugId(cat)) {
                                addError("V-ID-1", item.file, cptr,
                                         "invalid category '" + cat + "' (must match [a-z0-9_-]+)");
                                ok = false;
                            } else {
                                addUnique(mach.categories, cat);
                            }
                        }
                    }
                }
            }

            if (mj.contains("speed")) {
                if (!mj["speed"].is_number()) {
                    addError("V-STRUCT-2", item.file, item.json_pointer + "/speed",
                             "expected number, got " + jsonTypeName(mj["speed"]));
                    ok = false;
                } else {
                    double sp = mj["speed"].get<double>();
                    if (!std::isfinite(sp) || sp <= 0.0) {
                        addError("V-NUM-1", item.file, item.json_pointer + "/speed",
                                 "machine speed must be > 0, got " + std::to_string(sp));
                        ok = false;
                    } else {
                        mach.speed = sp;
                    }
                }
            }

            if (mj.contains("attrs")) {
                if (!validateEntityAttrs(
                        "machine", "/attributes/machine",
                        m_pkg.attributes["machine"], m_failedAttrs["machine"],
                        mj["attrs"], item.file, item.json_pointer + "/attrs",
                        mach.attrs, cascade)) {
                    ok = false;
                }
            }

            if (ok && !cascade && !mach.id.empty()) {
                for (const std::string& cat : mach.categories) {
                    addUnique(m_pkg.category_to_machines[cat], mach.id);
                    addUnique(m_pkg.category_to_profiles[cat], mach.profile);
                }
                m_pkg.machine_index[mach.id] = m_pkg.machines.size();
                m_pkg.machines.push_back(std::move(mach));
            } else {
                for (const std::string& cat : mach.categories) {
                    m_categoriesOfFailedMachines.insert(cat);
                }
                if (!id.empty()) {
                    m_failedMachines.insert(id);
                    markDropped("machines", id);
                }
            }
        }
    }

    // ========================================================================
    // Stage 8: Recipes (§2.8, §4.4)
    // ========================================================================

    expr::CompileResult compileCachedWithProfile(
        const Profile& prof,
        const expr::CompileEnv& base_env,
        expr::ExprContext context,
        expr::ExpectedType expected_type,
        bool port_has_base_resource,
        const std::string& source,
        bool allow_cache) {
        ExpressionCacheKey key{prof.id, context, expected_type, port_has_base_resource, source};
        if (allow_cache) {
            auto it = m_exprCache.find(key);
            if (it != m_exprCache.end()) {
                return it->second;
            }
        }

        expr::CompileEnv env = base_env;
        env.context = context;
        env.profile_id = prof.id;
        env.check_profile_params = true;
        env.profile_params = prof.params;
        env.let_types = prof.let_types;
        env.port_has_base_resource = port_has_base_resource;

        expr::CompileResult res = expr::compile(source, env, expected_type);
        if (allow_cache) {
            m_exprCache.emplace(std::move(key), res);
        }
        return res;
    }

    bool parseRecipePortList(
        const json& ports_j,
        const std::string& dir,
        const std::string& file,
        const std::string& base_ptr,
        std::vector<RecipePort>& out_ports,
        std::unordered_set<std::string>& seen_resource_params_on_recipe,
        bool& out_cascade) {
        if (!ports_j.is_array()) {
            addError("V-STRUCT-2", file, base_ptr,
                     "expected array for '" + dir + "', got " + jsonTypeName(ports_j));
            return false;
        }

        static const std::vector<std::string> kLongPortKeys = {
            "amount", "amount_expr", "attrs", "catalyst", "chance",
            "enabled", "notes", "res", "resource_expr", "resource_param"
        };

        bool ok = true;
        std::vector<std::string> res_candidates;
        auto ensureResCandidates = [&]() -> const std::vector<std::string>& {
            if (res_candidates.empty() && !m_pkg.resources.empty()) {
                res_candidates.reserve(m_pkg.resources.size());
                for (const auto& r : m_pkg.resources) res_candidates.push_back(r.id);
                std::sort(res_candidates.begin(), res_candidates.end());
            }
            return res_candidates;
        };

        for (size_t i = 0; i < ports_j.size(); ++i) {
            const json& pj = ports_j[i];
            std::string pptr = base_ptr + "/" + std::to_string(i);
            RecipePort port;
            port.dir = dir;
            port.index = i;
            port.slot_key = dir + ":" + std::to_string(i);

            if (pj.is_array()) {
                // Short tuple form: [resource, amount] or [resource, amount, chance]
                if (pj.size() != 2 && pj.size() != 3) {
                    addError("V-STRUCT-2", file, pptr,
                             "port tuple must have 2 or 3 elements [resource, amount[, chance]]");
                    ok = false;
                    continue;
                }
                if (!pj[0].is_string()) {
                    addError("V-STRUCT-2", file, pptr + "/0",
                             "expected resource id string, got " + jsonTypeName(pj[0]));
                    ok = false;
                    continue;
                }
                std::string rid = pj[0].get<std::string>();
                if (m_pkg.findResource(rid) == nullptr) {
                    if (m_failedResources.count(rid)) {
                        out_cascade = true;
                        ok = false;
                        continue;
                    }
                    std::string msg = "unknown resource '" + rid + "'";
                    auto sug = expr::findBestSuggestion(rid, ensureResCandidates(), &m_suggestionBudget);
                    if (sug.has_value()) msg += " (did you mean '" + *sug + "'?)";
                    addError("V-REF-1", file, pptr, std::move(msg));
                    ok = false;
                } else {
                    port.res = rid;
                    m_usedResources.insert(rid);
                }

                if (!pj[1].is_number()) {
                    addError("V-STRUCT-2", file, pptr + "/1",
                             "expected number for port amount, got " + jsonTypeName(pj[1]));
                    ok = false;
                } else {
                    double amt = pj[1].get<double>();
                    if (!std::isfinite(amt) || amt <= 0.0) {
                        addError("V-NUM-2", file, pptr,
                                 "amount must be > 0 (use 'setup' for items that are not consumed)");
                        ok = false;
                    } else {
                        port.amount = amt;
                    }
                }

                if (pj.size() == 3) {
                    if (!pj[2].is_number()) {
                        addError("V-STRUCT-2", file, pptr + "/2",
                                 "expected number for port chance, got " + jsonTypeName(pj[2]));
                        ok = false;
                    } else {
                        double ch = pj[2].get<double>();
                        if (!std::isfinite(ch) || ch <= 0.0 || ch > 1.0) {
                            addError("V-NUM-3", file, pptr,
                                     "chance " + std::to_string(ch) + " outside (0, 1]");
                            ok = false;
                        } else {
                            port.chance = ch;
                        }
                    }
                }
            } else if (pj.is_object()) {
                // Long form
                if (!checkAllowedKeys(pj, file, pptr, kLongPortKeys)) {
                    ok = false;
                }

                const bool has_res = pj.contains("res");
                const bool has_rparam = pj.contains("resource_param");
                const bool has_rexpr = pj.contains("resource_expr");

                if (!has_res && !has_rparam && !has_rexpr) {
                    addError("V-PROF-4", file, pptr,
                             "recipe port must specify at least one of 'res', 'resource_param', or 'resource_expr'");
                    ok = false;
                }
                if (has_rparam && has_rexpr) {
                    addError("V-PROF-4", file, pptr,
                             "'resource_param' and 'resource_expr' are mutually exclusive on a port");
                    ok = false;
                }

                if (has_res) {
                    if (!pj["res"].is_string()) {
                        addError("V-STRUCT-2", file, pptr + "/res",
                                 "expected string for 'res', got " + jsonTypeName(pj["res"]));
                        ok = false;
                    } else {
                        std::string rid = pj["res"].get<std::string>();
                        if (m_pkg.findResource(rid) == nullptr) {
                            if (m_failedResources.count(rid)) {
                                out_cascade = true;
                                ok = false;
                            } else {
                                std::string msg = "unknown resource '" + rid + "'";
                                auto sug = expr::findBestSuggestion(
                                    rid, ensureResCandidates(), &m_suggestionBudget);
                                if (sug.has_value()) msg += " (did you mean '" + *sug + "'?)";
                                addError("V-REF-1", file, pptr + "/res", std::move(msg));
                                ok = false;
                            }
                        } else {
                            port.res = rid;
                            m_usedResources.insert(rid);
                        }
                    }
                }

                if (has_rparam) {
                    if (!pj["resource_param"].is_string()) {
                        addError("V-STRUCT-2", file, pptr + "/resource_param",
                                 "expected string for 'resource_param', got " +
                                     jsonTypeName(pj["resource_param"]));
                        ok = false;
                    } else {
                        std::string rp = pj["resource_param"].get<std::string>();
                        if (!seen_resource_params_on_recipe.insert(rp).second) {
                            addError("V-PROF-4", file, pptr + "/resource_param",
                                     "more than one recipe port references resource_param '" + rp + "'");
                            ok = false;
                        }
                        port.resource_param = rp;
                        m_usedParameters.insert(rp);
                    }
                }

                if (has_rexpr) {
                    if (!pj["resource_expr"].is_string()) {
                        addError("V-STRUCT-2", file, pptr + "/resource_expr",
                                 "expected string for 'resource_expr', got " +
                                     jsonTypeName(pj["resource_expr"]));
                        ok = false;
                    } else {
                        port.resource_expr = pj["resource_expr"].get<std::string>();
                    }
                }

                if (!pj.contains("amount") || !pj["amount"].is_number()) {
                    addError("V-STRUCT-2", file, pptr + "/amount",
                             "recipe port requires numeric 'amount'");
                    ok = false;
                } else {
                    double amt = pj["amount"].get<double>();
                    if (!std::isfinite(amt) || amt <= 0.0) {
                        addError("V-NUM-2", file, pptr + "/amount",
                                 "amount must be > 0 (use 'setup' for items that are not consumed)");
                        ok = false;
                    } else {
                        port.amount = amt;
                    }
                }

                if (pj.contains("chance")) {
                    if (!pj["chance"].is_number()) {
                        addError("V-STRUCT-2", file, pptr + "/chance",
                                 "expected number for 'chance', got " + jsonTypeName(pj["chance"]));
                        ok = false;
                    } else {
                        double ch = pj["chance"].get<double>();
                        if (!std::isfinite(ch) || ch <= 0.0 || ch > 1.0) {
                            addError("V-NUM-3", file, pptr + "/chance",
                                     "chance " + std::to_string(ch) + " outside (0, 1]");
                            ok = false;
                        } else {
                            port.chance = ch;
                        }
                    }
                }

                if (pj.contains("catalyst")) {
                    if (!pj["catalyst"].is_number()) {
                        addError("V-STRUCT-2", file, pptr + "/catalyst",
                                 "expected number for 'catalyst', got " + jsonTypeName(pj["catalyst"]));
                        ok = false;
                    } else {
                        double cat = pj["catalyst"].get<double>();
                        if (!std::isfinite(cat) || cat < 0.0 || cat > port.amount) {
                            addError("V-NUM-3", file, pptr + "/catalyst",
                                     "catalyst " + std::to_string(cat) + " outside [0, amount]");
                            ok = false;
                        } else {
                            port.catalyst = cat;
                        }
                    }
                }

                if (pj.contains("attrs")) {
                    if (!validateEntityAttrs(
                            "port", "/attributes/port",
                            m_pkg.attributes["port"], m_failedAttrs["port"],
                            pj["attrs"], file, pptr + "/attrs",
                            port.attrs, out_cascade)) {
                        ok = false;
                    }
                }

                if (pj.contains("enabled")) {
                    const json& en_j = pj["enabled"];
                    if (en_j.is_boolean()) {
                        port.enabled_override.is_present = true;
                        port.enabled_override.is_literal = true;
                        port.enabled_override.literal_value = en_j.get<bool>();
                    } else if (en_j.is_string()) {
                        port.enabled_override.is_present = true;
                        port.enabled_override.is_literal = false;
                        port.enabled_override.expr_src = en_j.get<std::string>();
                    } else {
                        addError("V-STRUCT-2", file, pptr + "/enabled",
                                 "expected bool or expression string for 'enabled', got " +
                                     jsonTypeName(en_j));
                        ok = false;
                    }
                }

                if (pj.contains("amount_expr")) {
                    if (!pj["amount_expr"].is_string()) {
                        addError("V-STRUCT-2", file, pptr + "/amount_expr",
                                 "expected expression string for 'amount_expr', got " +
                                     jsonTypeName(pj["amount_expr"]));
                        ok = false;
                    } else {
                        port.amount_expr_override = pj["amount_expr"].get<std::string>();
                    }
                }
            } else {
                addError("V-STRUCT-2", file, pptr,
                         "expected tuple array or object for recipe port, got " + jsonTypeName(pj));
                ok = false;
                continue;
            }

            out_ports.push_back(std::move(port));
        }

        // V-W-3: Same static resource twice in one recipe's in (or out) with identical
        // chance, catalyst, attrs, enabled, and amount_expr (§9.2 & R2-13)
        for (size_t i = 0; i < out_ports.size(); ++i) {
            const RecipePort& p1 = out_ports[i];
            if (!p1.res.has_value() || p1.resource_param.has_value() || p1.resource_expr.has_value()) {
                continue;
            }
            for (size_t j = 0; j < i; ++j) {
                const RecipePort& p0 = out_ports[j];
                if (!p0.res.has_value() || p0.resource_param.has_value() || p0.resource_expr.has_value()) {
                    continue;
                }
                if (*p0.res == *p1.res &&
                    p0.chance == p1.chance &&
                    p0.catalyst == p1.catalyst &&
                    p0.attrs.size() == p1.attrs.size() &&
                    p0.enabled_override.is_present == p1.enabled_override.is_present &&
                    p0.enabled_override.is_literal == p1.enabled_override.is_literal &&
                    p0.enabled_override.literal_value == p1.enabled_override.literal_value &&
                    p0.enabled_override.expr_src == p1.enabled_override.expr_src &&
                    p0.amount_expr_override == p1.amount_expr_override) {
                    bool same_attrs = true;
                    for (const auto& [ak, av] : p0.attrs) {
                        auto it = p1.attrs.find(ak);
                        if (it == p1.attrs.end() || it->second.data != av.data) {
                            same_attrs = false;
                            break;
                        }
                    }
                    if (same_attrs) {
                        addWarning("V-W-3", file, base_ptr,
                                   "resource '" + *p1.res + "' listed twice");
                        break;
                    }
                }
            }
        }

        return ok && !out_cascade;
    }

    bool validateAndCompileRecipeForProfile(
        Recipe& rec,
        const Profile& prof,
        const std::string& rep_machine_id,
        const std::string& file,
        const std::string& rec_ptr,
        const expr::CompileEnv& base_env,
        CompiledRecipeProfile& out_compiled,
        bool& out_cascade) {
        bool ok = true;
        const std::string suffix = " in profile '" + prof.id + "' (machine '" + rep_machine_id + "')";

        // Check recipe port resource_param against prof.params (V-PROF-4)
        auto checkPortResourceParam = [&](const std::vector<RecipePort>& ports, const char* dir_key) {
            for (size_t i = 0; i < ports.size(); ++i) {
                if (!ports[i].resource_param.has_value()) continue;
                const std::string& rp = *ports[i].resource_param;
                std::string pptr = rec_ptr + "/" + dir_key + "/" + std::to_string(i) + "/resource_param";
                const ParameterDef* pdef = m_pkg.findParameter(rp);
                if (pdef == nullptr) {
                    if (m_failedParameters.count(rp)) {
                        out_cascade = true;
                        ok = false;
                        continue;
                    }
                    addError("V-PROF-4", file, pptr,
                             "'" + rp + "' is not a defined resource_choice parameter" + suffix);
                    ok = false;
                    continue;
                }
                bool in_prof = std::find(prof.params.begin(), prof.params.end(), rp) != prof.params.end();
                if (!in_prof || pdef->type != ParameterType::ResourceChoice) {
                    addError("V-PROF-4", file, pptr,
                             "'" + rp + "' is not a resource_choice parameter" + suffix);
                    ok = false;
                }
            }
        };

        checkPortResourceParam(rec.in, "in");
        checkPortResourceParam(rec.out, "out");
        if (!ok || out_cascade) return false;

        expr::CompileEnv prof_env = base_env;
        prof_env.profile_id = prof.id;
        prof_env.check_profile_params = true;
        prof_env.profile_params = prof.params;
        prof_env.let_types = prof.let_types;

        // 1. Overrides: let (V-OVR-1, V-OVR-2)
        const bool has_let_overrides = !rec.overrides.let.empty();
        if (has_let_overrides) {
            out_compiled.has_let_overrides = true;
            expr::LetCompileResult let_res = expr::compileLetMap(
                prof.let, prof_env, rec.overrides.let, &prof.let_types);
            if (!let_res.ok) {
                for (const auto& cerr : let_res.errors) {
                    if (isSecondaryCompileError(cerr)) {
                        out_cascade = true;
                        return false;
                    }
                    std::string err_ptr = rec_ptr + "/overrides/let";
                    if (!cerr.offending_symbol.empty() && rec.overrides.let.count(cerr.offending_symbol)) {
                        err_ptr += "/" + cerr.offending_symbol;
                    }
                    std::string rule_id = (cerr.rule_id == "V-OVR-1") ? "V-OVR-1" : "V-OVR-2";
                    addCompileError(cerr, file, err_ptr, rule_id, suffix);
                }
                return false;
            }
            for (const auto& [_, cexpr] : let_res.compiled_lets) {
                if (referencesFailedEntity(cexpr)) {
                    out_cascade = true;
                    return false;
                }
                recordExprUsage(cexpr);
            }
            out_compiled.let_evaluation_order = std::move(let_res.evaluation_order);
            out_compiled.compiled_lets = std::move(let_res.compiled_lets);
            prof_env.let_types = std::move(let_res.let_types);
        }

        const bool allow_cache = !has_let_overrides;

        auto compileOverrideFormula = [&](const std::string& expr_src,
                                          const std::string& ptr,
                                          expr::ExprContext ctx,
                                          expr::ExpectedType expected_t,
                                          bool port_has_base_res,
                                          bool is_res_expr,
                                          expr::CompiledExpr& out_cexpr) -> bool {
            expr::CompileResult res;
            if (allow_cache) {
                res = compileCachedWithProfile(
                    prof, base_env, ctx, expected_t, port_has_base_res, expr_src, true);
            } else {
                expr::CompileEnv env = prof_env;
                env.context = ctx;
                env.port_has_base_resource = port_has_base_res;
                res = expr::compile(expr_src, env, expected_t);
            }
            if (!res.ok) {
                if (isSecondaryCompileError(res.error)) {
                    out_cascade = true;
                    return false;
                }
                std::string rule_id = (res.error.rule_id == "V-REF-1") ? "V-REF-1" : "V-OVR-2";
                addCompileError(res.error, file, ptr, rule_id, suffix);
                return false;
            }
            if (referencesFailedEntity(res.expr)) {
                out_cascade = true;
                return false;
            }
            for (const std::string& rp : res.expr.referenced_params) {
                const ParameterDef* pdef = m_pkg.findParameter(rp);
                if (pdef != nullptr && pdef->uses_port_in_static) {
                    addError("V-PROF-4", file, ptr,
                             "port-bound parameter '" + rp +
                                 "' cannot be used outside a recipe port's resource_param");
                    return false;
                }
            }
            recordExprUsage(res.expr, is_res_expr);
            out_cexpr = std::move(res.expr);
            return true;
        };

        // 2. Overrides: requires
        for (size_t i = 0; i < rec.overrides.requires_rules.size(); ++i) {
            RequireRule rule = rec.overrides.requires_rules[i];
            std::string rptr = rec_ptr + "/overrides/requires/" + std::to_string(i) + "/expr";
            if (!compileOverrideFormula(
                    rule.expr_src, rptr, expr::ExprContext::PreSolveLet,
                    expr::ExpectedType::Bool, true, false, rule.compiled_expr)) {
                if (out_cascade) return false;
                ok = false;
            } else {
                out_compiled.extra_requires.push_back(std::move(rule));
            }
        }

        // 3. Overrides: craft_time, input_amount, output_amount
        if (rec.overrides.craft_time.has_value()) {
            expr::CompiledExpr cexpr;
            if (!compileOverrideFormula(
                    *rec.overrides.craft_time, rec_ptr + "/overrides/craft_time",
                    expr::ExprContext::PreSolveCraftTime, expr::ExpectedType::Number,
                    true, false, cexpr)) {
                if (out_cascade) return false;
                ok = false;
            } else {
                out_compiled.craft_time_override = std::move(cexpr);
            }
        }
        if (rec.overrides.input_amount.has_value()) {
            expr::CompiledExpr cexpr;
            if (!compileOverrideFormula(
                    *rec.overrides.input_amount, rec_ptr + "/overrides/input_amount",
                    expr::ExprContext::RecipePortFormula, expr::ExpectedType::Number,
                    true, false, cexpr)) {
                if (out_cascade) return false;
                ok = false;
            } else {
                out_compiled.input_amount_override = std::move(cexpr);
            }
        }
        if (rec.overrides.output_amount.has_value()) {
            expr::CompiledExpr cexpr;
            if (!compileOverrideFormula(
                    *rec.overrides.output_amount, rec_ptr + "/overrides/output_amount",
                    expr::ExprContext::RecipePortFormula, expr::ExpectedType::Number,
                    true, false, cexpr)) {
                if (out_cascade) return false;
                ok = false;
            } else {
                out_compiled.output_amount_override = std::move(cexpr);
            }
        }

        auto compileOverrideBool = [&](const BoolOrExpr& src_be,
                                       const std::string& ptr,
                                       BoolOrExpr& out_be) {
            if (!src_be.is_present) return;
            out_be = src_be;
            if (!src_be.is_literal) {
                if (!compileOverrideFormula(
                        src_be.expr_src, ptr, expr::ExprContext::RecipePortFormula,
                        expr::ExpectedType::Bool, true, false, out_be.compiled_expr)) {
                    if (!out_cascade) ok = false;
                }
            }
        };

        compileOverrideBool(
            rec.overrides.port_enabled, rec_ptr + "/overrides/port_enabled",
            out_compiled.port_enabled_override);
        compileOverrideBool(
            rec.overrides.input_enabled, rec_ptr + "/overrides/input_enabled",
            out_compiled.input_enabled_override);
        compileOverrideBool(
            rec.overrides.output_enabled, rec_ptr + "/overrides/output_enabled",
            out_compiled.output_enabled_override);
        if (out_cascade) return false;

        // 4. Overrides: metrics
        for (const std::string& mid : rec.overrides.metric_order) {
            ProfileMetricEntry mentry = rec.overrides.metrics.at(mid);
            std::string mptr = rec_ptr + "/overrides/metrics/" + mid;
            if (mentry.enabled.is_present && !mentry.enabled.is_literal) {
                if (!compileOverrideFormula(
                        mentry.enabled.expr_src, mptr + "/enabled",
                        expr::ExprContext::PreSolveAfterCraftTime, expr::ExpectedType::Bool,
                        true, false, mentry.enabled.compiled_expr)) {
                    if (out_cascade) return false;
                    ok = false;
                    continue;
                }
            }
            expr::ExprContext mctx = (mentry.form == MetricFormKind::Value)
                ? expr::ExprContext::PostSolveMetricValue
                : expr::ExprContext::PreSolveAfterCraftTime;
            if (!compileOverrideFormula(
                    mentry.expr_src, mptr, mctx, expr::ExpectedType::Number,
                    true, false, mentry.compiled_expr)) {
                if (out_cascade) return false;
                ok = false;
                continue;
            }
            out_compiled.metric_override_order.push_back(mid);
            out_compiled.metric_overrides[mid] = std::move(mentry);
        }

        // 5. Recipe port formulas (resource_expr, enabled, amount_expr)
        auto compilePortsForProfile = [&](const std::vector<RecipePort>& ports,
                                          const char* dir_key,
                                          std::vector<CompiledRecipePort>& out_cports) {
            out_cports.resize(ports.size());
            for (size_t i = 0; i < ports.size(); ++i) {
                const RecipePort& p = ports[i];
                CompiledRecipePort& cp = out_cports[i];
                std::string pptr = rec_ptr + "/" + dir_key + "/" + std::to_string(i);
                const bool has_base_res = p.res.has_value();

                if (p.resource_expr.has_value()) {
                    cp.has_resource_expr = true;
                    if (!compileOverrideFormula(
                            *p.resource_expr, pptr + "/resource_expr",
                            expr::ExprContext::RecipePortResourceExpr,
                            expr::ExpectedType::ResourceOrString,
                            has_base_res, true, cp.compiled_resource_expr)) {
                        if (out_cascade) return;
                        ok = false;
                    }
                }
                if (p.enabled_override.is_present) {
                    cp.compiled_enabled = p.enabled_override;
                    if (!p.enabled_override.is_literal) {
                        if (!compileOverrideFormula(
                                p.enabled_override.expr_src, pptr + "/enabled",
                                expr::ExprContext::RecipePortFormula,
                                expr::ExpectedType::Bool,
                                has_base_res, false, cp.compiled_enabled.compiled_expr)) {
                            if (out_cascade) return;
                            ok = false;
                        }
                    }
                }
                if (p.amount_expr_override.has_value()) {
                    cp.has_amount_expr = true;
                    if (!compileOverrideFormula(
                            *p.amount_expr_override, pptr + "/amount_expr",
                            expr::ExprContext::RecipePortFormula,
                            expr::ExpectedType::Number,
                            has_base_res, false, cp.compiled_amount_expr)) {
                        if (out_cascade) return;
                        ok = false;
                    }
                }
            }
        };

        compilePortsForProfile(rec.in, "in", out_compiled.in_ports);
        if (out_cascade) return false;
        compilePortsForProfile(rec.out, "out", out_compiled.out_ports);
        if (out_cascade) return false;

        return ok && !out_cascade;
    }

    void runStage8() {
        static const std::vector<std::string> kRecipeKeys = {
            "attrs", "category", "id", "in", "machines", "name",
            "notes", "out", "overrides", "setup", "time"
        };
        static const std::vector<std::string> kOverrideKeys = {
            "craft_time", "input_amount", "input_enabled", "let", "metrics",
            "notes", "output_amount", "output_enabled", "port_enabled", "requires"
        };
        static const std::vector<std::string> kRequireKeys = {"expr", "message", "notes"};
        static const std::vector<std::string> kMetricEntryKeys = {
            "enabled", "notes", "per_craft", "per_machine", "value"
        };

        expr::CompileEnv base_env = buildBaseCompileEnv();
        std::unordered_map<std::string, std::pair<std::string, std::string>> first_seen_rec;
        std::unordered_set<std::string> params_used_as_recipe_port_param;

        for (const RawArrayItem& item : m_rawRecipes) {
            if (!item.value.is_object()) {
                addError("V-STRUCT-2", item.file, item.json_pointer,
                         "expected object for recipe, got " + jsonTypeName(item.value));
                continue;
            }
            const json& rj = item.value;
            bool ok = checkAllowedKeys(rj, item.file, item.json_pointer, kRecipeKeys);
            bool cascade = false;

            std::string id;
            if (!rj.contains("id")) {
                addError("V-ID-1", item.file, item.json_pointer + "/id", "missing required recipe 'id'");
                ok = false;
            } else if (!rj["id"].is_string()) {
                addError("V-STRUCT-2", item.file, item.json_pointer + "/id",
                         "expected string for recipe 'id', got " + jsonTypeName(rj["id"]));
                ok = false;
            } else {
                id = rj["id"].get<std::string>();
                if (!expr::isValidSlugId(id)) {
                    addError("V-ID-1", item.file, item.json_pointer + "/id",
                             "invalid recipe id '" + id + "' (must match [a-z0-9_-]+)");
                    ok = false;
                }
            }

            if (!id.empty()) {
                auto seen_it = first_seen_rec.find(id);
                if (seen_it != first_seen_rec.end()) {
                    addError("V-ID-2", item.file, item.json_pointer + "/id",
                             "duplicate recipe id '" + id + "' (first defined at " +
                                 seen_it->second.first + ": " + seen_it->second.second + ")");
                    markDropped("recipes", id);
                    continue;
                }
                first_seen_rec[id] = {item.file, item.json_pointer};
            }

            Recipe rec;
            rec.id = id;
            rec.name = id;

            if (rj.contains("name")) {
                if (!rj["name"].is_string()) {
                    addError("V-STRUCT-2", item.file, item.json_pointer + "/name",
                             "expected string for 'name', got " + jsonTypeName(rj["name"]));
                    ok = false;
                } else {
                    rec.name = rj["name"].get<std::string>();
                }
            }

            if (!rj.contains("time")) {
                addError("V-STRUCT-2", item.file, item.json_pointer + "/time",
                         "recipe requires 'time'");
                ok = false;
            } else if (!rj["time"].is_number()) {
                addError("V-STRUCT-2", item.file, item.json_pointer + "/time",
                         "expected number for 'time', got " + jsonTypeName(rj["time"]));
                ok = false;
            } else {
                double t = rj["time"].get<double>();
                if (!std::isfinite(t) || t <= 0.0) {
                    addError("V-NUM-1", item.file, item.json_pointer + "/time",
                             "must be > 0, got " + std::to_string(t));
                    ok = false;
                } else {
                    rec.time_raw = t;
                    rec.time_seconds = (m_pkg.units.recipe_time == RecipeTimeUnit::Ticks)
                        ? (t / m_pkg.units.ticks_per_second)
                        : t;
                }
            }

            // V-REF-2 & V-REF-3: category vs machines
            const bool has_cat = rj.contains("category");
            const bool has_machs = rj.contains("machines");
            if (has_cat == has_machs) {
                addError("V-REF-2", item.file, item.json_pointer,
                         "specify exactly one of 'category' or 'machines'");
                ok = false;
            } else if (has_cat) {
                if (!rj["category"].is_string()) {
                    addError("V-STRUCT-2", item.file, item.json_pointer + "/category",
                             "expected string for 'category', got " + jsonTypeName(rj["category"]));
                    ok = false;
                } else {
                    std::string cat = rj["category"].get<std::string>();
                    if (!expr::isValidSlugId(cat)) {
                        addError("V-ID-1", item.file, item.json_pointer + "/category",
                                 "invalid category '" + cat + "' (must match [a-z0-9_-]+)");
                        ok = false;
                    } else {
                        rec.category = cat;
                        auto it = m_pkg.category_to_machines.find(cat);
                        if (it == m_pkg.category_to_machines.end() || it->second.empty()) {
                            if (m_categoriesOfFailedMachines.count(cat)) {
                                cascade = true;
                                ok = false;
                            } else {
                                addError("V-REF-3", item.file, item.json_pointer,
                                         "no machine has category '" + cat + "'");
                                ok = false;
                            }
                        } else {
                            rec.compatible_machine_ids = it->second;
                            rec.compatible_profile_ids = m_pkg.category_to_profiles.at(cat);
                            for (const auto& mid : rec.compatible_machine_ids) {
                                m_usedMachines.insert(mid);
                            }
                        }
                    }
                }
            } else if (has_machs) {
                if (!rj["machines"].is_array()) {
                    addError("V-STRUCT-2", item.file, item.json_pointer + "/machines",
                             "expected array for 'machines', got " + jsonTypeName(rj["machines"]));
                    ok = false;
                } else if (rj["machines"].empty()) {
                    addError("V-REF-3", item.file, item.json_pointer + "/machines",
                             "recipe 'machines' list cannot be empty");
                    ok = false;
                } else {
                    bool had_failed_machine = false;
                    for (size_t mi = 0; mi < rj["machines"].size(); ++mi) {
                        const json& me = rj["machines"][mi];
                        std::string mptr = item.json_pointer + "/machines/" + std::to_string(mi);
                        if (!me.is_string()) {
                            addError("V-STRUCT-2", item.file, mptr,
                                     "expected machine id string, got " + jsonTypeName(me));
                            ok = false;
                            continue;
                        }
                        std::string mid = me.get<std::string>();
                        const Machine* mptr_obj = m_pkg.findMachine(mid);
                        if (mptr_obj == nullptr) {
                            if (m_failedMachines.count(mid)) {
                                had_failed_machine = true;
                            } else {
                                addError("V-REF-2", item.file, mptr,
                                         "unknown machine '" + mid + "'");
                                ok = false;
                            }
                        } else {
                            rec.explicit_machines.push_back(mid);
                            addUnique(rec.compatible_machine_ids, mid);
                            addUnique(rec.compatible_profile_ids, mptr_obj->profile);
                            m_usedMachines.insert(mid);
                        }
                    }
                    if (rec.compatible_machine_ids.empty() && had_failed_machine) {
                        cascade = true;
                        ok = false;
                    }
                }
            }

            // setup
            if (rj.contains("setup")) {
                if (!rj["setup"].is_array()) {
                    addError("V-STRUCT-2", item.file, item.json_pointer + "/setup",
                             "expected array for 'setup', got " + jsonTypeName(rj["setup"]));
                    ok = false;
                } else {
                    for (size_t si = 0; si < rj["setup"].size(); ++si) {
                        const json& se = rj["setup"][si];
                        std::string sptr = item.json_pointer + "/setup/" + std::to_string(si);
                        if (!se.is_array() || se.size() != 2 || !se[0].is_string() || !se[1].is_number()) {
                            addError("V-STRUCT-2", item.file, sptr,
                                     "setup item must be a [resource, amount] tuple");
                            ok = false;
                            continue;
                        }
                        std::string s_res = se[0].get<std::string>();
                        double s_amt = se[1].get<double>();
                        if (m_pkg.findResource(s_res) == nullptr) {
                            if (m_failedResources.count(s_res)) {
                                cascade = true;
                                ok = false;
                            } else {
                                addError("V-REF-1", item.file, sptr + "/0",
                                         "unknown resource '" + s_res + "'");
                                ok = false;
                            }
                        } else {
                            m_usedResources.insert(s_res);
                        }
                        if (!std::isfinite(s_amt) || s_amt <= 0.0) {
                            addError("V-NUM-2", item.file, sptr + "/1",
                                     "setup amount must be > 0");
                            ok = false;
                        }
                        rec.setup.push_back(SetupItem{s_res, s_amt});
                    }
                }
            }

            // attrs
            if (rj.contains("attrs")) {
                if (!validateEntityAttrs(
                        "recipe", "/attributes/recipe",
                        m_pkg.attributes["recipe"], m_failedAttrs["recipe"],
                        rj["attrs"], item.file, item.json_pointer + "/attrs",
                        rec.attrs, cascade)) {
                    ok = false;
                }
            }

            // in and out ports
            std::unordered_set<std::string> seen_resource_params_on_recipe;
            if (rj.contains("in")) {
                if (!parseRecipePortList(
                        rj["in"], "in", item.file, item.json_pointer + "/in",
                        rec.in, seen_resource_params_on_recipe, cascade)) {
                    ok = false;
                }
            }
            if (rj.contains("out")) {
                if (!parseRecipePortList(
                        rj["out"], "out", item.file, item.json_pointer + "/out",
                        rec.out, seen_resource_params_on_recipe, cascade)) {
                    ok = false;
                }
            }

            for (const std::string& rp : seen_resource_params_on_recipe) {
                params_used_as_recipe_port_param.insert(rp);
                auto pit = m_pkg.parameter_index.find(rp);
                if (pit != m_pkg.parameter_index.end()) {
                    m_pkg.parameters[pit->second].is_port_bound = true;
                }
            }

            if (rec.in.empty() && rec.out.empty() && !cascade) {
                if ((!rj.contains("in") || (rj["in"].is_array() && rj["in"].empty())) &&
                    (!rj.contains("out") || (rj["out"].is_array() && rj["out"].empty()))) {
                    addError("V-NUM-4", item.file, item.json_pointer,
                             "recipe has no inputs and no outputs");
                    ok = false;
                }
            }

            // Precompute static item/fluid totals (§5.5)
            auto accumulateTotals = [&](const std::vector<RecipePort>& ports,
                                        double& out_items,
                                        double& out_fluids) {
                for (const auto& p : ports) {
                    if (!p.res.has_value()) continue;
                    const Resource* rptr = m_pkg.findResource(*p.res);
                    if (rptr == nullptr) continue;
                    if (rptr->kind == "item") {
                        out_items += p.amount;
                    } else if (rptr->kind == "fluid") {
                        out_fluids += p.amount;
                    }
                }
            };
            accumulateTotals(rec.in, rec.input_items_total, rec.input_fluids_total);
            accumulateTotals(rec.out, rec.output_items_total, rec.output_fluids_total);

            // Parse overrides structure (V-OVR-1)
            if (rj.contains("overrides")) {
                const json& ovj = rj["overrides"];
                std::string ov_ptr = item.json_pointer + "/overrides";
                if (!ovj.is_object()) {
                    addError("V-STRUCT-2", item.file, ov_ptr,
                             "expected object for 'overrides', got " + jsonTypeName(ovj));
                    ok = false;
                } else {
                    rec.overrides.has_any = true;
                    for (auto it = ovj.begin(); it != ovj.end(); ++it) {
                        const std::string& k = it.key();
                        if (std::find(kOverrideKeys.begin(), kOverrideKeys.end(), k) ==
                            kOverrideKeys.end()) {
                            addError("V-OVR-1", item.file, ov_ptr + "/" + k,
                                     "'" + k + "' cannot be overridden per recipe");
                            ok = false;
                        }
                    }

                    auto readStrOvr = [&](const char* f, std::optional<std::string>& out_s) {
                        if (!ovj.contains(f)) return;
                        if (!ovj[f].is_string()) {
                            addError("V-STRUCT-2", item.file, ov_ptr + "/" + f,
                                     std::string("expected string for 'overrides.") + f + "', got " +
                                         jsonTypeName(ovj[f]));
                            ok = false;
                        } else {
                            out_s = ovj[f].get<std::string>();
                        }
                    };
                    readStrOvr("craft_time", rec.overrides.craft_time);
                    readStrOvr("input_amount", rec.overrides.input_amount);
                    readStrOvr("output_amount", rec.overrides.output_amount);

                    auto readBoolOvr = [&](const char* f, BoolOrExpr& out_be) {
                        if (!ovj.contains(f)) return;
                        const json& fj = ovj[f];
                        if (fj.is_boolean()) {
                            out_be.is_present = true;
                            out_be.is_literal = true;
                            out_be.literal_value = fj.get<bool>();
                        } else if (fj.is_string()) {
                            out_be.is_present = true;
                            out_be.is_literal = false;
                            out_be.expr_src = fj.get<std::string>();
                        } else {
                            addError("V-STRUCT-2", item.file, ov_ptr + "/" + f,
                                     std::string("expected bool or string for 'overrides.") + f +
                                         "', got " + jsonTypeName(fj));
                            ok = false;
                        }
                    };
                    readBoolOvr("port_enabled", rec.overrides.port_enabled);
                    readBoolOvr("input_enabled", rec.overrides.input_enabled);
                    readBoolOvr("output_enabled", rec.overrides.output_enabled);

                    if (ovj.contains("let")) {
                        if (!ovj["let"].is_object()) {
                            addError("V-STRUCT-2", item.file, ov_ptr + "/let",
                                     "expected object for 'overrides.let', got " + jsonTypeName(ovj["let"]));
                            ok = false;
                        } else {
                            for (auto it = ovj["let"].begin(); it != ovj["let"].end(); ++it) {
                                const std::string& lname = it.key();
                                if (lname == "notes" && it.value().is_string()) continue;
                                if (!it.value().is_string()) {
                                    addError("V-STRUCT-2", item.file, ov_ptr + "/let/" + lname,
                                             "expected expression string for override let '" + lname + "'");
                                    ok = false;
                                } else {
                                    rec.overrides.let[lname] = it.value().get<std::string>();
                                }
                            }
                        }
                    }

                    if (ovj.contains("requires")) {
                        if (!ovj["requires"].is_array()) {
                            addError("V-STRUCT-2", item.file, ov_ptr + "/requires",
                                     "expected array for 'overrides.requires'");
                            ok = false;
                        } else {
                            for (size_t ri = 0; ri < ovj["requires"].size(); ++ri) {
                                const json& req_j = ovj["requires"][ri];
                                std::string rptr = ov_ptr + "/requires/" + std::to_string(ri);
                                if (!req_j.is_object() ||
                                    !checkAllowedKeys(req_j, item.file, rptr, kRequireKeys) ||
                                    !req_j.contains("expr") || !req_j["expr"].is_string() ||
                                    !req_j.contains("message") || !req_j["message"].is_string()) {
                                    addError("V-STRUCT-2", item.file, rptr,
                                             "override requirement must have string 'expr' and 'message'");
                                    ok = false;
                                    continue;
                                }
                                RequireRule rr;
                                rr.expr_src = req_j["expr"].get<std::string>();
                                rr.message = req_j["message"].get<std::string>();
                                rec.overrides.requires_rules.push_back(std::move(rr));
                            }
                        }
                    }

                    if (ovj.contains("metrics")) {
                        if (!ovj["metrics"].is_object()) {
                            addError("V-STRUCT-2", item.file, ov_ptr + "/metrics",
                                     "expected object for 'overrides.metrics'");
                            ok = false;
                        } else {
                            for (auto it = ovj["metrics"].begin(); it != ovj["metrics"].end(); ++it) {
                                const std::string& mid = it.key();
                                if (mid == "notes" && it.value().is_string()) continue;
                                std::string mptr = ov_ptr + "/metrics/" + mid;
                                if (m_failedMetrics.count(mid)) {
                                    continue;
                                }
                                if (m_pkg.findMetric(mid) == nullptr) {
                                    addError("V-PROF-3", item.file, mptr,
                                             "unknown metric '" + mid + "' in recipe overrides");
                                    ok = false;
                                    continue;
                                }
                                const json& mej = it.value();
                                if (!mej.is_object()) {
                                    addError("V-STRUCT-2", item.file, mptr,
                                             "expected object for metric override");
                                    ok = false;
                                    continue;
                                }
                                if (!checkAllowedKeys(mej, item.file, mptr, kMetricEntryKeys)) {
                                    ok = false;
                                }
                                int fcount = 0;
                                if (mej.contains("per_machine")) ++fcount;
                                if (mej.contains("per_craft")) ++fcount;
                                if (mej.contains("value")) ++fcount;
                                if (fcount != 1) {
                                    addError("V-PROF-3", item.file, mptr,
                                             "give exactly one of per_machine, per_craft, value");
                                    ok = false;
                                    continue;
                                }
                                ProfileMetricEntry pme;
                                pme.metric_id = mid;
                                pme.enabled.is_present = true;
                                pme.enabled.is_literal = true;
                                pme.enabled.literal_value = true;
                                if (mej.contains("enabled")) {
                                    if (mej["enabled"].is_boolean()) {
                                        pme.enabled.is_literal = true;
                                        pme.enabled.literal_value = mej["enabled"].get<bool>();
                                    } else if (mej["enabled"].is_string()) {
                                        pme.enabled.is_literal = false;
                                        pme.enabled.expr_src = mej["enabled"].get<std::string>();
                                    } else {
                                        addError("V-STRUCT-2", item.file, mptr + "/enabled",
                                                 "expected bool or string for 'enabled'");
                                        ok = false;
                                    }
                                }
                                const char* fk = mej.contains("per_machine")
                                    ? "per_machine"
                                    : (mej.contains("per_craft") ? "per_craft" : "value");
                                pme.form = mej.contains("per_machine")
                                    ? MetricFormKind::PerMachine
                                    : (mej.contains("per_craft") ? MetricFormKind::PerCraft : MetricFormKind::Value);
                                if (mej[fk].is_number()) {
                                    pme.expr_src = std::to_string(mej[fk].get<double>());
                                } else if (mej[fk].is_string()) {
                                    pme.expr_src = mej[fk].get<std::string>();
                                } else {
                                    addError("V-STRUCT-2", item.file, mptr + "/" + fk,
                                             "expected expression string for metric override");
                                    ok = false;
                                    continue;
                                }
                                m_usedMetrics.insert(mid);
                                rec.overrides.metric_order.push_back(mid);
                                rec.overrides.metrics[mid] = std::move(pme);
                            }
                        }
                    }
                }
            }

            // Compile overrides and port formulas against each unique compatible profile (§2.9 item 2)
            if (ok && !cascade) {
                for (const std::string& prof_id : rec.compatible_profile_ids) {
                    const Profile* prof_ptr = m_pkg.findProfile(prof_id);
                    if (prof_ptr == nullptr) continue;

                    std::string rep_mach = rec.compatible_machine_ids.empty()
                        ? ""
                        : rec.compatible_machine_ids.front();
                    for (const std::string& mid : rec.compatible_machine_ids) {
                        const Machine* m = m_pkg.findMachine(mid);
                        if (m != nullptr && m->profile == prof_id) {
                            rep_mach = mid;
                            break;
                        }
                    }

                    CompiledRecipeProfile crp;
                    if (!validateAndCompileRecipeForProfile(
                            rec, *prof_ptr, rep_mach, item.file, item.json_pointer,
                            base_env, crp, cascade)) {
                        ok = false;
                        if (cascade) break;
                    } else {
                        rec.compiled_by_profile.emplace(prof_id, std::move(crp));
                    }
                }
            }

            if (ok && !cascade && !rec.id.empty()) {
                m_pkg.recipe_index[rec.id] = m_pkg.recipes.size();
                m_pkg.recipes.push_back(std::move(rec));
            } else if (!id.empty()) {
                m_failedRecipes.insert(id);
                markDropped("recipes", id);
            }
        }
    }

    // ========================================================================
    // Stage 9: Unused Entity Warnings (V-W-2) & Base EvalEnv (§2.9 item 5)
    // ========================================================================

    void runStage9() {
        // Also mark resources referenced through string attributes read by resource_expr (R2-08 item 2)
        if (!m_resourceAttrsUsedInResourceExpr.empty()) {
            for (const auto& res : m_pkg.resources) {
                for (const auto& attr_name : m_resourceAttrsUsedInResourceExpr) {
                    auto it = res.attrs.find(attr_name);
                    if (it != res.attrs.end() && it->second.isString() && !it->second.asString().empty()) {
                        m_usedResources.insert(it->second.asString());
                    }
                }
            }
        }
        if (!m_machineAttrsUsedInResourceExpr.empty()) {
            for (const auto& mach : m_pkg.machines) {
                for (const auto& attr_name : m_machineAttrsUsedInResourceExpr) {
                    auto it = mach.attrs.find(attr_name);
                    if (it != mach.attrs.end() && it->second.isString() && !it->second.asString().empty()) {
                        m_usedResources.insert(it->second.asString());
                    }
                }
            }
        }
        if (!m_recipeAttrsUsedInResourceExpr.empty()) {
            for (const auto& rec : m_pkg.recipes) {
                for (const auto& attr_name : m_recipeAttrsUsedInResourceExpr) {
                    auto it = rec.attrs.find(attr_name);
                    if (it != rec.attrs.end() && it->second.isString() && !it->second.asString().empty()) {
                        m_usedResources.insert(it->second.asString());
                    }
                }
            }
        }
        if (!m_portAttrsUsedInResourceExpr.empty()) {
            for (const auto& rec : m_pkg.recipes) {
                for (const auto& p : rec.in) {
                    for (const auto& attr_name : m_portAttrsUsedInResourceExpr) {
                        auto it = p.attrs.find(attr_name);
                        if (it != p.attrs.end() && it->second.isString() && !it->second.asString().empty()) {
                            m_usedResources.insert(it->second.asString());
                        }
                    }
                }
                for (const auto& p : rec.out) {
                    for (const auto& attr_name : m_portAttrsUsedInResourceExpr) {
                        auto it = p.attrs.find(attr_name);
                        if (it != p.attrs.end() && it->second.isString() && !it->second.asString().empty()) {
                            m_usedResources.insert(it->second.asString());
                        }
                    }
                }
            }
        }

        // Only emit V-W-2 when no entities were dropped due to errors (so failed entities don't
        // cause cascading false-positive unused warnings on the entities they referenced).
        if (m_report.errors.empty()) {
            for (const auto& p : m_pkg.parameters) {
                if (!m_usedParameters.count(p.id)) {
                    const RawEntry& raw = m_rawParameters.at(p.id);
                    addWarning("V-W-2", raw.file, raw.json_pointer,
                               "defined but not used by any profile");
                }
            }
            for (const auto& oset : m_pkg.option_sets) {
                if (!m_usedOptionSets.count(oset.id)) {
                    const RawEntry& raw = m_rawOptionSets.at(oset.id);
                    addWarning("V-W-2", raw.file, raw.json_pointer,
                               "option_set '" + oset.id + "' is defined but not used by any parameter or global");
                }
            }
            for (const auto& tbl : m_pkg.tables) {
                if (!m_usedTables.count(tbl.id)) {
                    const RawEntry& raw = m_rawTables.at(tbl.id);
                    addWarning("V-W-2", raw.file, raw.json_pointer,
                               "table '" + tbl.id + "' is defined but not used by any expression");
                }
            }
            for (const auto& met : m_pkg.metrics) {
                if (!m_usedMetrics.count(met.id)) {
                    const RawEntry& raw = m_rawMetrics.at(met.id);
                    addWarning("V-W-2", raw.file, raw.json_pointer,
                               "metric '" + met.id + "' is defined but not used by any profile or recipe");
                }
            }
            for (size_t i = 0; i < m_pkg.machines.size(); ++i) {
                const auto& mach = m_pkg.machines[i];
                if (!m_usedMachines.count(mach.id)) {
                    addWarning("V-W-2", "game.json", "/machines/" + std::to_string(i),
                               "machine '" + mach.id + "' cannot run any loaded recipe");
                }
            }
            for (size_t i = 0; i < m_pkg.resources.size(); ++i) {
                const auto& res = m_pkg.resources[i];
                if (!m_usedResources.count(res.id)) {
                    addWarning("V-W-2", "game.json", "/resources/" + std::to_string(i),
                               "resource '" + res.id + "' is not referenced by any recipe, profile, or parameter");
                }
            }
        }

        // Populate m_pkg.base_eval_env
        expr::EvalEnv& env = m_pkg.base_eval_env;
        env.game_ticks_per_second = m_pkg.units.ticks_per_second;
        env.game_attrs = m_pkg.game.attrs;

        for (const auto& [k, decl] : m_pkg.attributes["machine"]) {
            env.machine_attr_defaults[k] = decl.default_value;
        }
        for (const auto& [k, decl] : m_pkg.attributes["recipe"]) {
            env.recipe_attr_defaults[k] = decl.default_value;
        }
        for (const auto& [k, decl] : m_pkg.attributes["port"]) {
            env.port_attr_defaults[k] = decl.default_value;
        }
        for (const auto& [k, decl] : m_pkg.attributes["resource"]) {
            env.resource_attr_defaults[k] = decl.default_value;
        }

        for (const auto& res : m_pkg.resources) {
            expr::ResourceEvalData rdata;
            rdata.tags = res.tag_set;
            rdata.attrs = res.attrs;
            env.resources[res.id] = std::move(rdata);
        }

        for (const auto& oset : m_pkg.option_sets) {
            expr::OptionSetEvalData odata;
            for (const auto& [ak, adecl] : oset.attrs) {
                odata.attr_defaults[ak] = adecl.default_value;
            }
            for (const auto& opt : oset.options) {
                odata.option_attrs[opt.key] = opt.attrs;
            }
            env.option_sets[oset.id] = std::move(odata);
        }

        for (const auto& tbl : m_pkg.tables) {
            if (tbl.kind == expr::TableKind::Array) {
                env.array_tables[tbl.id] = tbl.array_values;
            } else {
                env.object_tables[tbl.id] = tbl.object_values;
            }
        }
    }

    std::filesystem::path m_packageRoot;
    const std::unordered_map<std::string, std::string>* m_virtualShards = nullptr;
    GamePackage& m_pkg;
    LoadReport& m_report;
    expr::SuggestionBudget m_suggestionBudget;

    json m_manifestJson;

    // Raw collected entries in deterministic file order
    std::vector<std::string> m_resourceKindOrder;
    std::unordered_map<std::string, RawEntry> m_rawResourceKinds;

    std::unordered_map<std::string, std::vector<std::string>> m_attributeOrder;
    std::unordered_map<std::string, std::unordered_map<std::string, RawEntry>> m_rawAttributes;

    std::vector<std::string> m_tableOrder;
    std::unordered_map<std::string, RawEntry> m_rawTables;

    std::vector<std::string> m_optionSetOrder;
    std::unordered_map<std::string, RawEntry> m_rawOptionSets;

    std::vector<std::string> m_parameterOrder;
    std::unordered_map<std::string, RawEntry> m_rawParameters;

    std::vector<std::string> m_globalOrder;
    std::unordered_map<std::string, RawEntry> m_rawGlobals;

    std::vector<std::string> m_metricOrder;
    std::unordered_map<std::string, RawEntry> m_rawMetrics;

    std::vector<std::string> m_profileOrder;
    std::unordered_map<std::string, RawEntry> m_rawProfiles;

    std::vector<RawArrayItem> m_rawResources;
    std::vector<RawArrayItem> m_rawMachines;
    std::vector<RawArrayItem> m_rawRecipes;

    // Failed entity sets for secondary error suppression (§9.1)
    std::unordered_set<std::string> m_failedResourceKinds;
    std::unordered_map<std::string, std::unordered_set<std::string>> m_failedAttrs;
    std::unordered_set<std::string> m_failedResources;
    std::unordered_set<std::string> m_tagsOfFailedResources;
    std::unordered_set<std::string> m_failedOptionSets;
    std::unordered_set<std::string> m_failedTables;
    std::unordered_set<std::string> m_failedParameters;
    std::unordered_set<std::string> m_failedGlobals;
    std::unordered_set<std::string> m_failedMetrics;
    std::unordered_set<std::string> m_failedProfiles;
    std::unordered_set<std::string> m_failedMachines;
    std::unordered_set<std::string> m_categoriesOfFailedMachines;
    std::unordered_set<std::string> m_failedRecipes;

    // Usage sets for V-W-2 single-pass marking (§2.9 item 5)
    std::unordered_set<std::string> m_usedParameters;
    std::unordered_set<std::string> m_usedOptionSets;
    std::unordered_set<std::string> m_usedTables;
    std::unordered_set<std::string> m_usedMetrics;
    std::unordered_set<std::string> m_usedMachines;
    std::unordered_set<std::string> m_usedResources;

    std::unordered_set<std::string> m_resourceAttrsUsedInResourceExpr;
    std::unordered_set<std::string> m_machineAttrsUsedInResourceExpr;
    std::unordered_set<std::string> m_recipeAttrsUsedInResourceExpr;
    std::unordered_set<std::string> m_portAttrsUsedInResourceExpr;

    // Compiled expression cache (§2.9 item 2)
    std::unordered_map<ExpressionCacheKey, expr::CompileResult, ExpressionCacheKeyHash> m_exprCache;
};

} // namespace

bool loadGamePackage(
    const std::filesystem::path& manifestOrDir,
    GamePackage& outPackage,
    LoadReport& outReport,
    bool ignoreErrors) {
    outPackage = GamePackage{};
    outReport = LoadReport{};

    std::error_code ec;
    std::filesystem::path manifest_path = manifestOrDir;
    std::filesystem::path package_root = manifestOrDir.parent_path();

    if (std::filesystem::is_directory(manifestOrDir, ec)) {
        package_root = manifestOrDir;
        manifest_path = manifestOrDir / "game.json";
    }

    GamePackage working_pkg;
    working_pkg.package_root = package_root;
    working_pkg.manifest_path = manifest_path;

    PackageLoader loader(package_root, nullptr, working_pkg, outReport);

    if (!std::filesystem::exists(manifest_path, ec) ||
        !std::filesystem::is_regular_file(manifest_path, ec)) {
        loader.addFatal("F-1", manifest_path.generic_string(), "", "cannot open file");
        return false;
    }

    std::ifstream ifs(manifest_path, std::ios::binary);
    if (!ifs) {
        loader.addFatal("F-1", manifest_path.generic_string(), "", "cannot open file");
        return false;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();

    std::string rel_name = manifest_path.filename().generic_string();
    if (rel_name.empty()) {
        rel_name = "game.json";
    }

    if (!loader.loadFromManifestText(ss.str(), rel_name)) {
        return false;
    }

    if (!outReport.errors.empty()) {
        if (!ignoreErrors || outReport.fatal) {
            return false;
        }
        outReport.degraded = true;
    }

    outPackage = std::move(working_pkg);
    return true;
}

bool loadGamePackageFromMemory(
    std::string_view manifestJson,
    const std::unordered_map<std::string, std::string>& virtualShards,
    GamePackage& outPackage,
    LoadReport& outReport,
    bool ignoreErrors,
    const std::filesystem::path& packageRoot) {
    outPackage = GamePackage{};
    outReport = LoadReport{};

    GamePackage working_pkg;
    working_pkg.package_root = packageRoot;
    working_pkg.manifest_path = packageRoot.empty()
        ? std::filesystem::path("game.json")
        : (packageRoot / "game.json");

    PackageLoader loader(packageRoot, &virtualShards, working_pkg, outReport);
    if (!loader.loadFromManifestText(manifestJson, "game.json")) {
        return false;
    }

    if (!outReport.errors.empty()) {
        if (!ignoreErrors || outReport.fatal) {
            return false;
        }
        outReport.degraded = true;
    }

    outPackage = std::move(working_pkg);
    return true;
}

void checkMissingIcons(const GamePackage& package, LoadReport& inOutReport) {
    std::error_code ec;
    auto checkFile = [&](const std::filesystem::path& rel_icon, const std::string& json_ptr) {
        std::filesystem::path full = package.icon_root / rel_icon;
        if (!std::filesystem::exists(full, ec)) {
            LoadDiagnostic d;
            d.severity = DiagnosticSeverity::Warning;
            d.rule_id = "V-W-1";
            d.file = (std::filesystem::path("icons") / rel_icon).generic_string();
            d.json_pointer = json_ptr;
            d.message = d.file + " not found";
            d.formatted = d.message;
            inOutReport.warnings.push_back(std::move(d));
        }
    };

    for (size_t i = 0; i < package.resources.size(); ++i) {
        checkFile(
            std::filesystem::path("resources") / (package.resources[i].id + ".png"),
            "/resources/" + std::to_string(i));
    }
    for (size_t i = 0; i < package.machines.size(); ++i) {
        checkFile(
            std::filesystem::path("machines") / (package.machines[i].id + ".png"),
            "/machines/" + std::to_string(i));
    }
    for (const auto& oset : package.option_sets) {
        for (size_t i = 0; i < oset.options.size(); ++i) {
            checkFile(
                std::filesystem::path("options") / oset.id / (oset.options[i].key + ".png"),
                "/option_sets/" + oset.id + "/options/" + std::to_string(i));
        }
    }
}

} // namespace gamedata
