#ifndef GAMEDATAV4_H
#define GAMEDATAV4_H

#include "core/expr/ExprAst.h"
#include "core/expr/ExprDiagnostic.h"
#include "core/expr/ExprEnv.h"
#include "core/expr/ExprValue.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace gamedata {

// ============================================================================
// 1. Units & Metadata (§2.2–§2.5)
// ============================================================================

enum class RecipeTimeUnit {
    Seconds,
    Ticks
};

enum class RateUnit {
    PerTick,
    PerSecond,
    PerMinute,
    PerHour,
    PerSec = PerSecond,
    PerMin = PerMinute
};

inline std::string rateUnitToString(RateUnit unit) {
    switch (unit) {
        case RateUnit::PerTick:
            return "tick";
        case RateUnit::PerSecond:
            return "s";
        case RateUnit::PerMinute:
            return "min";
        case RateUnit::PerHour:
            return "h";
    }
    return "s";
}

inline std::optional<RateUnit> rateUnitFromString(std::string_view s) {
    if (s == "tick") return RateUnit::PerTick;
    if (s == "s") return RateUnit::PerSecond;
    if (s == "min") return RateUnit::PerMinute;
    if (s == "h") return RateUnit::PerHour;
    return std::nullopt;
}

// Multiplier to convert a canonical per-second rate into the given display rate unit (§2.3).
inline double rateDisplayMultiplier(RateUnit unit, double ticks_per_second = 20.0) {
    switch (unit) {
        case RateUnit::PerTick:
            return (ticks_per_second > 0.0) ? (1.0 / ticks_per_second) : 1.0;
        case RateUnit::PerSecond:
            return 1.0;
        case RateUnit::PerMinute:
            return 60.0;
        case RateUnit::PerHour:
            return 3600.0;
    }
    return 1.0;
}

inline double toCanonicalRate(double display_rate, RateUnit unit, double ticks_per_second = 20.0) {
    double mult = rateDisplayMultiplier(unit, ticks_per_second);
    return (mult > 0.0) ? (display_rate / mult) : display_rate;
}

inline double fromCanonicalRate(double canonical_rate, RateUnit unit, double ticks_per_second = 20.0) {
    return canonical_rate * rateDisplayMultiplier(unit, ticks_per_second);
}

inline std::string rateUnitSuffix(RateUnit unit) {
    switch (unit) {
        case RateUnit::PerTick:
            return "/tick";
        case RateUnit::PerSecond:
            return "/s";
        case RateUnit::PerMinute:
            return "/min";
        case RateUnit::PerHour:
            return "/h";
    }
    return "/s";
}

struct UnitsDef {
    RecipeTimeUnit recipe_time = RecipeTimeUnit::Seconds;
    double ticks_per_second = 20.0;
    bool has_ticks_per_second = false;
    RateUnit default_rate = RateUnit::PerSecond;
    std::string notes;
};

struct GameMetadata {
    std::string id;
    std::string name;
    std::string data_version;
    std::unordered_map<std::string, expr::EvalValue> attrs;
    std::unordered_map<std::string, expr::AttrType> attr_types;
    std::string notes;
};

struct ResourceKindDef {
    std::string id;
    std::string label;
    std::string display_unit;
    double display_scale = 1.0;
    std::string notes;
};

struct AttributeDecl {
    std::string name;
    expr::AttrType type = expr::AttrType::Number;
    expr::EvalValue default_value;
    std::string notes;
};

// ============================================================================
// 2. Parameters, Globals, Option Sets & Tables (§3.1–§3.4)
// ============================================================================

enum class ParameterType {
    Number,
    Int,
    Bool,
    Enum,
    ResourceChoice,
    Slots
};

inline std::string parameterTypeToString(ParameterType t) {
    switch (t) {
        case ParameterType::Number:
            return "number";
        case ParameterType::Int:
            return "int";
        case ParameterType::Bool:
            return "bool";
        case ParameterType::Enum:
            return "enum";
        case ParameterType::ResourceChoice:
            return "resource_choice";
        case ParameterType::Slots:
            return "slots";
    }
    return "number";
}

enum class UiPlacement {
    Body,
    Panel
};

enum class UiWidget {
    Slider,
    Drag,
    Input,
    Stepper,
    Checkbox,
    Combo,
    SlotRow
};

