#ifndef EXPRENV_H
#define EXPRENV_H

#include "core/expr/ExprDiagnostic.h"
#include "core/expr/ExprValue.h"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace expr {

// Compile-time contexts per §5.4 and §4.5
enum class ExprContext {
    // min, max, count, visible_if, allowed_if on a global (§3.2, §5.4):
    // allows game.*, global.*, and option (if allow_option is true).
    GlobalStatic,

    // min, max, count, visible_if, allowed_if on a node parameter (§3.1, §5.4):
    // allows game.*, global.*, machine.*, recipe.*, earlier param.* in profile.params,
    // option (if allow_option is true), and port.* excluding port.resource (if allow_port_in_static is true).
    ParamStatic,

    // let and requires expressions (§4.1, §4.5 steps 2-3, §5.4):
    // allows game.*, global.*, machine.*, recipe.*, profile param.*, let variables.
    PreSolveLet,

    // craft_time expression (§4.1, §4.5 step 4, §5.4):
    // same scope as PreSolveLet (craft_time itself is not yet in scope).
    PreSolveCraftTime,

    // extra_ports (enabled, amount, resource_expr) and linear/enabled metric formulas
    // (enabled, per_machine, per_craft) (§4.3, §4.5 steps 6-7, §5.4, §6.2):
    // allows PreSolveLet scope + craft_time.
    PreSolveAfterCraftTime,

    // recipe port resource_expr (§2.8, §4.5 step 5, §5.4, §5.5):
    // allows PreSolveAfterCraftTime + port.* excluding port.resource
    // (port.base_resource allowed iff port_has_base_resource is true).
    RecipePortResourceExpr,

    // profile input_amount, output_amount, port_enabled, input_enabled, output_enabled,
    // and recipe port enabled, amount_expr (§4.1, §4.4, §4.5 step 5, §5.4, §5.5):
    // allows PreSolveAfterCraftTime + all port.* including port.resource.
    RecipePortFormula,

    // post-solve metric value formula (§5.4, §6.2, §7.6):
    // allows PreSolveAfterCraftTime + node.machines, node.crafts (no port.*).
    PostSolveMetricValue
};

enum class AttrType {
    Number,
    Bool,
    String
};

inline ExprType attrTypeToExprType(AttrType t) {
    switch (t) {
        case AttrType::Number:
            return ExprType::number();
        case AttrType::Bool:
            return ExprType::boolean();
        case AttrType::String:
            return ExprType::string();
    }
    return ExprType::number();
}

enum class TableKind {
    Array,
    Object
};

struct OptionSetCompileInfo {
    std::vector<std::string> option_keys;
    std::unordered_map<std::string, AttrType> attrs;

    bool hasKey(std::string_view key) const {
        for (const auto& k : option_keys) {
            if (k == key) {
                return true;
            }
        }
        return false;
    }
};

struct ParamCompileInfo {
    ExprType type;
    bool allow_none = false;
};

struct CompileEnv {
    ExprContext context = ExprContext::PreSolveLet;

    // Declared attributes per entity kind (§2.5)
    std::unordered_map<std::string, AttrType> game_attrs;
    std::unordered_map<std::string, AttrType> machine_attrs;
    std::unordered_map<std::string, AttrType> recipe_attrs;
    std::unordered_map<std::string, AttrType> port_attrs;
    std::unordered_map<std::string, AttrType> resource_attrs;

    // Option sets, tables, and known resource IDs
    std::unordered_map<std::string, OptionSetCompileInfo> option_sets;
    std::unordered_map<std::string, TableKind> tables;
    std::unordered_set<std::string> resources;

    // Declared parameters and globals (§3.1, §3.2)
    std::unordered_map<std::string, ParamCompileInfo> parameters;
    std::unordered_map<std::string, ParamCompileInfo> globals;

    // Profile parameter scoping (§3.1, §4.1, §5.4, V-EXPR-5):
    // When check_profile_params is true, param.x must appear in profile_params
    // (and if max_param_index is set, at index < *max_param_index).
    // When check_profile_params is false (standalone parameter check before profile binding),
    // param.x only needs to exist in parameters (and not self-reference current_param_id).
    bool check_profile_params = true;
    std::vector<std::string> profile_params;
    std::optional<size_t> max_param_index;
    std::string current_param_id;
    std::string current_global_id;
    std::string profile_id;

    // Known let variables and their resolved types in the current profile
    std::unordered_map<std::string, ExprType> let_types;

    // Context-specific bindings
    bool allow_option = false;            // True inside allowed_if
    ExprType option_type;                 // Option<S> (for enum/slots) or Resource (for resource_choice)
    bool allow_port_in_static = false;    // True inside visible_if / allowed_if of a resource_choice param
    bool port_has_base_resource = true;   // True when the recipe port declares static "res"

    // Optional per-load budget for Levenshtein "did you mean" suggestions (capped at 25)
    SuggestionBudget* suggestion_budget = nullptr;
};

struct MachineEvalInfo {
    std::string id;
    double speed = 1.0;
    std::unordered_map<std::string, EvalValue> attrs;
};

struct RecipeEvalInfo {
    std::string id;
    double time = 1.0;
    double time_raw = 1.0;
    double input_items_total = 0.0;
    double input_fluids_total = 0.0;
    double output_items_total = 0.0;
    double output_fluids_total = 0.0;
    std::unordered_map<std::string, EvalValue> attrs;
};

struct PortEvalInfo {
    std::string dir = "in";
    double amount = 0.0;
    double chance = 1.0;
    double catalyst = 0.0;
    double index = 0.0;
    std::optional<std::string> base_resource;
    std::optional<std::string> resource;
    std::unordered_map<std::string, EvalValue> attrs;
};

struct NodeEvalInfo {
    double machines = 0.0;
    double crafts = 0.0;
};

struct ResourceEvalData {
    std::unordered_set<std::string> tags;
    std::unordered_map<std::string, EvalValue> attrs;
};

struct OptionSetEvalData {
    std::unordered_map<std::string, EvalValue> attr_defaults;
    std::unordered_map<std::string, std::unordered_map<std::string, EvalValue>> option_attrs;
};

struct EvalEnv {
    double game_ticks_per_second = 20.0;
    std::unordered_map<std::string, EvalValue> game_attrs;

    // Attribute defaults (§2.5) used when an entity omits an attribute override
    std::unordered_map<std::string, EvalValue> machine_attr_defaults;
    std::unordered_map<std::string, EvalValue> recipe_attr_defaults;
    std::unordered_map<std::string, EvalValue> port_attr_defaults;
    std::unordered_map<std::string, EvalValue> resource_attr_defaults;

    MachineEvalInfo machine;
    RecipeEvalInfo recipe;
    PortEvalInfo port;
    NodeEvalInfo node;

    std::unordered_map<std::string, EvalValue> globals;
    std::unordered_map<std::string, EvalValue> params;
    std::unordered_map<std::string, EvalValue> lets;

    double craft_time = 1.0;
    std::optional<EvalValue> option_value; // Bound during allowed_if evaluation

    std::unordered_map<std::string, ResourceEvalData> resources;
    std::unordered_map<std::string, OptionSetEvalData> option_sets;

    std::unordered_map<std::string, std::vector<double>> array_tables;
    std::unordered_map<std::string, std::unordered_map<std::string, double>> object_tables;
};

} // namespace expr

#endif // EXPRENV_H
