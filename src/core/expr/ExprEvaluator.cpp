#include "core/expr/ExprEvaluator.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace expr {

namespace {

EvalResult checkedNumber(double v, const char* context_msg = "non-finite numeric result") {
    if (!std::isfinite(v)) {
        return EvalResult::failure(context_msg);
    }
    return EvalResult::success(EvalValue::number(normalizeZero(v)));
}

EvalValue defaultValueForType(const ExprType& type) {
    switch (type.kind) {
        case ValueKind::Number:
            return EvalValue::number(0.0);
        case ValueKind::Bool:
            return EvalValue::boolean(false);
        case ValueKind::String:
            return EvalValue::string("");
        case ValueKind::Option:
            return EvalValue::option(type.option_set, std::nullopt);
        case ValueKind::Resource:
            return EvalValue::resource(std::nullopt);
        case ValueKind::Slots:
            return EvalValue::slots(type.option_set, {});
        case ValueKind::NumberList:
            return EvalValue::numberList({});
    }
    return EvalValue::number(0.0);
}

EvalValue lookupAttrWithDefault(
    const std::unordered_map<std::string, EvalValue>& attrs,
    const std::unordered_map<std::string, EvalValue>& defaults,
    const std::string& key,
    const ExprType& fallback_type) {
    auto it = attrs.find(key);
    if (it != attrs.end()) {
        if (it->second.isNumber()) {
            return EvalValue::number(it->second.asNumber());
        }
        return it->second;
    }
    auto def_it = defaults.find(key);
    if (def_it != defaults.end()) {
        if (def_it->second.isNumber()) {
            return EvalValue::number(def_it->second.asNumber());
        }
        return def_it->second;
    }
    return defaultValueForType(fallback_type);
}

EvalValue lookupOptionAttr(
    const EvalEnv& env,
    const std::string& option_set_id,
    const std::string& option_key,
    const std::string& attr_name,
    const ExprType& fallback_type) {
    auto set_it = env.option_sets.find(option_set_id);
    if (set_it != env.option_sets.end()) {
        auto opt_it = set_it->second.option_attrs.find(option_key);
        if (opt_it != set_it->second.option_attrs.end()) {
            auto attr_it = opt_it->second.find(attr_name);
            if (attr_it != opt_it->second.end()) {
                if (attr_it->second.isNumber()) {
                    return EvalValue::number(attr_it->second.asNumber());
                }
                return attr_it->second;
            }
        }
        auto def_it = set_it->second.attr_defaults.find(attr_name);
        if (def_it != set_it->second.attr_defaults.end()) {
            if (def_it->second.isNumber()) {
                return EvalValue::number(def_it->second.asNumber());
            }
            return def_it->second;
        }
    }
    return defaultValueForType(fallback_type);
}

std::string formatNumberForError(double v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

EvalResult evalNode(const AstNode& node, const EvalEnv& env);

EvalResult evalAccess(const AstNode& node, const EvalEnv& env) {
    switch (node.access_kind) {
        case ResolvedAccessKind::LetVar: {
            auto it = env.lets.find(node.resolved_key);
            if (it == env.lets.end()) {
                return EvalResult::failure("unbound let variable '" + node.resolved_key + "'");
            }
            if (it->second.isNumber()) {
                return checkedNumber(it->second.asNumber(), "non-finite let variable");
            }
            return EvalResult::success(it->second);
        }
        case ResolvedAccessKind::CraftTime:
            return checkedNumber(env.craft_time, "non-finite craft_time");
        case ResolvedAccessKind::OptionCandidate: {
            if (!env.option_value.has_value()) {
                return EvalResult::failure("'option' is not bound in current evaluation environment");
            }
            return EvalResult::success(*env.option_value);
        }
        case ResolvedAccessKind::GameTicksPerSecond:
            return checkedNumber(env.game_ticks_per_second, "non-finite game.ticks_per_second");
        case ResolvedAccessKind::GameAttr: {
            auto it = env.game_attrs.find(node.resolved_key);
            if (it != env.game_attrs.end()) {
                if (it->second.isNumber()) {
                    return checkedNumber(it->second.asNumber(), "non-finite game attribute");
                }
                return EvalResult::success(it->second);
            }
            return EvalResult::success(defaultValueForType(node.resolved_type));
        }
        case ResolvedAccessKind::GlobalAccess: {
            auto it = env.globals.find(node.resolved_key);
            if (it == env.globals.end()) {
                return EvalResult::failure("unbound global 'global." + node.resolved_key + "'");
            }
            if (it->second.isNumber()) {
                return checkedNumber(it->second.asNumber(), "non-finite global value");
            }
            return EvalResult::success(it->second);
        }
        case ResolvedAccessKind::MachineId:
            return EvalResult::success(EvalValue::string(env.machine.id));
        case ResolvedAccessKind::MachineSpeed:
            return checkedNumber(env.machine.speed, "non-finite machine.speed");
        case ResolvedAccessKind::MachineAttr: {
            EvalValue val = lookupAttrWithDefault(
                env.machine.attrs, env.machine_attr_defaults, node.resolved_key, node.resolved_type);
            if (val.isNumber()) {
                return checkedNumber(val.asNumber(), "non-finite machine attribute");
            }
            return EvalResult::success(std::move(val));
        }
        case ResolvedAccessKind::RecipeId:
            return EvalResult::success(EvalValue::string(env.recipe.id));
        case ResolvedAccessKind::RecipeTime:
            return checkedNumber(env.recipe.time, "non-finite recipe.time");
        case ResolvedAccessKind::RecipeTimeRaw:
            return checkedNumber(env.recipe.time_raw, "non-finite recipe.time_raw");
        case ResolvedAccessKind::RecipeInputItemsTotal:
            return checkedNumber(env.recipe.input_items_total, "non-finite recipe.input_items_total");
        case ResolvedAccessKind::RecipeInputFluidsTotal:
            return checkedNumber(env.recipe.input_fluids_total, "non-finite recipe.input_fluids_total");
        case ResolvedAccessKind::RecipeOutputItemsTotal:
            return checkedNumber(env.recipe.output_items_total, "non-finite recipe.output_items_total");
        case ResolvedAccessKind::RecipeOutputFluidsTotal:
            return checkedNumber(env.recipe.output_fluids_total, "non-finite recipe.output_fluids_total");
        case ResolvedAccessKind::RecipeAttr: {
            EvalValue val = lookupAttrWithDefault(
                env.recipe.attrs, env.recipe_attr_defaults, node.resolved_key, node.resolved_type);
            if (val.isNumber()) {
                return checkedNumber(val.asNumber(), "non-finite recipe attribute");
            }
            return EvalResult::success(std::move(val));
        }
        case ResolvedAccessKind::ParamAccess: {
            auto it = env.params.find(node.resolved_key);
            if (it == env.params.end()) {
                return EvalResult::failure("unbound parameter 'param." + node.resolved_key + "'");
            }
            if (it->second.isNumber()) {
                return checkedNumber(it->second.asNumber(), "non-finite parameter value");
            }
            return EvalResult::success(it->second);
        }
        case ResolvedAccessKind::PortDir:
            return EvalResult::success(EvalValue::string(env.port.dir));
        case ResolvedAccessKind::PortAmount:
            return checkedNumber(env.port.amount, "non-finite port.amount");
        case ResolvedAccessKind::PortChance:
            return checkedNumber(env.port.chance, "non-finite port.chance");
        case ResolvedAccessKind::PortCatalyst:
            return checkedNumber(env.port.catalyst, "non-finite port.catalyst");
        case ResolvedAccessKind::PortIndex:
            return checkedNumber(env.port.index, "non-finite port.index");
        case ResolvedAccessKind::PortBaseResource:
            return EvalResult::success(EvalValue::resource(env.port.base_resource));
        case ResolvedAccessKind::PortResource:
            return EvalResult::success(EvalValue::resource(env.port.resource));
        case ResolvedAccessKind::PortAttr: {
            EvalValue val = lookupAttrWithDefault(
                env.port.attrs, env.port_attr_defaults, node.resolved_key, node.resolved_type);
            if (val.isNumber()) {
                return checkedNumber(val.asNumber(), "non-finite port attribute");
            }
            return EvalResult::success(std::move(val));
        }
        case ResolvedAccessKind::NodeMachines:
            return checkedNumber(env.node.machines, "non-finite node.machines");
        case ResolvedAccessKind::NodeCrafts:
            return checkedNumber(env.node.crafts, "non-finite node.crafts");

        // Value-level postfix member accesses:
        case ResolvedAccessKind::OptionKey: {
            EvalResult obj_res = evalNode(*node.left, env);
            if (!obj_res.ok) {
                return obj_res;
            }
            const OptionValue& opt = obj_res.value.asOption();
            if (!opt.key.has_value()) {
                return EvalResult::failure("cannot access '.key' on unset (null) option");
            }
            return EvalResult::success(EvalValue::string(*opt.key));
        }
        case ResolvedAccessKind::OptionAttr: {
            EvalResult obj_res = evalNode(*node.left, env);
            if (!obj_res.ok) {
                return obj_res;
            }
            const OptionValue& opt = obj_res.value.asOption();
            if (!opt.key.has_value()) {
                return EvalResult::failure(
                    "cannot access '." + node.resolved_key + "' on unset (null) option");
            }
            EvalValue val = lookupOptionAttr(
                env, opt.option_set, *opt.key, node.resolved_key, node.resolved_type);
            if (val.isNumber()) {
                return checkedNumber(val.asNumber(), "non-finite option attribute");
            }
            return EvalResult::success(std::move(val));
        }
        case ResolvedAccessKind::ResourceId: {
            EvalResult obj_res = evalNode(*node.left, env);
            if (!obj_res.ok) {
                return obj_res;
            }
            const ResourceValue& rv = obj_res.value.asResource();
            if (!rv.id.has_value()) {
                return EvalResult::failure("cannot access '.id' on unset (null) resource");
            }
            return EvalResult::success(EvalValue::string(*rv.id));
        }
        case ResolvedAccessKind::ResourceAttr: {
            EvalResult obj_res = evalNode(*node.left, env);
            if (!obj_res.ok) {
                return obj_res;
            }
            const ResourceValue& rv = obj_res.value.asResource();
            if (!rv.id.has_value()) {
                return EvalResult::failure(
                    "cannot access '." + node.resolved_key + "' on unset (null) resource");
            }
            auto res_it = env.resources.find(*rv.id);
            const std::unordered_map<std::string, EvalValue>* res_attrs = nullptr;
            std::unordered_map<std::string, EvalValue> empty_attrs;
            if (res_it != env.resources.end()) {
                res_attrs = &res_it->second.attrs;
            } else {
                res_attrs = &empty_attrs;
            }
            EvalValue val = lookupAttrWithDefault(
                *res_attrs, env.resource_attr_defaults, node.resolved_key, node.resolved_type);
            if (val.isNumber()) {
                return checkedNumber(val.asNumber(), "non-finite resource attribute");
            }
            return EvalResult::success(std::move(val));
        }
        case ResolvedAccessKind::SlotsAttr: {
            EvalResult obj_res = evalNode(*node.left, env);
            if (!obj_res.ok) {
                return obj_res;
            }
            const SlotsValue& sv = obj_res.value.asSlots();
            std::vector<double> values;
            values.reserve(sv.active_slots.size());
            for (const auto& slot_key : sv.active_slots) {
                if (!slot_key.has_value()) {
                    continue;
                }
                EvalValue val = lookupOptionAttr(
                    env, sv.option_set, *slot_key, node.resolved_key, ExprType::number());
                double num = val.isNumber() ? val.asNumber() : 0.0;
                if (!std::isfinite(num)) {
                    return EvalResult::failure("non-finite slot attribute '" + node.resolved_key + "'");
                }
                values.push_back(normalizeZero(num));
            }
            return EvalResult::success(EvalValue::numberList(std::move(values)));
        }
        case ResolvedAccessKind::None:
            break;
    }
    return EvalResult::failure("unresolved identifier or member access");
}

bool evalEquality(const EvalValue& lhs, const EvalValue& rhs) {
    if (lhs.isNumber() && rhs.isNumber()) {
        return lhs.asNumber() == rhs.asNumber();
    }
    if (lhs.isBool() && rhs.isBool()) {
        return lhs.asBool() == rhs.asBool();
    }
    if (lhs.isString() && rhs.isString()) {
        return lhs.asString() == rhs.asString();
    }
    if (lhs.isOption() && rhs.isOption()) {
        return lhs.asOption().key == rhs.asOption().key;
    }
    if (lhs.isOption() && rhs.isString()) {
        const auto& k = lhs.asOption().key;
        return k.has_value() && *k == rhs.asString();
    }
    if (lhs.isString() && rhs.isOption()) {
        const auto& k = rhs.asOption().key;
        return k.has_value() && lhs.asString() == *k;
    }
    if (lhs.isResource() && rhs.isResource()) {
        return lhs.asResource().id == rhs.asResource().id;
    }
    if (lhs.isResource() && rhs.isString()) {
        const auto& id = lhs.asResource().id;
        return id.has_value() && *id == rhs.asString();
    }
    if (lhs.isString() && rhs.isResource()) {
        const auto& id = rhs.asResource().id;
        return id.has_value() && lhs.asString() == *id;
    }
    return false;
}

EvalResult evalCall(const AstNode& node, const EvalEnv& env) {
    switch (node.builtin_func) {
        case BuiltinFunc::If: {
            EvalResult cond_res = evalNode(*node.args[0], env);
            if (!cond_res.ok) {
                return cond_res;
            }
            if (cond_res.value.asBool()) {
                return evalNode(*node.args[1], env);
            }
            return evalNode(*node.args[2], env);
        }
        case BuiltinFunc::MinScalar:
        case BuiltinFunc::MaxScalar: {
            const bool is_min = (node.builtin_func == BuiltinFunc::MinScalar);
            EvalResult first = evalNode(*node.args[0], env);
            if (!first.ok) {
                return first;
            }
            double best = first.value.asNumber();
            for (size_t i = 1; i < node.args.size(); ++i) {
                EvalResult cur = evalNode(*node.args[i], env);
                if (!cur.ok) {
                    return cur;
                }
                double v = cur.value.asNumber();
                if (is_min ? (v < best) : (v > best)) {
                    best = v;
                }
            }
            return checkedNumber(best);
        }
        case BuiltinFunc::MinList:
        case BuiltinFunc::MaxList: {
            const bool is_min = (node.builtin_func == BuiltinFunc::MinList);
            EvalResult list_res = evalNode(*node.args[0], env);
            if (!list_res.ok) {
                return list_res;
            }
            double def_val = 0.0;
            if (node.args.size() == 2) {
                EvalResult def_res = evalNode(*node.args[1], env);
                if (!def_res.ok) {
                    return def_res;
                }
                def_val = def_res.value.asNumber();
            }
            const auto& vals = list_res.value.asNumberList().values;
            if (vals.empty()) {
                return checkedNumber(def_val);
            }
            double best = vals[0];
            for (size_t i = 1; i < vals.size(); ++i) {
                if (is_min ? (vals[i] < best) : (vals[i] > best)) {
                    best = vals[i];
                }
            }
            return checkedNumber(best);
        }
        case BuiltinFunc::Clamp: {
            EvalResult x_res = evalNode(*node.args[0], env);
            if (!x_res.ok) return x_res;
            EvalResult lo_res = evalNode(*node.args[1], env);
            if (!lo_res.ok) return lo_res;
            EvalResult hi_res = evalNode(*node.args[2], env);
            if (!hi_res.ok) return hi_res;

            double x = x_res.value.asNumber();
            double lo = lo_res.value.asNumber();
            double hi = hi_res.value.asNumber();
            if (lo > hi) {
                return EvalResult::failure(
                    "clamp(" + formatNumberForError(x) + ", " +
                    formatNumberForError(lo) + ", " +
                    formatNumberForError(hi) + "): lower bound exceeds upper bound");
            }
            return checkedNumber(std::clamp(x, lo, hi));
        }
        case BuiltinFunc::Floor: {
            EvalResult r = evalNode(*node.args[0], env);
            if (!r.ok) return r;
            return checkedNumber(std::floor(r.value.asNumber()));
        }
        case BuiltinFunc::Ceil: {
            EvalResult r = evalNode(*node.args[0], env);
            if (!r.ok) return r;
            return checkedNumber(std::ceil(r.value.asNumber()));
        }
        case BuiltinFunc::Round: {
            EvalResult r = evalNode(*node.args[0], env);
            if (!r.ok) return r;
            return checkedNumber(std::round(r.value.asNumber()));
        }
        case BuiltinFunc::Abs: {
            EvalResult r = evalNode(*node.args[0], env);
            if (!r.ok) return r;
            return checkedNumber(std::abs(r.value.asNumber()));
        }
        case BuiltinFunc::Sqrt: {
            EvalResult r = evalNode(*node.args[0], env);
            if (!r.ok) return r;
            double x = r.value.asNumber();
            if (x < 0.0) {
                return EvalResult::failure(
                    "sqrt(" + formatNumberForError(x) + "): argument must be >= 0");
            }
            return checkedNumber(std::sqrt(x), "sqrt() produced non-finite result");
        }
        case BuiltinFunc::Pow: {
            EvalResult a_res = evalNode(*node.args[0], env);
            if (!a_res.ok) return a_res;
            EvalResult b_res = evalNode(*node.args[1], env);
            if (!b_res.ok) return b_res;
            double res = std::pow(a_res.value.asNumber(), b_res.value.asNumber());
            return checkedNumber(res, "pow() produced non-finite result");
        }
        case BuiltinFunc::Exp: {
            EvalResult r = evalNode(*node.args[0], env);
            if (!r.ok) return r;
            double res = std::exp(r.value.asNumber());
            return checkedNumber(res, "exp() produced non-finite result");
        }
        case BuiltinFunc::Ln: {
            EvalResult r = evalNode(*node.args[0], env);
            if (!r.ok) return r;
            double x = r.value.asNumber();
            if (x <= 0.0) {
                return EvalResult::failure(
                    "ln(" + formatNumberForError(x) + "): argument must be > 0");
            }
            return checkedNumber(std::log(x), "ln() produced non-finite result");
        }
        case BuiltinFunc::Log: {
            EvalResult x_res = evalNode(*node.args[0], env);
            if (!x_res.ok) return x_res;
            EvalResult base_res = evalNode(*node.args[1], env);
            if (!base_res.ok) return base_res;
            double x = x_res.value.asNumber();
            double base = base_res.value.asNumber();
            if (x <= 0.0) {
                return EvalResult::failure(
                    "log(" + formatNumberForError(x) + ", " +
                    formatNumberForError(base) + "): argument must be > 0");
            }
            if (base <= 0.0 || base == 1.0) {
                return EvalResult::failure(
                    "log(" + formatNumberForError(x) + ", " +
                    formatNumberForError(base) + "): base must be > 0 and != 1");
            }
            return checkedNumber(std::log(x) / std::log(base), "log() produced non-finite result");
        }
        case BuiltinFunc::LookupArray: {
            EvalResult key_res = evalNode(*node.args[1], env);
            if (!key_res.ok) return key_res;
            double key = key_res.value.asNumber();
            if (!std::isfinite(key) || std::abs(key - std::round(key)) > 1e-9) {
                return EvalResult::failure(
                    "lookup(\"" + node.resolved_key + "\", " +
                    formatNumberForError(key) + "): index must be an integer");
            }
            long long idx = std::llround(key);
            auto tbl_it = env.array_tables.find(node.resolved_key);
            if (tbl_it != env.array_tables.end() &&
                idx >= 0 &&
                static_cast<size_t>(idx) < tbl_it->second.size()) {
                return checkedNumber(
                    tbl_it->second[static_cast<size_t>(idx)],
                    "lookup() returned non-finite table value");
            }
            if (node.args.size() == 3) {
                return evalNode(*node.args[2], env);
            }
            return EvalResult::failure(
                "lookup(\"" + node.resolved_key + "\", " +
                std::to_string(idx) + "): index out of bounds");
        }
        case BuiltinFunc::LookupObject: {
            EvalResult key_res = evalNode(*node.args[1], env);
            if (!key_res.ok) return key_res;

            std::string key_str;
            if (key_res.value.isString()) {
                key_str = key_res.value.asString();
            } else if (key_res.value.isOption()) {
                const auto& opt = key_res.value.asOption();
                if (!opt.key.has_value()) {
                    return EvalResult::failure(
                        "lookup(\"" + node.resolved_key + "\", ...): option argument is unset (null)");
                }
                key_str = *opt.key;
            } else if (key_res.value.isResource()) {
                const auto& rv = key_res.value.asResource();
                if (!rv.id.has_value()) {
                    return EvalResult::failure(
                        "lookup(\"" + node.resolved_key + "\", ...): resource argument is unset (null)");
                }
                key_str = *rv.id;
            }

            auto tbl_it = env.object_tables.find(node.resolved_key);
            if (tbl_it != env.object_tables.end()) {
                auto val_it = tbl_it->second.find(key_str);
                if (val_it != tbl_it->second.end()) {
                    return checkedNumber(
                        val_it->second, "lookup() returned non-finite table value");
                }
            }
            if (node.args.size() == 3) {
                return evalNode(*node.args[2], env);
            }
            return EvalResult::failure(
                "lookup(\"" + node.resolved_key + "\", \"" + key_str + "\"): key not found");
        }
        case BuiltinFunc::HasTag: {
            EvalResult res_arg = evalNode(*node.args[0], env);
            if (!res_arg.ok) return res_arg;
            EvalResult tag_arg = evalNode(*node.args[1], env);
            if (!tag_arg.ok) return tag_arg;

            const auto& rv = res_arg.value.asResource();
            if (!rv.id.has_value()) {
                return EvalResult::failure("has_tag(): resource argument is unset (null)");
            }
            const std::string& tag = tag_arg.value.asString();
            auto res_it = env.resources.find(*rv.id);
            bool found = (res_it != env.resources.end()) &&
                         (res_it->second.tags.find(tag) != res_it->second.tags.end());
            return EvalResult::success(EvalValue::boolean(found));
        }
        case BuiltinFunc::Sum: {
            EvalResult list_res = evalNode(*node.args[0], env);
            if (!list_res.ok) return list_res;
            double sum = 0.0;
            for (double v : list_res.value.asNumberList().values) {
                sum += v;
            }
            return checkedNumber(sum, "sum() produced non-finite result");
        }
        case BuiltinFunc::CountAll: {
            EvalResult slots_res = evalNode(*node.args[0], env);
            if (!slots_res.ok) return slots_res;
            size_t count = 0;
            for (const auto& s : slots_res.value.asSlots().active_slots) {
                if (s.has_value()) {
                    ++count;
                }
            }
            return EvalResult::success(EvalValue::number(static_cast<double>(count)));
        }
        case BuiltinFunc::CountKey: {
            EvalResult slots_res = evalNode(*node.args[0], env);
            if (!slots_res.ok) return slots_res;
            const std::string& target_key = node.args[1]->string_value;
            size_t count = 0;
            for (const auto& s : slots_res.value.asSlots().active_slots) {
                if (s.has_value() && *s == target_key) {
                    ++count;
                }
            }
            return EvalResult::success(EvalValue::number(static_cast<double>(count)));
        }
        case BuiltinFunc::IsSetParam:
        case BuiltinFunc::IsSetGlobal: {
            EvalResult arg_res = evalNode(*node.args[0], env);
            if (!arg_res.ok) return arg_res;
            if (arg_res.value.isOption()) {
                return EvalResult::success(
                    EvalValue::boolean(arg_res.value.asOption().key.has_value()));
            }
            if (arg_res.value.isResource()) {
                return EvalResult::success(
                    EvalValue::boolean(arg_res.value.asResource().id.has_value()));
            }
            if (arg_res.value.isSlots()) {
                const auto& slots = arg_res.value.asSlots().active_slots;
                bool any_non_empty = std::any_of(
                    slots.begin(), slots.end(),
                    [](const std::optional<std::string>& s) { return s.has_value(); });
                return EvalResult::success(EvalValue::boolean(any_non_empty));
            }
            return EvalResult::success(EvalValue::boolean(false));
        }
    }
    return EvalResult::failure("unknown built-in function");
}

EvalResult evalNode(const AstNode& node, const EvalEnv& env) {
    switch (node.kind) {
        case AstKind::NumberLit:
            return checkedNumber(node.number_value);
        case AstKind::BoolLit:
            return EvalResult::success(EvalValue::boolean(node.bool_value));
        case AstKind::StringLit:
            return EvalResult::success(EvalValue::string(node.string_value));
        case AstKind::Ident:
        case AstKind::Member:
            return evalAccess(node, env);
        case AstKind::UnaryOp: {
            EvalResult op_res = evalNode(*node.left, env);
            if (!op_res.ok) return op_res;
            if (node.unary_op == UnaryOpKind::Negate) {
                return checkedNumber(-op_res.value.asNumber());
            }
            return EvalResult::success(EvalValue::boolean(!op_res.value.asBool()));
        }
        case AstKind::BinaryOp: {
            // Short-circuiting logical operators (§5.2)
            if (node.binary_op == BinaryOpKind::Or) {
                EvalResult lr = evalNode(*node.left, env);
                if (!lr.ok) return lr;
                if (lr.value.asBool()) {
                    return EvalResult::success(EvalValue::boolean(true));
                }
                return evalNode(*node.right, env);
            }
            if (node.binary_op == BinaryOpKind::And) {
                EvalResult lr = evalNode(*node.left, env);
                if (!lr.ok) return lr;
                if (!lr.value.asBool()) {
                    return EvalResult::success(EvalValue::boolean(false));
                }
                return evalNode(*node.right, env);
            }

            EvalResult lr = evalNode(*node.left, env);
            if (!lr.ok) return lr;
            EvalResult rr = evalNode(*node.right, env);
            if (!rr.ok) return rr;

            switch (node.binary_op) {
                case BinaryOpKind::Eq:
                    return EvalResult::success(
                        EvalValue::boolean(evalEquality(lr.value, rr.value)));
                case BinaryOpKind::Ne:
                    return EvalResult::success(
                        EvalValue::boolean(!evalEquality(lr.value, rr.value)));
                case BinaryOpKind::Lt:
                    return EvalResult::success(
                        EvalValue::boolean(lr.value.asNumber() < rr.value.asNumber()));
                case BinaryOpKind::Le:
                    return EvalResult::success(
                        EvalValue::boolean(lr.value.asNumber() <= rr.value.asNumber()));
                case BinaryOpKind::Gt:
                    return EvalResult::success(
                        EvalValue::boolean(lr.value.asNumber() > rr.value.asNumber()));
                case BinaryOpKind::Ge:
                    return EvalResult::success(
                        EvalValue::boolean(lr.value.asNumber() >= rr.value.asNumber()));
                case BinaryOpKind::Add:
                    return checkedNumber(lr.value.asNumber() + rr.value.asNumber());
                case BinaryOpKind::Sub:
                    return checkedNumber(lr.value.asNumber() - rr.value.asNumber());
                case BinaryOpKind::Mul:
                    return checkedNumber(lr.value.asNumber() * rr.value.asNumber());
                case BinaryOpKind::Div: {
                    double denom = rr.value.asNumber();
                    if (denom == 0.0) {
                        return EvalResult::failure("division by zero");
                    }
                    return checkedNumber(
                        lr.value.asNumber() / denom, "division produced non-finite result");
                }
                case BinaryOpKind::Mod: {
                    double denom = rr.value.asNumber();
                    if (denom == 0.0) {
                        return EvalResult::failure("modulo by zero");
                    }
                    return checkedNumber(
                        std::fmod(lr.value.asNumber(), denom), "modulo produced non-finite result");
                }
                case BinaryOpKind::Pow: {
                    double res = std::pow(lr.value.asNumber(), rr.value.asNumber());
                    return checkedNumber(res, "exponentiation '^' produced non-finite result");
                }
                default:
                    break;
            }
            break;
        }
        case AstKind::Call:
            return evalCall(node, env);
    }
    return EvalResult::failure("invalid AST node");
}

} // namespace

EvalResult evaluate(const CompiledExpr& expr, const EvalEnv& env) {
    if (!expr.isValid()) {
        return EvalResult::failure("cannot evaluate invalid compiled expression");
    }
    return evalNode(*expr.root, env);
}

} // namespace expr