inline UiWidget defaultWidgetForParameterType(ParameterType t) {
    switch (t) {
        case ParameterType::Number:
            return UiWidget::Drag;
        case ParameterType::Int:
            return UiWidget::Stepper;
        case ParameterType::Bool:
            return UiWidget::Checkbox;
        case ParameterType::Enum:
        case ParameterType::ResourceChoice:
            return UiWidget::Combo;
        case ParameterType::Slots:
            return UiWidget::SlotRow;
    }
    return UiWidget::Drag;
}

inline std::string uiWidgetToString(UiWidget w) {
    switch (w) {
        case UiWidget::Slider:
            return "slider";
        case UiWidget::Drag:
            return "drag";
        case UiWidget::Input:
            return "input";
        case UiWidget::Stepper:
            return "stepper";
        case UiWidget::Checkbox:
            return "checkbox";
        case UiWidget::Combo:
            return "combo";
        case UiWidget::SlotRow:
            return "slot_row";
    }
    return "drag";
}

struct ParameterUiDef {
    UiWidget widget = UiWidget::Drag;
    std::string format;
    std::string group;
    UiPlacement placement = UiPlacement::Panel;
    std::string notes;
};

// Project/node stored parameter value (§7.5, §8.1):
// number | bool | string (option key / resource id) | nullptr (unset allow_none) | slots array
struct ParamValue {
    using SlotsArray = std::vector<std::optional<std::string>>;
    using Variant = std::variant<std::nullptr_t, double, bool, std::string, SlotsArray>;

    Variant data = nullptr;

    ParamValue() : data(nullptr) {}
    ParamValue(std::nullptr_t) : data(nullptr) {}
    ParamValue(std::nullopt_t) : data(nullptr) {}
    ParamValue(double v) : data(expr::normalizeZero(v)) {}
    ParamValue(int v) : data(static_cast<double>(v)) {}
    ParamValue(int64_t v) : data(static_cast<double>(v)) {}
    ParamValue(bool v) : data(v) {}
    ParamValue(const char* s) : data(s ? Variant(std::string(s)) : Variant(nullptr)) {}
    ParamValue(std::string s) : data(std::move(s)) {}
    ParamValue(std::string_view s) : data(std::string(s)) {}
    ParamValue(SlotsArray slots_val) : data(std::move(slots_val)) {}
    ParamValue(std::initializer_list<std::optional<std::string>> slots_init)
        : data(SlotsArray(slots_init)) {}

    static ParamValue null() { return ParamValue(nullptr); }
    static ParamValue number(double v) { return ParamValue(v); }
    static ParamValue boolean(bool v) { return ParamValue(v); }
    static ParamValue string(std::string s) { return ParamValue(std::move(s)); }
    static ParamValue slots(SlotsArray s) { return ParamValue(std::move(s)); }
    static ParamValue slots(std::initializer_list<std::optional<std::string>> s) {
        return ParamValue(SlotsArray(s));
    }

    bool isNull() const { return std::holds_alternative<std::nullptr_t>(data); }
    bool isNumber() const { return std::holds_alternative<double>(data); }
    bool isBool() const { return std::holds_alternative<bool>(data); }
    bool isString() const { return std::holds_alternative<std::string>(data); }
    bool isSlots() const { return std::holds_alternative<SlotsArray>(data); }

    double asNumber() const { return std::get<double>(data); }
    bool asBool() const { return std::get<bool>(data); }
    const std::string& asString() const { return std::get<std::string>(data); }
    const SlotsArray& asSlots() const { return std::get<SlotsArray>(data); }

    bool operator==(const ParamValue& other) const { return data == other.data; }
    bool operator!=(const ParamValue& other) const { return data != other.data; }

    nlohmann::json toJson() const {
        if (isNull()) return nullptr;
        if (isNumber()) {
            double v = asNumber();
            if (std::isfinite(v) && std::floor(v) == v &&
                v >= -9007199254740992.0 && v <= 9007199254740992.0) {
                return static_cast<int64_t>(v);
            }
            return v;
        }
        if (isBool()) return asBool();
        if (isString()) return asString();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& item : asSlots()) {
            if (item.has_value()) {
                arr.push_back(*item);
            } else {
                arr.push_back(nullptr);
            }
        }
        return arr;
    }

    static std::optional<ParamValue> fromJson(const nlohmann::json& j) {
        if (j.is_null()) return ParamValue(nullptr);
        if (j.is_boolean()) return ParamValue(j.get<bool>());
        if (j.is_number()) return ParamValue(j.get<double>());
        if (j.is_string()) return ParamValue(j.get<std::string>());
        if (j.is_array()) {
            SlotsArray arr;
            arr.reserve(j.size());
            for (const auto& elem : j) {
                if (elem.is_null()) {
                    arr.push_back(std::nullopt);
                } else if (elem.is_string()) {
                    arr.push_back(elem.get<std::string>());
                } else {
                    return std::nullopt;
                }
            }
            return ParamValue(std::move(arr));
        }
        return std::nullopt;
    }
};

struct ScalarOrExpr {
    bool is_present = false;
    bool is_literal = true;
    double literal_value = 0.0;
    std::string expr_src;
    expr::CompiledExpr compiled_expr;
};

struct BoolOrExpr {
    bool is_present = false;
    bool is_literal = true;
    bool literal_value = true;
    std::string expr_src;
    expr::CompiledExpr compiled_expr;
};

struct ParameterDef {
    std::string id;
    std::string label;
    std::string tooltip;
    ParameterType type = ParameterType::Number;

    bool has_default = false;
    ParamValue default_value;

    ScalarOrExpr min;
    ScalarOrExpr max;
    double step = 1.0;
    bool has_step = false;
    std::string unit;

    std::string option_set;
    BoolOrExpr allowed_if;

    std::vector<std::string> resources;
    std::string tag;
    std::vector<std::string> candidate_resources; // Resolved candidate resource IDs in declaration order

    bool allow_none = false;

    ScalarOrExpr count;
    bool allow_empty = true;

    BoolOrExpr visible_if;
    ParameterUiDef ui;

    // True if this is a resource_choice parameter referenced by a recipe port's resource_param
    // or whose visible_if / allowed_if references port.* (§3.1, §4.5 Step 1, V-PROF-4).
    bool is_port_bound = false;
    bool uses_port_in_static = false;

    std::string notes;
};

struct OptionDef {
    std::string key;
    std::string label;
    std::unordered_map<std::string, expr::EvalValue> attrs;
    std::string notes;
};

struct OptionSet {
    std::string id;
    std::unordered_map<std::string, AttributeDecl> attrs;
    std::vector<OptionDef> options;
    std::unordered_map<std::string, size_t> option_index_by_key;
    std::string notes;

    const OptionDef* findOption(std::string_view key) const {
        auto it = option_index_by_key.find(std::string(key));
        if (it == option_index_by_key.end()) return nullptr;
        return &options[it->second];
    }
};

struct TableDef {
    std::string id;
    expr::TableKind kind = expr::TableKind::Array;
    std::vector<double> array_values;
    std::unordered_map<std::string, double> object_values;
    std::string notes;
};

// ============================================================================
// 3. Metrics & Profiles (§4, §6)
// ============================================================================

enum class MetricAggregate {
    Sum,
    Max,
    Min,
    None
};

enum class MetricDisplayLocation {
    Body,
    Tooltip,
    Totals
};

enum class MetricFormKind {
    PerMachine,
    PerCraft,
    Value
};

struct MetricDef {
    std::string id;
    std::string label;
    std::string unit;
    std::string format = "%.3g";
    std::vector<MetricDisplayLocation> display = {MetricDisplayLocation::Tooltip};
    MetricAggregate aggregate = MetricAggregate::Sum;
    bool split_sign = false;
    bool is_rate = false;
    std::string notes;
};

struct RequireRule {
    std::string expr_src;
    std::string message;
    expr::CompiledExpr compiled_expr;
    std::string notes;
};

struct ExtraPortDef {
    std::string id;
    std::string slot_key; // "xin:<id>" or "xout:<id>"
    std::string dir = "in";
    std::optional<std::string> resource;
    std::optional<std::string> resource_param;
    std::optional<std::string> resource_expr;
    expr::CompiledExpr compiled_resource_expr;
    std::string amount_expr;
    expr::CompiledExpr compiled_amount;
    BoolOrExpr enabled;
    std::string notes;
};

struct ProfileMetricEntry {
    std::string metric_id;
    BoolOrExpr enabled;
    MetricFormKind form = MetricFormKind::PerMachine;
    std::string expr_src;
    expr::CompiledExpr compiled_expr;
    std::string notes;
};

struct Profile {
    std::string id;
    std::string extends;
    std::vector<std::string> params;

    std::unordered_map<std::string, std::string> let;
    std::vector<std::string> let_evaluation_order;
    std::unordered_map<std::string, expr::CompiledExpr> compiled_lets;
    std::unordered_map<std::string, expr::ExprType> let_types;

    std::vector<RequireRule> requires_rules;

    std::string craft_time_expr = "recipe.time / machine.speed";
    expr::CompiledExpr compiled_craft_time;

    std::string input_amount_expr = "port.amount * port.chance";
    expr::CompiledExpr compiled_input_amount;

    std::string output_amount_expr = "port.amount * port.chance";
    expr::CompiledExpr compiled_output_amount;

    BoolOrExpr port_enabled;
    BoolOrExpr input_enabled;
    BoolOrExpr output_enabled;

    std::vector<ExtraPortDef> extra_ports;

    std::vector<std::string> metric_order;
    std::unordered_map<std::string, ProfileMetricEntry> metrics;

    std::string notes;
};

// ============================================================================
// 4. Entities: Resources, Machines, Recipes (§2.6–§2.8, §4.4)
// ============================================================================

struct Resource {
    std::string id;
    std::string name;
    std::string kind = "item";
    std::vector<std::string> tags;
    std::unordered_set<std::string> tag_set;
    std::unordered_map<std::string, expr::EvalValue> attrs;
    std::string notes;
};

struct Machine {
    std::string id;
    std::string name;
    std::string profile = "default";
    std::vector<std::string> categories;
    double speed = 1.0;
    std::unordered_map<std::string, expr::EvalValue> attrs;
    std::string notes;
};

struct RecipePort {
    std::string dir = "in"; // "in" or "out"
    size_t index = 0;       // 0-based within in or out
    std::string slot_key;   // "in:<index>" or "out:<index>"

    std::optional<std::string> res;
    std::optional<std::string> resource_param;
    std::optional<std::string> resource_expr;

    double amount = 0.0;
    double chance = 1.0;
    double catalyst = 0.0;
    std::unordered_map<std::string, expr::EvalValue> attrs;

    BoolOrExpr enabled_override;
    std::optional<std::string> amount_expr_override;
    std::string notes;
};

struct SetupItem {
    std::string resource;
    double amount = 0.0;
};

struct RecipeOverrides {
    bool has_any = false;
    std::optional<std::string> craft_time;
    std::optional<std::string> input_amount;
    std::optional<std::string> output_amount;
    BoolOrExpr port_enabled;
    BoolOrExpr input_enabled;
    BoolOrExpr output_enabled;
    std::unordered_map<std::string, std::string> let;
    std::vector<RequireRule> requires_rules;
    std::vector<std::string> metric_order;
    std::unordered_map<std::string, ProfileMetricEntry> metrics;
    std::string notes;
};

struct CompiledRecipePort {
    bool has_resource_expr = false;
    expr::CompiledExpr compiled_resource_expr;
    BoolOrExpr compiled_enabled;
    bool has_amount_expr = false;
    expr::CompiledExpr compiled_amount_expr;
};

struct CompiledRecipeProfile {
    bool has_let_overrides = false;
    std::vector<std::string> let_evaluation_order;
    std::unordered_map<std::string, expr::CompiledExpr> compiled_lets;

    std::vector<RequireRule> extra_requires;

    std::optional<expr::CompiledExpr> craft_time_override;
    std::optional<expr::CompiledExpr> input_amount_override;
    std::optional<expr::CompiledExpr> output_amount_override;

    BoolOrExpr port_enabled_override;
    BoolOrExpr input_enabled_override;
    BoolOrExpr output_enabled_override;

    std::vector<std::string> metric_override_order;
    std::unordered_map<std::string, ProfileMetricEntry> metric_overrides;

    std::vector<CompiledRecipePort> in_ports;
    std::vector<CompiledRecipePort> out_ports;
};

struct Recipe {
    std::string id;
    std::string name;
    std::optional<std::string> category;
    std::vector<std::string> explicit_machines;

    // Resolved compatible machines & unique profile IDs
    std::vector<std::string> compatible_machine_ids;
    std::vector<std::string> compatible_profile_ids;

    double time_raw = 0.0;
    double time_seconds = 0.0;

    std::vector<RecipePort> in;
    std::vector<RecipePort> out;
    std::vector<SetupItem> setup;
    std::unordered_map<std::string, expr::EvalValue> attrs;
    RecipeOverrides overrides;

    // Precomputed static totals (§5.5)
    double input_items_total = 0.0;
    double input_fluids_total = 0.0;
    double output_items_total = 0.0;
    double output_fluids_total = 0.0;

    // Compiled overrides and port formulas per unique compatible profile
    std::unordered_map<std::string, CompiledRecipeProfile> compiled_by_profile;

    std::string notes;
};

// ============================================================================
// 5. LoadReport & GamePackage (§2.9, §9.1–§9.2)
// ============================================================================

enum class DiagnosticSeverity {
    Fatal,
    Error,
    Warning,
    Info
};

struct LoadDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string rule_id;      // e.g. "F-1".."F-5", "V-STRUCT-1", "V-EXPR-2", "V-W-2"
    std::string file;         // Relative path inside package (e.g. "game.json", "recipes/gt.json")
    std::string json_pointer; // e.g. "/recipes/88/in/0"
    size_t line = 0;          // 1-based line (0 if not applicable)
    size_t column = 0;        // 1-based column (0 if not applicable)
    std::string subject;      // Optional offending token (unknown id, bad slug, key)
    std::string entity;       // Optional canonical owner key ("kind:id", e.g. "profile:gt_single")
    std::string message;      // Core message
    std::string formatted;    // Full formatted diagnostic string (§5.7, §9.2)
};

struct LoadTimingMs {
    double total = 0.0;
    double parse = 0.0;
    double compile = 0.0;
};

struct LoadedFileInfo {
    std::string path;   // Relative path inside package ("game.json", "recipes/foo.json")
    uint64_t bytes = 0;
    size_t recipes = 0;
};

struct WarningWaiver {
    std::string rule;   // "V-W-1", "V-W-2", "V-W-3", "V-PAR-5"
    std::string match;  // glob pattern (* and ? wildcards)
    std::string reason; // optional human explanation
    std::string notes;  // optional notes
};

inline bool isWaivableWarningRule(std::string_view rule_id) {
    return rule_id == "V-W-1" || rule_id == "V-W-2" ||
           rule_id == "V-W-3" || rule_id == "V-PAR-5";
}

// Iterative O(|pattern| * |text|) glob matcher supporting '*' (any chars) and '?' (single char).
inline bool matchesWarningWaiverGlob(std::string_view pattern, std::string_view text) {
    size_t p = 0;
    size_t t = 0;
    size_t star_p = std::string_view::npos;
    size_t star_t = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star_p = p++;
            star_t = t;
        } else if (star_p != std::string_view::npos) {
            p = star_p + 1;
            t = ++star_t;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

inline std::string formatWaivedCountWithCommas(size_t value) {
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

inline std::string formatWaivedWarningsNote(const std::map<std::string, size_t>& waived) {
    size_t total = 0;
    size_t nonZeroRules = 0;
    for (const auto& [_, cnt] : waived) {
        if (cnt > 0) {
            total += cnt;
            ++nonZeroRules;
        }
    }
    if (total == 0) {
        return {};
    }
    std::string msg = formatWaivedCountWithCommas(total) +
                      (total == 1 ? " warning waived (" : " warnings waived (");
    bool first = true;
    for (const auto& [rule, cnt] : waived) {
        if (cnt == 0) continue;
        if (!first) msg += ", ";
        first = false;
        msg += rule;
        if (nonZeroRules > 1) {
            msg += ": ";
            msg += formatWaivedCountWithCommas(cnt);
        }
    }
    msg += ")";
    return msg;
}

struct LoadReport {
    bool fatal = false;
    bool degraded = false; // True when loaded via ignore_errors ("Load anyway") with errors
    std::string game_id;   // Parsed game.id from manifest when available (even if load fails later)
    std::vector<LoadDiagnostic> errors;
    std::vector<LoadDiagnostic> warnings;
    std::vector<LoadDiagnostic> notes; // Info-level entries (e.g. "no icons" mode); never affect ok() or --werror
    std::map<std::string, size_t> waived_warnings; // rule_id -> count of suppressed warnings
    std::unordered_map<std::string, std::vector<std::string>> dropped; // plural kind -> ordered [ids]
    std::unordered_map<std::string, std::string> drop_root;            // dropped "kind:id" -> root "kind:id" (singular kinds)
    LoadTimingMs timing_ms;
    std::vector<LoadedFileInfo> loaded_files;

    bool ok() const { return !fatal && errors.empty(); }

    size_t totalWaivedWarnings() const {
        size_t total = 0;
        for (const auto& [_, count] : waived_warnings) {
            total += count;
        }
        return total;
    }

    void recordWaivedWarning(std::string_view rule_id) {
        ++waived_warnings[std::string(rule_id)];
    }

    void syncWaivedWarningsNote() {
        const std::string msg = formatWaivedWarningsNote(waived_warnings);
        for (auto it = notes.begin(); it != notes.end(); ++it) {
            if (it->rule_id == "WAIVED") {
                if (msg.empty()) {
                    notes.erase(it);
                } else {
                    it->message = msg;
                    it->formatted = msg;
                }
                return;
            }
        }
        if (!msg.empty()) {
            addNote(msg, "WAIVED");
        }
    }

    void addNote(
        std::string message,
        std::string rule_id = {},
        std::string file = {},
        std::string json_pointer = {}) {
        LoadDiagnostic d;
        d.severity = DiagnosticSeverity::Info;
        d.rule_id = std::move(rule_id);
        d.file = std::move(file);
        d.json_pointer = std::move(json_pointer);
        d.message = std::move(message);
        d.formatted = d.message;
        notes.push_back(std::move(d));
    }

    bool hasRule(std::string_view rule_id) const {
        for (const auto& e : errors) {
            if (e.rule_id == rule_id) return true;
        }
        for (const auto& w : warnings) {
            if (w.rule_id == rule_id) return true;
        }
        return false;
    }

    size_t totalDroppedCount() const {
        size_t total = 0;
        for (const auto& [_, ids] : dropped) {
            total += ids.size();
        }
        return total;
    }
};

struct GamePackage {
    int schema_version = 4;
    GameMetadata game;
    UnitsDef units;

    std::filesystem::path package_root;
    std::filesystem::path manifest_path;
    std::filesystem::path icon_root;

    // Attribute declarations by entity kind: "machine", "recipe", "port", "resource"
    std::unordered_map<std::string, std::unordered_map<std::string, AttributeDecl>> attributes;

    // Contiguous entity storage + O(1) hash indices (§2.9 item 1)
    std::vector<ResourceKindDef> resource_kinds;
    std::unordered_map<std::string, size_t> resource_kind_index;

    std::vector<Resource> resources;
    std::unordered_map<std::string, size_t> resource_index;

    std::vector<OptionSet> option_sets;
    std::unordered_map<std::string, size_t> option_set_index;

    std::vector<TableDef> tables;
    std::unordered_map<std::string, size_t> table_index;

    std::vector<ParameterDef> parameters;
    std::unordered_map<std::string, size_t> parameter_index;

    std::vector<ParameterDef> globals;
    std::unordered_map<std::string, size_t> global_index;
    std::vector<std::string> global_eval_order;

    std::vector<MetricDef> metrics;
    std::unordered_map<std::string, size_t> metric_index;

    std::vector<Profile> profiles;
    std::unordered_map<std::string, size_t> profile_index;

    std::vector<Machine> machines;
    std::unordered_map<std::string, size_t> machine_index;

    std::vector<Recipe> recipes;
    std::unordered_map<std::string, size_t> recipe_index;

    // Pre-indexed category mappings (§2.9 item 2)
    std::unordered_map<std::string, std::vector<std::string>> category_to_machines;
    std::unordered_map<std::string, std::vector<std::string>> category_to_profiles;

    // Optional ID alias map (old_id -> new_id or prefixed rec@/mac@/res@)
    std::unordered_map<std::string, std::string> id_aliases;

    // Optional warning waivers loaded from manifest / waiver shards (§9.3)
    std::vector<WarningWaiver> warning_waivers;

    // Pre-built static evaluation environment shared across all nodes in the package
    expr::EvalEnv base_eval_env;

    bool isWarningWaived(std::string_view rule_id, std::string_view target) const {
        for (const auto& w : warning_waivers) {
            if (w.rule == rule_id && matchesWarningWaiverGlob(w.match, target)) {
                return true;
            }
        }
        return false;
    }

    // Entity lookup helpers
    const ResourceKindDef* findResourceKind(std::string_view id) const {
        auto it = resource_kind_index.find(std::string(id));
        return (it != resource_kind_index.end()) ? &resource_kinds[it->second] : nullptr;
    }

    const Resource* findResource(std::string_view id) const {
        auto it = resource_index.find(std::string(id));
        return (it != resource_index.end()) ? &resources[it->second] : nullptr;
    }

    const OptionSet* findOptionSet(std::string_view id) const {
        auto it = option_set_index.find(std::string(id));
        return (it != option_set_index.end()) ? &option_sets[it->second] : nullptr;
    }

    const TableDef* findTable(std::string_view id) const {
        auto it = table_index.find(std::string(id));
        return (it != table_index.end()) ? &tables[it->second] : nullptr;
    }

    const ParameterDef* findParameter(std::string_view id) const {
        auto it = parameter_index.find(std::string(id));
        return (it != parameter_index.end()) ? &parameters[it->second] : nullptr;
    }

    const ParameterDef* findGlobal(std::string_view id) const {
        auto it = global_index.find(std::string(id));
        return (it != global_index.end()) ? &globals[it->second] : nullptr;
    }

    const MetricDef* findMetric(std::string_view id) const {
        auto it = metric_index.find(std::string(id));
        return (it != metric_index.end()) ? &metrics[it->second] : nullptr;
    }

    const Profile* findProfile(std::string_view id) const {
        auto it = profile_index.find(std::string(id));
        return (it != profile_index.end()) ? &profiles[it->second] : nullptr;
    }

    const Machine* findMachine(std::string_view id) const {
        auto it = machine_index.find(std::string(id));
        return (it != machine_index.end()) ? &machines[it->second] : nullptr;
    }

    const Recipe* findRecipe(std::string_view id) const {
        auto it = recipe_index.find(std::string(id));
        return (it != recipe_index.end()) ? &recipes[it->second] : nullptr;
    }

    Resource* findResourceMut(std::string_view id) {
        auto it = resource_index.find(std::string(id));
        return (it != resource_index.end()) ? &resources[it->second] : nullptr;
    }

    Machine* findMachineMut(std::string_view id) {
        auto it = machine_index.find(std::string(id));
        return (it != machine_index.end()) ? &machines[it->second] : nullptr;
    }

    Recipe* findRecipeMut(std::string_view id) {
        auto it = recipe_index.find(std::string(id));
        return (it != recipe_index.end()) ? &recipes[it->second] : nullptr;
    }

    Profile* findProfileMut(std::string_view id) {
        auto it = profile_index.find(std::string(id));
        return (it != profile_index.end()) ? &profiles[it->second] : nullptr;
    }

    bool canMachineRunRecipe(std::string_view machine_id, std::string_view recipe_id) const {
        const Recipe* r = findRecipe(recipe_id);
        if (r == nullptr || findMachine(machine_id) == nullptr) {
            return false;
        }
        return std::find(r->compatible_machine_ids.begin(),
                         r->compatible_machine_ids.end(),
                         machine_id) != r->compatible_machine_ids.end();
    }

    std::string resolveAliasedId(std::string_view prefix, std::string_view raw_id) const {
        std::string current(raw_id);
        for (int depth = 0; depth < 16; ++depth) {
            std::string prefixed = std::string(prefix) + "@" + current;
            auto it = id_aliases.find(prefixed);
            if (it == id_aliases.end()) {
                it = id_aliases.find(current);
            }
            if (it == id_aliases.end() || it->second.empty() || it->second == current) {
                break;
            }
            current = it->second;
        }
        return current;
    }

    std::string resolveRecipeId(std::string_view id) const {
        if (findRecipe(id) != nullptr) return std::string(id);
        std::string resolved = resolveAliasedId("rec", id);
        return (findRecipe(resolved) != nullptr) ? resolved : std::string{};
    }

    std::string resolveMachineId(std::string_view id) const {
        if (findMachine(id) != nullptr) return std::string(id);
        std::string resolved = resolveAliasedId("mac", id);
        return (findMachine(resolved) != nullptr) ? resolved : std::string{};
    }

    std::string resolveResourceId(std::string_view id) const {
        if (findResource(id) != nullptr) return std::string(id);
        std::string resolved = resolveAliasedId("res", id);
        return (findResource(resolved) != nullptr) ? resolved : std::string{};
    }

    double toDisplayRate(double canonical_rate_per_s, RateUnit unit) const {
        return fromCanonicalRate(canonical_rate_per_s, unit, units.ticks_per_second);
    }

    double fromDisplayRate(double display_rate, RateUnit unit) const {
        return toCanonicalRate(display_rate, unit, units.ticks_per_second);
    }

    const ResourceKindDef* findResourceKindForResource(std::string_view resource_id) const {
        const Resource* res = findResource(resource_id);
        return (res != nullptr) ? findResourceKind(res->kind) : nullptr;
    }

    double resourceDisplayScale(std::string_view resource_id) const {
        const ResourceKindDef* kind = findResourceKindForResource(resource_id);
        if (kind != nullptr && std::isfinite(kind->display_scale) && kind->display_scale > 0.0) {
            return kind->display_scale;
        }
        return 1.0;
    }

    std::string resourceDisplayUnit(std::string_view resource_id) const {
        const ResourceKindDef* kind = findResourceKindForResource(resource_id);
        return (kind != nullptr) ? kind->display_unit : std::string{};
    }
};

using ParamDef = ParameterDef;
using ParamType = ParameterType;
using MachineProfile = Profile;

// ============================================================================
// 6. NodeEval & Post-Solve Metric Structures (§4.5, §6, §7.1, §7.6)
// ============================================================================

enum class EvalStatus {
    OK,
    WARNING,
    BLOCKED,
    ERROR
};

inline std::string evalStatusToString(EvalStatus s) {
    switch (s) {
        case EvalStatus::OK:
            return "OK";
        case EvalStatus::WARNING:
            return "WARNING";
        case EvalStatus::BLOCKED:
            return "BLOCKED";
        case EvalStatus::ERROR:
            return "ERROR";
    }
    return "OK";
}

struct EvalMessage {
    EvalStatus severity = EvalStatus::WARNING;
    std::string text;
    std::string source; // e.g. "profiles/gt_single/let/oc" or "parameters/clock"
};

struct EvaluatedPort {
    std::string slot_key;     // "in:0", "out:0", "xin:water", "xout:ash"
    uint64_t port_id = 0;     // Populated when bound to a graph Node
    std::string dir = "in";   // "in" or "out"
    std::string resource;     // Resolved resource ID ("" if unset)
    bool active = false;
    double amount_per_craft = 0.0;
};

struct EvaluatedMetricCoeff {
    std::string metric_id;
    bool enabled = true;
    MetricFormKind form = MetricFormKind::PerMachine;
    double coeff = 0.0; // Pre-solve coefficient for PerMachine / PerCraft; 0.0 for Value
};

struct EvaluatedParamUi {
    bool visible = true;
    bool has_min = false;
    bool has_max = false;
    double min_val = 0.0;
    double max_val = 0.0;
    size_t slot_count = 0;
    std::vector<std::string> allowed_options;
    std::vector<std::string> allowed_resources;
};

struct NodeEval {
    EvalStatus status = EvalStatus::OK;
    std::vector<EvalMessage> messages;
    double craft_time_s = 0.0;
    std::vector<EvaluatedPort> ports;
    std::vector<EvaluatedMetricCoeff> metric_coeffs;

    // Resolved parameter values and let values from pre-solve Steps 1–2
    std::unordered_map<std::string, ParamValue> resolved_params;
    std::unordered_map<std::string, expr::EvalValue> resolved_param_eval_values;
    std::unordered_map<std::string, bool> param_visible;
    std::unordered_map<std::string, EvaluatedParamUi> param_ui;
    std::unordered_map<std::string, expr::EvalValue> resolved_lets;

    const EvaluatedPort* findPort(std::string_view slot_key) const {
        for (const auto& p : ports) {
            if (p.slot_key == slot_key) return &p;
        }
        return nullptr;
    }

    const EvaluatedMetricCoeff* findMetric(std::string_view metric_id) const {
        for (const auto& m : metric_coeffs) {
            if (m.metric_id == metric_id) return &m;
        }
        return nullptr;
    }
};

struct ResolvedGlobals {
    std::unordered_map<std::string, ParamValue> resolved_values;
    std::unordered_map<std::string, expr::EvalValue> eval_values;
    std::unordered_map<std::string, bool> visible;
    std::unordered_map<std::string, EvaluatedParamUi> param_ui;
    std::unordered_map<std::string, EvalStatus> status_by_global;
    std::unordered_map<std::string, std::vector<EvalMessage>> messages_by_global;
};

struct EvaluatedNodeMetric {
    std::string metric_id;
    bool enabled = false;
    EvalStatus status = EvalStatus::OK; // ERROR if a post-solve 'value' metric failed at runtime (§7.6)
    double value = 0.0;
    std::string error_message;
};

struct NodeMetricsResult {
    EvalStatus node_status = EvalStatus::OK;
    std::vector<EvaluatedNodeMetric> metrics;
    std::vector<EvalMessage> messages; // Post-solve metric runtime errors (§7.6)

    const EvaluatedNodeMetric* findMetric(std::string_view metric_id) const {
        for (const auto& m : metrics) {
            if (m.metric_id == metric_id) return &m;
        }
        return nullptr;
    }
};

struct MetricTotal {
    std::string metric_id;
    MetricAggregate aggregate = MetricAggregate::Sum;
    bool split_sign = false;
    bool has_value = false;     // False when aggregate is min/max and 0 nodes contributed (§6.2)
    double value = 0.0;         // Sum (net), Min, or Max
    double positive_sum = 0.0;  // Populated when split_sign == true (consumption >= 0)
    double negative_sum = 0.0;  // Populated when split_sign == true (generation <= 0)
    double net_sum = 0.0;       // positive_sum + negative_sum
    size_t contributing_nodes = 0;
};

} // namespace gamedata

#endif // GAMEDATAV4_H
