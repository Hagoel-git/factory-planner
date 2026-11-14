#include "core/data/NodeEvaluator.h"

#include "core/expr/ExprDiagnostic.h"
#include "core/expr/ExprEvaluator.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>

namespace gamedata {

namespace {

void upgradeStatus(EvalStatus& current, EvalStatus incoming) {
    if (static_cast<int>(incoming) > static_cast<int>(current)) {
        current = incoming;
    }
}

void lockAllPortsInactive(NodeEval& nodeEval) {
    for (auto& p : nodeEval.ports) {
        p.active = false;
        p.amount_per_craft = 0.0;
    }
}

void populateMachineAndRecipeEnv(
    const Machine& machine,
    const Recipe& recipe,
    expr::EvalEnv& env) {
    env.machine.id = machine.id;
    env.machine.speed = machine.speed;
    env.machine.attrs = machine.attrs;

    env.recipe.id = recipe.id;
    env.recipe.time = recipe.time_seconds;
    env.recipe.time_raw = recipe.time_raw;
    env.recipe.input_items_total = recipe.input_items_total;
    env.recipe.input_fluids_total = recipe.input_fluids_total;
    env.recipe.output_items_total = recipe.output_items_total;
    env.recipe.output_fluids_total = recipe.output_fluids_total;
    env.recipe.attrs = recipe.attrs;
}

void bindRecipePortToEnv(const RecipePort& p, expr::PortEvalInfo& outPort) {
    outPort = expr::PortEvalInfo{};
    outPort.dir = p.dir;
    outPort.amount = p.amount;
    outPort.chance = p.chance;
    outPort.catalyst = p.catalyst;
    outPort.index = static_cast<double>(p.index);
    outPort.base_resource = p.res;
    outPort.resource = std::nullopt;
    outPort.attrs = p.attrs;
}

const RecipePort* findUniquePortForResourceParam(
    const Recipe& recipe,
    std::string_view paramId) {
    for (const auto& p : recipe.in) {
        if (p.resource_param.has_value() && *p.resource_param == paramId) {
            return &p;
        }
    }
    for (const auto& p : recipe.out) {
        if (p.resource_param.has_value() && *p.resource_param == paramId) {
            return &p;
        }
    }
    return nullptr;
}

// Propagates any WARNING, BLOCKED, or ERROR status from globals referenced by `cexpr`.
// Returns false if evaluation must stop immediately due to a BLOCKED or ERROR global.
bool propagateReferencedGlobalStatuses(
    const expr::CompiledExpr& cexpr,
    const ResolvedGlobals& globals,
    std::unordered_set<std::string>& warnedGlobalsOnNode,
    EvalStatus& inOutStatus,
    std::vector<EvalMessage>& inOutMessages) {
    for (const std::string& gid : cexpr.referenced_globals) {
        auto st_it = globals.status_by_global.find(gid);
        if (st_it == globals.status_by_global.end()) {
            continue;
        }
        EvalStatus gst = st_it->second;
        if (gst == EvalStatus::ERROR || gst == EvalStatus::BLOCKED) {
            upgradeStatus(inOutStatus, gst);
            auto msg_it = globals.messages_by_global.find(gid);
            if (msg_it != globals.messages_by_global.end()) {
                for (const auto& m : msg_it->second) {
                    inOutMessages.push_back(m);
                }
            }
            return false;
        }
        if (gst == EvalStatus::WARNING && warnedGlobalsOnNode.insert(gid).second) {
            upgradeStatus(inOutStatus, EvalStatus::WARNING);
            auto msg_it = globals.messages_by_global.find(gid);
            if (msg_it != globals.messages_by_global.end()) {
                for (const auto& m : msg_it->second) {
                    inOutMessages.push_back(m);
                }
            }
        }
    }
    return true;
}

void bindHiddenDefault(
    const GamePackage& pkg,
    const ParameterDef& pdef,
    ParamValue& outParamVal,
    expr::EvalValue& outEvalVal) {
    switch (pdef.type) {
        case ParameterType::Number: {
            double v = (pdef.has_default && pdef.default_value.isNumber())
                ? expr::normalizeZero(pdef.default_value.asNumber())
                : 0.0;
            outParamVal = ParamValue::number(v);
            outEvalVal = expr::EvalValue::number(v);
            return;
        }
        case ParameterType::Int: {
            double v = (pdef.has_default && pdef.default_value.isNumber())
                ? expr::normalizeZero(std::round(pdef.default_value.asNumber()))
                : 0.0;
            outParamVal = ParamValue::number(v);
            outEvalVal = expr::EvalValue::number(v);
            return;
        }
        case ParameterType::Bool: {
            bool v = (pdef.has_default && pdef.default_value.isBool())
                ? pdef.default_value.asBool()
                : false;
            outParamVal = ParamValue::boolean(v);
            outEvalVal = expr::EvalValue::boolean(v);
            return;
        }
        case ParameterType::Enum: {
            if (pdef.has_default) {
                if (pdef.default_value.isNull()) {
                    outParamVal = ParamValue::null();
                    outEvalVal = expr::EvalValue::option(pdef.option_set, std::nullopt);
                    return;
                }
                if (pdef.default_value.isString()) {
                    const std::string& k = pdef.default_value.asString();
                    outParamVal = ParamValue::string(k);
                    outEvalVal = expr::EvalValue::option(pdef.option_set, k);
                    return;
                }
            }
            if (pdef.allow_none) {
                outParamVal = ParamValue::null();
                outEvalVal = expr::EvalValue::option(pdef.option_set, std::nullopt);
                return;
            }
            const OptionSet* os = pkg.findOptionSet(pdef.option_set);
            if (os != nullptr && !os->options.empty()) {
                const std::string& firstKey = os->options.front().key;
                outParamVal = ParamValue::string(firstKey);
                outEvalVal = expr::EvalValue::option(pdef.option_set, firstKey);
                return;
            }
            outParamVal = ParamValue::null();
            outEvalVal = expr::EvalValue::option(pdef.option_set, std::nullopt);
            return;
        }
        case ParameterType::ResourceChoice: {
            if (pdef.has_default) {
                if (pdef.default_value.isNull()) {
                    outParamVal = ParamValue::null();
                    outEvalVal = expr::EvalValue::resource(std::nullopt);
                    return;
                }
                if (pdef.default_value.isString()) {
                    const std::string& r = pdef.default_value.asString();
                    outParamVal = ParamValue::string(r);
                    outEvalVal = expr::EvalValue::resource(r);
                    return;
                }
            }
            if (pdef.allow_none) {
                outParamVal = ParamValue::null();
                outEvalVal = expr::EvalValue::resource(std::nullopt);
                return;
            }
            if (!pdef.candidate_resources.empty()) {
                const std::string& firstRes = pdef.candidate_resources.front();
                outParamVal = ParamValue::string(firstRes);
                outEvalVal = expr::EvalValue::resource(firstRes);
                return;
            }
            outParamVal = ParamValue::null();
            outEvalVal = expr::EvalValue::resource(std::nullopt);
            return;
        }
        case ParameterType::Slots: {
            if (pdef.has_default && pdef.default_value.isSlots()) {
                const auto& defSlots = pdef.default_value.asSlots();
                outParamVal = ParamValue::slots(defSlots);
                outEvalVal = expr::EvalValue::slots(pdef.option_set, defSlots);
                return;
            }
            outParamVal = ParamValue::slots({});
            outEvalVal = expr::EvalValue::slots(pdef.option_set, {});
            return;
        }
    }
}

struct SingleParamResolution {
    EvalStatus status = EvalStatus::OK;
    std::vector<EvalMessage> messages;
    bool visible = true;
    ParamValue resolved_param;
    expr::EvalValue eval_value;
};

SingleParamResolution resolveSingleParamOrGlobal(
    const GamePackage& pkg,
    const ParameterDef& pdef,
    bool isGlobal,
    const Recipe* recipe,
    const std::unordered_map<std::string, ParamValue>& storedValues,
    const ResolvedGlobals& resolvedGlobalsSoFar,
    std::unordered_set<std::string>& warnedGlobals,
    expr::EvalEnv& env,
    std::string_view nodeLabel,
    std::string_view recipeId) {
    SingleParamResolution out;
    const std::string sourcePrefix = (isGlobal ? "globals/" : "parameters/") + pdef.id;
    const std::string displayLabel = pdef.label.empty() ? pdef.id : pdef.label;

    auto addMsg = [&](EvalStatus sev, std::string text, std::string subField = {}) {
        upgradeStatus(out.status, sev);
        std::string src = subField.empty() ? sourcePrefix : (sourcePrefix + "/" + subField);
        out.messages.push_back(EvalMessage{sev, std::move(text), std::move(src)});
    };

    auto addRuntimeError = [&](std::string errText, std::string subField) {
        std::string src = sourcePrefix + "/" + subField;
        std::string formatted = expr::formatRuntimeError(nodeLabel, errText, src, recipeId);
        addMsg(EvalStatus::ERROR, std::move(formatted), std::move(subField));
    };

    // Port-bound resource_choice binding (§3.1, §4.5 Step 1)
    bool boundPort = false;
    if (!isGlobal && pdef.type == ParameterType::ResourceChoice && pdef.is_port_bound) {
        const RecipePort* rport = (recipe != nullptr)
            ? findUniquePortForResourceParam(*recipe, pdef.id)
            : nullptr;
        if (rport == nullptr) {
            // Automatically treated as hidden when unused by the current recipe
            out.visible = false;
            bindHiddenDefault(pkg, pdef, out.resolved_param, out.eval_value);
            return out;
        }
        bindRecipePortToEnv(*rport, env.port);
        boundPort = true;
    }

    struct PortCleanupScope {
        bool active;
        expr::EvalEnv& env;
        ~PortCleanupScope() {
            if (active) {
                env.port = expr::PortEvalInfo{};
            }
        }
    } portScope{boundPort, env};

    // 1. Evaluate visible_if first (§3.1, §4.5 Step 1)
    out.visible = true;
    if (pdef.visible_if.is_present) {
        if (pdef.visible_if.is_literal) {
            out.visible = pdef.visible_if.literal_value;
        } else {
            if (!propagateReferencedGlobalStatuses(
                    pdef.visible_if.compiled_expr, resolvedGlobalsSoFar,
                    warnedGlobals, out.status, out.messages)) {
                return out;
            }
            expr::EvalResult vres = expr::evaluate(pdef.visible_if.compiled_expr, env);
            if (!vres.ok) {
                addRuntimeError(vres.error, "visible_if");
                return out;
            }
            out.visible = vres.value.asBool();
        }
    }

    if (!out.visible) {
        bindHiddenDefault(pkg, pdef, out.resolved_param, out.eval_value);
        return out;
    }

    // 2. Visible parameter validation & clamping / fallback
    auto storedIt = storedValues.find(pdef.id);
    const bool hasStored = (storedIt != storedValues.end());
    const ParamValue* storedPtr = hasStored ? &storedIt->second : nullptr;

    switch (pdef.type) {
        case ParameterType::Number:
        case ParameterType::Int: {
            bool hasMin = pdef.min.is_present;
            bool hasMax = pdef.max.is_present;
            double minVal = 0.0;
            double maxVal = 0.0;

            if (hasMin) {
                if (pdef.min.is_literal) {
                    minVal = pdef.min.literal_value;
                } else {
                    if (!propagateReferencedGlobalStatuses(
                            pdef.min.compiled_expr, resolvedGlobalsSoFar,
                            warnedGlobals, out.status, out.messages)) {
                        return out;
                    }
                    expr::EvalResult mres = expr::evaluate(pdef.min.compiled_expr, env);
                    if (!mres.ok || !std::isfinite(mres.value.asNumber())) {
                        addRuntimeError(mres.ok ? "non-finite min bound" : mres.error, "min");
                        return out;
                    }
                    minVal = mres.value.asNumber();
                }
            }

            if (hasMax) {
                if (pdef.max.is_literal) {
                    maxVal = pdef.max.literal_value;
                } else {
                    if (!propagateReferencedGlobalStatuses(
                            pdef.max.compiled_expr, resolvedGlobalsSoFar,
                            warnedGlobals, out.status, out.messages)) {
                        return out;
                    }
                    expr::EvalResult mres = expr::evaluate(pdef.max.compiled_expr, env);
                    if (!mres.ok || !std::isfinite(mres.value.asNumber())) {
                        addRuntimeError(mres.ok ? "non-finite max bound" : mres.error, "max");
                        return out;
                    }
                    maxVal = mres.value.asNumber();
                }
            }

            if (pdef.type == ParameterType::Int) {
                if (hasMin) minVal = std::ceil(minVal - 1e-9);
                if (hasMax) maxVal = std::floor(maxVal + 1e-9);
            }

            if (hasMin && hasMax && minVal > maxVal) {
                std::ostringstream oss;
                oss << "min (" << minVal << ") > max (" << maxVal << ") for parameter '" << pdef.id << "'";
                addRuntimeError(oss.str(), "min");
                return out;
            }

            double defVal = (pdef.has_default && pdef.default_value.isNumber())
                ? pdef.default_value.asNumber()
                : 0.0;
            if (pdef.type == ParameterType::Int) {
                defVal = std::round(defVal);
            }

            double val = defVal;
            if (hasStored) {
                if (!storedPtr->isNumber() || !std::isfinite(storedPtr->asNumber())) {
                    addMsg(EvalStatus::WARNING,
                           "Invalid stored value for parameter '" + displayLabel + "', using default");
                    val = defVal;
                } else {
                    val = storedPtr->asNumber();
                    if (pdef.type == ParameterType::Int) {
                        double rounded = std::round(val);
                        if (std::abs(val - rounded) > 1e-9) {
                            addMsg(EvalStatus::WARNING,
                                   "Non-integer stored value for int parameter '" + displayLabel +
                                       "', rounding to " + std::to_string(static_cast<int64_t>(rounded)));
                        }
                        val = rounded;
                    }
                    if ((hasMin && val < minVal) || (hasMax && val > maxVal)) {
                        if (hasMin && val < minVal) val = minVal;
                        if (hasMax && val > maxVal) val = maxVal;
                        addMsg(EvalStatus::WARNING,
                               "Value for parameter '" + displayLabel + "' clamped to bounds");
                    }
                }
            }

            if (hasMin && val < minVal) val = minVal;
            if (hasMax && val > maxVal) val = maxVal;
            val = expr::normalizeZero(val);

            out.resolved_param = ParamValue::number(val);
            out.eval_value = expr::EvalValue::number(val);
            return out;
        }

        case ParameterType::Bool: {
            bool defVal = (pdef.has_default && pdef.default_value.isBool())
                ? pdef.default_value.asBool()
                : false;
            bool val = defVal;
            if (hasStored) {
                if (!storedPtr->isBool()) {
                    addMsg(EvalStatus::WARNING,
                           "Invalid stored value for bool parameter '" + displayLabel + "', using default");
                } else {
                    val = storedPtr->asBool();
                }
            }
            out.resolved_param = ParamValue::boolean(val);
            out.eval_value = expr::EvalValue::boolean(val);
            return out;
        }

        case ParameterType::Enum: {
            const OptionSet* os = pkg.findOptionSet(pdef.option_set);
            bool fatalSubExpr = false;

            auto isOptionAllowed = [&](std::string_view key) -> bool {
                if (os == nullptr || os->findOption(key) == nullptr) {
                    return false;
                }
                if (!pdef.allowed_if.is_present) {
                    return true;
                }
                if (pdef.allowed_if.is_literal) {
                    return pdef.allowed_if.literal_value;
                }
                if (!propagateReferencedGlobalStatuses(
                        pdef.allowed_if.compiled_expr, resolvedGlobalsSoFar,
                        warnedGlobals, out.status, out.messages)) {
                    fatalSubExpr = true;
                    return false;
                }
                env.option_value = expr::EvalValue::option(pdef.option_set, std::string(key));
                expr::EvalResult ares = expr::evaluate(pdef.allowed_if.compiled_expr, env);
                env.option_value = std::nullopt;
                if (!ares.ok) {
                    addRuntimeError(ares.error, "allowed_if");
                    fatalSubExpr = true;
                    return false;
                }
                return ares.value.asBool();
            };

            bool resolved = false;
            std::optional<std::string> chosenKey;
            bool storedFailed = false;

            if (hasStored) {
                if (storedPtr->isNull()) {
                    if (pdef.allow_none) {
                        resolved = true;
                        chosenKey = std::nullopt;
                    } else {
                        storedFailed = true;
                    }
                } else if (storedPtr->isString()) {
                    if (isOptionAllowed(storedPtr->asString())) {
                        resolved = true;
                        chosenKey = storedPtr->asString();
                    } else if (fatalSubExpr) {
                        return out;
                    } else {
                        storedFailed = true;
                    }
                } else {
                    storedFailed = true;
                }
            }

            if (!resolved) {
                // Step F1: Try declared default
                if (pdef.has_default) {
                    if (pdef.default_value.isNull() && pdef.allow_none) {
                        resolved = true;
                        chosenKey = std::nullopt;
                    } else if (pdef.default_value.isString()) {
                        if (isOptionAllowed(pdef.default_value.asString())) {
                            resolved = true;
                            chosenKey = pdef.default_value.asString();
                        } else if (fatalSubExpr) {
                            return out;
                        }
                    }
                }
            }

            if (!resolved && os != nullptr) {
                // Step F2: Try first option in declaration order passing allowed_if
                for (const auto& opt : os->options) {
                    if (isOptionAllowed(opt.key)) {
                        resolved = true;
                        chosenKey = opt.key;
                        break;
                    }
                    if (fatalSubExpr) {
                        return out;
                    }
                }
            }

            if (!resolved) {
                // Step F3: null if allow_none, else BLOCKED
                if (pdef.allow_none) {
                    resolved = true;
                    chosenKey = std::nullopt;
                } else {
                    addMsg(EvalStatus::BLOCKED,
                           "No allowed value for parameter '" + displayLabel + "'");
                    return out;
                }
            }

            if (storedFailed) {
                addMsg(EvalStatus::WARNING,
                       "Stored value for parameter '" + displayLabel +
                           "' is not allowed; using fallback value");
            }

            if (chosenKey.has_value()) {
                out.resolved_param = ParamValue::string(*chosenKey);
            } else {
                out.resolved_param = ParamValue::null();
            }
            out.eval_value = expr::EvalValue::option(pdef.option_set, chosenKey);
            return out;
        }

        case ParameterType::ResourceChoice: {
            bool fatalSubExpr = false;

            auto isResourceAllowed = [&](std::string_view resId) -> bool {
                auto candIt = std::find(
                    pdef.candidate_resources.begin(),
                    pdef.candidate_resources.end(),
                    resId);
                if (candIt == pdef.candidate_resources.end()) {
                    return false;
                }
                if (pkg.findResource(resId) == nullptr) {
                    return false;
                }
                if (!pdef.allowed_if.is_present) {
                    return true;
                }
                if (pdef.allowed_if.is_literal) {
                    return pdef.allowed_if.literal_value;
                }
                if (!propagateReferencedGlobalStatuses(
                        pdef.allowed_if.compiled_expr, resolvedGlobalsSoFar,
                        warnedGlobals, out.status, out.messages)) {
                    fatalSubExpr = true;
                    return false;
                }
                env.option_value = expr::EvalValue::resource(std::string(resId));
                expr::EvalResult ares = expr::evaluate(pdef.allowed_if.compiled_expr, env);
                env.option_value = std::nullopt;
                if (!ares.ok) {
                    addRuntimeError(ares.error, "allowed_if");
                    fatalSubExpr = true;
                    return false;
                }
                return ares.value.asBool();
            };

            bool resolved = false;
            std::optional<std::string> chosenRes;
            bool storedFailed = false;

            if (hasStored) {
                if (storedPtr->isNull()) {
                    if (pdef.allow_none) {
                        resolved = true;
                        chosenRes = std::nullopt;
                    } else {
                        storedFailed = true;
                    }
                } else if (storedPtr->isString()) {
                    if (isResourceAllowed(storedPtr->asString())) {
                        resolved = true;
                        chosenRes = storedPtr->asString();
                    } else if (fatalSubExpr) {
                        return out;
                    } else {
                        storedFailed = true;
                    }
                } else {
                    storedFailed = true;
                }
            }

            if (!resolved) {
                // Step F1: Try declared default
                if (pdef.has_default) {
                    if (pdef.default_value.isNull() && pdef.allow_none) {
                        resolved = true;
                        chosenRes = std::nullopt;
                    } else if (pdef.default_value.isString()) {
                        if (isResourceAllowed(pdef.default_value.asString())) {
                            resolved = true;
                            chosenRes = pdef.default_value.asString();
                        } else if (fatalSubExpr) {
                            return out;
                        }
                    }
                }
            }

            if (!resolved) {
                // Step F2: Try first candidate resource in declaration order passing allowed_if
                for (const std::string& cand : pdef.candidate_resources) {
                    if (isResourceAllowed(cand)) {
                        resolved = true;
                        chosenRes = cand;
                        break;
                    }
                    if (fatalSubExpr) {
                        return out;
                    }
                }
            }

            if (!resolved) {
                // Step F3: null if allow_none, else BLOCKED
                if (pdef.allow_none) {
                    resolved = true;
                    chosenRes = std::nullopt;
                } else {
                    addMsg(EvalStatus::BLOCKED,
                           "No allowed value for parameter '" + displayLabel + "'");
                    return out;
                }
            }

            if (chosenRes.has_value() && pkg.findResource(*chosenRes) == nullptr) {
                addRuntimeError("resource '" + *chosenRes + "' missing from loaded data", "resources");
                return out;
            }

            if (storedFailed) {
                addMsg(EvalStatus::WARNING,
                       "Stored value for parameter '" + displayLabel +
                           "' is not allowed; using fallback value");
            }

            if (chosenRes.has_value()) {
                out.resolved_param = ParamValue::string(*chosenRes);
            } else {
                out.resolved_param = ParamValue::null();
            }
            out.eval_value = expr::EvalValue::resource(chosenRes);
            return out;
        }

        case ParameterType::Slots: {
            double countVal = 0.0;
            if (pdef.count.is_literal) {
                countVal = pdef.count.literal_value;
            } else {
                if (!propagateReferencedGlobalStatuses(
                        pdef.count.compiled_expr, resolvedGlobalsSoFar,
                        warnedGlobals, out.status, out.messages)) {
                    return out;
                }
                expr::EvalResult cres = expr::evaluate(pdef.count.compiled_expr, env);
                if (!cres.ok) {
                    addRuntimeError(cres.error, "count");
                    return out;
                }
                countVal = cres.value.asNumber();
            }

            if (!std::isfinite(countVal) || std::abs(countVal - std::round(countVal)) > 1e-9) {
                addRuntimeError("slots.count must evaluate to an integer in [0, 64]", "count");
                return out;
            }
            int64_t countInt = std::llround(countVal);
            if (countInt < 0 || countInt > 64) {
                addRuntimeError(
                    "slots.count evaluated to " + std::to_string(countInt) + " (must be in [0, 64])",
                    "count");
                return out;
            }
            const size_t activeCount = static_cast<size_t>(countInt);

            ParamValue::SlotsArray inputSlots;
            if (hasStored) {
                if (!storedPtr->isSlots()) {
                    addMsg(EvalStatus::WARNING,
                           "Invalid stored value for slots parameter '" + displayLabel + "', using default");
                    if (pdef.has_default && pdef.default_value.isSlots()) {
                        inputSlots = pdef.default_value.asSlots();
                    }
                } else {
                    inputSlots = storedPtr->asSlots();
                    for (size_t i = activeCount; i < inputSlots.size(); ++i) {
                        if (inputSlots[i].has_value() && !inputSlots[i]->empty()) {
                            addMsg(EvalStatus::WARNING,
                                   "Non-empty slot beyond active count (" +
                                       std::to_string(activeCount) + ") ignored for parameter '" +
                                       displayLabel + "'");
                            break;
                        }
                    }
                }
            } else if (pdef.has_default && pdef.default_value.isSlots()) {
                inputSlots = pdef.default_value.asSlots();
            }

            const OptionSet* os = pkg.findOptionSet(pdef.option_set);
            bool fatalSubExpr = false;

            auto isSlotOptionAllowed = [&](std::string_view key) -> bool {
                if (os == nullptr || os->findOption(key) == nullptr) {
                    return false;
                }
                if (!pdef.allowed_if.is_present) {
                    return true;
                }
                if (pdef.allowed_if.is_literal) {
                    return pdef.allowed_if.literal_value;
                }
                if (!propagateReferencedGlobalStatuses(
                        pdef.allowed_if.compiled_expr, resolvedGlobalsSoFar,
                        warnedGlobals, out.status, out.messages)) {
                    fatalSubExpr = true;
                    return false;
                }
                env.option_value = expr::EvalValue::option(pdef.option_set, std::string(key));
                expr::EvalResult ares = expr::evaluate(pdef.allowed_if.compiled_expr, env);
                env.option_value = std::nullopt;
                if (!ares.ok) {
                    addRuntimeError(ares.error, "allowed_if");
                    fatalSubExpr = true;
                    return false;
                }
                return ares.value.asBool();
            };

            ParamValue::SlotsArray resolvedSlots(activeCount, std::nullopt);
            for (size_t i = 0; i < activeCount; ++i) {
                if (i >= inputSlots.size() || !inputSlots[i].has_value() || inputSlots[i]->empty()) {
                    resolvedSlots[i] = std::nullopt;
                    continue;
                }
                const std::string& skey = *inputSlots[i];
                if (isSlotOptionAllowed(skey)) {
                    resolvedSlots[i] = skey;
                } else if (fatalSubExpr) {
                    return out;
                } else {
                    resolvedSlots[i] = std::nullopt;
                    addMsg(EvalStatus::WARNING,
                           "Option '" + skey + "' in slot " + std::to_string(i) +
                               " is not allowed for parameter '" + displayLabel + "'; cleared to empty");
                }
            }

            if (!pdef.allow_empty) {
                for (size_t i = 0; i < activeCount; ++i) {
                    if (!resolvedSlots[i].has_value()) {
                        addMsg(EvalStatus::BLOCKED,
                               "No allowed value for parameter '" + displayLabel + "'");
                        return out;
                    }
                }
            }

            out.resolved_param = ParamValue::slots(resolvedSlots);
            out.eval_value = expr::EvalValue::slots(pdef.option_set, std::move(resolvedSlots));
            return out;
        }
    }

    return out;
}

} // namespace

ResolvedGlobals resolveProjectGlobals(
    const GamePackage& pkg,
    const std::unordered_map<std::string, ParamValue>& storedGlobals) {
    ResolvedGlobals result;
    expr::EvalEnv env = pkg.base_eval_env;

    for (const std::string& gid : pkg.global_eval_order) {
        const ParameterDef* gdef = pkg.findGlobal(gid);
        if (gdef == nullptr) {
            continue;
        }
        std::unordered_set<std::string> warnedGlobals;
        SingleParamResolution gres = resolveSingleParamOrGlobal(
            pkg, *gdef, true, nullptr, storedGlobals,
            result, warnedGlobals, env, "Project Globals", {});

        result.visible[gid] = gres.visible;
        result.status_by_global[gid] = gres.status;
        if (!gres.messages.empty()) {
            result.messages_by_global[gid] = std::move(gres.messages);
        }
        if (gres.status == EvalStatus::OK || gres.status == EvalStatus::WARNING) {
            result.resolved_values[gid] = gres.resolved_param;
            result.eval_values[gid] = gres.eval_value;
            env.globals[gid] = gres.eval_value;
        }
    }

    return result;
}

std::unordered_map<std::string, ParamValue> resolveInitialNodeParams(
    const GamePackage& pkg,
    std::string_view machineId,
    std::string_view recipeId,
    const ResolvedGlobals* globals) {
    std::unordered_map<std::string, ParamValue> initialParams;
    const Machine* machine = pkg.findMachine(machineId);
    const Recipe* recipe = pkg.findRecipe(recipeId);
    if (machine == nullptr || recipe == nullptr) {
        return initialParams;
    }
    const Profile* profile = pkg.findProfile(machine->profile);
    if (profile == nullptr) {
        return initialParams;
    }

    ResolvedGlobals defaultGlobals;
    if (globals == nullptr) {
        defaultGlobals = resolveProjectGlobals(pkg, {});
        globals = &defaultGlobals;
    }

    expr::EvalEnv env = pkg.base_eval_env;
    populateMachineAndRecipeEnv(*machine, *recipe, env);
    env.globals = globals->eval_values;

    const std::unordered_map<std::string, ParamValue> emptyStored;
    std::unordered_set<std::string> warnedGlobals;

    for (const std::string& pid : profile->params) {
        const ParameterDef* pdef = pkg.findParameter(pid);
        if (pdef == nullptr) {
            continue;
        }
        SingleParamResolution pres = resolveSingleParamOrGlobal(
            pkg, *pdef, false, recipe, emptyStored,
            *globals, warnedGlobals, env, machine->name, recipe->id);
        if (pres.status == EvalStatus::OK || pres.status == EvalStatus::WARNING) {
            initialParams[pid] = pres.resolved_param;
            env.params[pid] = pres.eval_value;
        } else {
            ParamValue fallbackParam;
            expr::EvalValue fallbackEval;
            bindHiddenDefault(pkg, *pdef, fallbackParam, fallbackEval);
            initialParams[pid] = fallbackParam;
            env.params[pid] = fallbackEval;
        }
    }

    return initialParams;
}

NodeEval evaluateNode(
    const GamePackage& pkg,
    std::string_view machineId,
    std::string_view recipeId,
    const std::unordered_map<std::string, ParamValue>& nodeParams,
    std::string_view nodeLabel) {
    ResolvedGlobals globals = resolveProjectGlobals(pkg, {});
    return evaluateNode(pkg, machineId, recipeId, nodeParams, globals, nodeLabel);
}

NodeEval evaluateNode(
    const GamePackage& pkg,
    std::string_view machineId,
    std::string_view recipeId,
    const std::unordered_map<std::string, ParamValue>& nodeParams,
    const ResolvedGlobals& globals,
    std::string_view nodeLabel) {
    NodeEval nodeEval;

    const Machine* machine = pkg.findMachine(machineId);
    const Recipe* recipe = pkg.findRecipe(recipeId);
    if (machine == nullptr) {
        nodeEval.status = EvalStatus::ERROR;
        nodeEval.messages.push_back(EvalMessage{
            EvalStatus::ERROR,
            "Unknown machine '" + std::string(machineId) + "'",
            "machine"
        });
        return nodeEval;
    }
    if (recipe == nullptr) {
        nodeEval.status = EvalStatus::ERROR;
        nodeEval.messages.push_back(EvalMessage{
            EvalStatus::ERROR,
            "Unknown recipe '" + std::string(recipeId) + "'",
            "recipe"
        });
        return nodeEval;
    }

    const Profile* profile = pkg.findProfile(machine->profile);
    if (profile == nullptr) {
        nodeEval.status = EvalStatus::ERROR;
        nodeEval.messages.push_back(EvalMessage{
            EvalStatus::ERROR,
            "Unknown profile '" + machine->profile + "' for machine '" + machine->id + "'",
            "profile"
        });
        return nodeEval;
    }

    const std::string effectiveNodeLabel = nodeLabel.empty() ? machine->name : std::string(nodeLabel);

    // Populate the stable port superset upfront (§0 Q2, §7.4)
    nodeEval.ports.reserve(recipe->in.size() + recipe->out.size() + profile->extra_ports.size());
    for (const auto& p : recipe->in) {
        nodeEval.ports.push_back(EvaluatedPort{
            p.slot_key,
            0,
            "in",
            p.res.value_or(""),
            false,
            0.0
        });
    }
    for (const auto& p : recipe->out) {
        nodeEval.ports.push_back(EvaluatedPort{
            p.slot_key,
            0,
            "out",
            p.res.value_or(""),
            false,
            0.0
        });
    }
    for (const auto& ep : profile->extra_ports) {
        nodeEval.ports.push_back(EvaluatedPort{
            ep.slot_key,
            0,
            ep.dir,
            ep.resource.value_or(""),
            false,
            0.0
        });
    }

    auto crpIt = recipe->compiled_by_profile.find(profile->id);
    if (crpIt == recipe->compiled_by_profile.end()) {
        nodeEval.status = EvalStatus::ERROR;
        nodeEval.messages.push_back(EvalMessage{
            EvalStatus::ERROR,
            "Machine '" + machine->id + "' (profile '" + profile->id +
                "') cannot run recipe '" + recipe->id + "'",
            "recipe"
        });
        lockAllPortsInactive(nodeEval);
        return nodeEval;
    }
    const CompiledRecipeProfile& crp = crpIt->second;

    expr::EvalEnv env = pkg.base_eval_env;
    populateMachineAndRecipeEnv(*machine, *recipe, env);
    env.globals = globals.eval_values;

    std::unordered_set<std::string> warnedGlobalsOnNode;

    auto failRuntimeError = [&](std::string_view errMsg, std::string source) -> NodeEval& {
        nodeEval.status = EvalStatus::ERROR;
        std::string formatted = expr::formatRuntimeError(
            effectiveNodeLabel, errMsg, source, recipe->id);
        nodeEval.messages.push_back(EvalMessage{
            EvalStatus::ERROR,
            std::move(formatted),
            std::move(source)
        });
        lockAllPortsInactive(nodeEval);
        return nodeEval;
    };

    auto evalCompiledExpr = [&](const expr::CompiledExpr& cexpr,
                                const std::string& source,
                                expr::EvalValue& outVal) -> bool {
        if (!propagateReferencedGlobalStatuses(
                cexpr, globals, warnedGlobalsOnNode, nodeEval.status, nodeEval.messages)) {
            lockAllPortsInactive(nodeEval);
            return false;
        }
        expr::EvalResult res = expr::evaluate(cexpr, env);
        if (!res.ok) {
            failRuntimeError(res.error, source);
            return false;
        }
        outVal = std::move(res.value);
        return true;
    };

    auto evalBoolOrExpr = [&](const BoolOrExpr& be,
                              bool defaultVal,
                              const std::string& source,
                              bool& outBool) -> bool {
        if (!be.is_present) {
            outBool = defaultVal;
            return true;
        }
        if (be.is_literal) {
            outBool = be.literal_value;
            return true;
        }
        expr::EvalValue v;
        if (!evalCompiledExpr(be.compiled_expr, source, v)) {
            return false;
        }
        outBool = v.asBool();
        return true;
    };

    // ========================================================================
    // Step 1: Resolve `param.*` sequentially in `profile.params` order (§4.5)
    // ========================================================================
    for (const std::string& pid : profile->params) {
        const ParameterDef* pdef = pkg.findParameter(pid);
        if (pdef == nullptr) {
            continue;
        }
        SingleParamResolution pres = resolveSingleParamOrGlobal(
            pkg, *pdef, false, recipe, nodeParams,
            globals, warnedGlobalsOnNode, env, effectiveNodeLabel, recipe->id);

        for (auto& m : pres.messages) {
            nodeEval.messages.push_back(std::move(m));
        }
        upgradeStatus(nodeEval.status, pres.status);

        if (pres.status == EvalStatus::BLOCKED || pres.status == EvalStatus::ERROR) {
            lockAllPortsInactive(nodeEval);
            return nodeEval;
        }

        nodeEval.param_visible[pid] = pres.visible;
        nodeEval.resolved_params[pid] = pres.resolved_param;
        nodeEval.resolved_param_eval_values[pid] = pres.eval_value;
        env.params[pid] = std::move(pres.eval_value);
    }

    // ========================================================================
    // Step 2: Evaluate `let` values in topological dependency order (§4.5)
    // ========================================================================
    const auto& letOrder = crp.has_let_overrides
        ? crp.let_evaluation_order
        : profile->let_evaluation_order;
    const auto& compiledLets = crp.has_let_overrides
        ? crp.compiled_lets
        : profile->compiled_lets;

    for (const std::string& lname : letOrder) {
        const expr::CompiledExpr& lexpr = compiledLets.at(lname);
        std::string src = crp.has_let_overrides && recipe->overrides.let.count(lname)
            ? ("recipes/" + recipe->id + "/overrides/let/" + lname)
            : ("profiles/" + profile->id + "/let/" + lname);
        expr::EvalValue lval;
        if (!evalCompiledExpr(lexpr, src, lval)) {
            return nodeEval;
        }
        env.lets[lname] = lval;
        nodeEval.resolved_lets[lname] = std::move(lval);
    }

    // ========================================================================
    // Step 3: Evaluate `requires` in order (§4.5)
    // ========================================================================
    for (size_t i = 0; i < profile->requires_rules.size(); ++i) {
        const RequireRule& req = profile->requires_rules[i];
        std::string src = "profiles/" + profile->id + "/requires/" + std::to_string(i);
        expr::EvalValue rval;
        if (!evalCompiledExpr(req.compiled_expr, src, rval)) {
            return nodeEval;
        }
        if (!rval.asBool()) {
            nodeEval.status = EvalStatus::BLOCKED;
            nodeEval.messages.push_back(EvalMessage{
                EvalStatus::BLOCKED,
                req.message,
                std::move(src)
            });
            lockAllPortsInactive(nodeEval);
            return nodeEval;
        }
    }

    for (size_t i = 0; i < crp.extra_requires.size(); ++i) {
        const RequireRule& req = crp.extra_requires[i];
        std::string src = "recipes/" + recipe->id + "/overrides/requires/" + std::to_string(i);
        expr::EvalValue rval;
        if (!evalCompiledExpr(req.compiled_expr, src, rval)) {
            return nodeEval;
        }
        if (!rval.asBool()) {
            nodeEval.status = EvalStatus::BLOCKED;
            nodeEval.messages.push_back(EvalMessage{
                EvalStatus::BLOCKED,
                req.message,
                std::move(src)
            });
            lockAllPortsInactive(nodeEval);
            return nodeEval;
        }
    }

    // ========================================================================
    // Step 4: Evaluate `craft_time` (§4.5)
    // ========================================================================
    {
        const expr::CompiledExpr& ctExpr = crp.craft_time_override.has_value()
            ? *crp.craft_time_override
            : profile->compiled_craft_time;
        std::string ctSrc = crp.craft_time_override.has_value()
            ? ("recipes/" + recipe->id + "/overrides/craft_time")
            : ("profiles/" + profile->id + "/craft_time");

        expr::EvalValue ctVal;
        if (!evalCompiledExpr(ctExpr, ctSrc, ctVal)) {
            return nodeEval;
        }
        double ct = ctVal.asNumber();
        if (!std::isfinite(ct) || ct <= 0.0) {
            std::ostringstream oss;
            oss << "craft_time evaluated to " << ct << " s (must be > 0)";
            return failRuntimeError(oss.str(), std::move(ctSrc));
        }
        ct = expr::normalizeZero(ct);
        nodeEval.craft_time_s = ct;
        env.craft_time = ct;
    }

    // ========================================================================
    // Step 5: Evaluate recipe ports in slot order (§4.5)
    // ========================================================================
    auto evaluateRecipePortGroup = [&](const std::vector<RecipePort>& rports,
                                       const std::vector<CompiledRecipePort>& cports,
                                       size_t portSliceOffset) -> bool {
        for (size_t i = 0; i < rports.size(); ++i) {
            const RecipePort& p = rports[i];
            const CompiledRecipePort& cp = cports[i];
            EvaluatedPort& evalPort = nodeEval.ports[portSliceOffset + i];
            const bool isIn = (p.dir == "in");
            const std::string portPrefix = "recipes/" + recipe->id + "/" + p.dir + "/" + std::to_string(i);

            bindRecipePortToEnv(p, env.port);

            // Determine effective `enabled` (port override -> dir override -> port_enabled override -> profile dir -> profile port_enabled)
            const BoolOrExpr* effEnabled = nullptr;
            std::string enabledSrc = "profiles/" + profile->id + "/port_enabled";
            if (cp.compiled_enabled.is_present) {
                effEnabled = &cp.compiled_enabled;
                enabledSrc = portPrefix + "/enabled";
            } else if (isIn && crp.input_enabled_override.is_present) {
                effEnabled = &crp.input_enabled_override;
                enabledSrc = "recipes/" + recipe->id + "/overrides/input_enabled";
            } else if (!isIn && crp.output_enabled_override.is_present) {
                effEnabled = &crp.output_enabled_override;
                enabledSrc = "recipes/" + recipe->id + "/overrides/output_enabled";
            } else if (crp.port_enabled_override.is_present) {
                effEnabled = &crp.port_enabled_override;
                enabledSrc = "recipes/" + recipe->id + "/overrides/port_enabled";
            } else if (isIn && profile->input_enabled.is_present) {
                effEnabled = &profile->input_enabled;
                enabledSrc = "profiles/" + profile->id + "/input_enabled";
            } else if (!isIn && profile->output_enabled.is_present) {
                effEnabled = &profile->output_enabled;
                enabledSrc = "profiles/" + profile->id + "/output_enabled";
            } else if (profile->port_enabled.is_present) {
                effEnabled = &profile->port_enabled;
                enabledSrc = "profiles/" + profile->id + "/port_enabled";
            }

            const bool enabledRefsPortResource =
                (effEnabled != nullptr && !effEnabled->is_literal &&
                 effEnabled->compiled_expr.references_port_resource);

            bool portEnabled = true;
            if (!enabledRefsPortResource && effEnabled != nullptr) {
                if (!evalBoolOrExpr(*effEnabled, true, enabledSrc, portEnabled)) {
                    env.port = expr::PortEvalInfo{};
                    return false;
                }
                if (!portEnabled) {
                    evalPort.active = false;
                    evalPort.amount_per_craft = 0.0;
                    continue;
                }
            }

            // Resolve port resource (`res`, `resource_param` falling back to `res` if null, or `resource_expr`)
            std::string resolvedRes;
            if (p.resource_param.has_value()) {
                auto pit = env.params.find(*p.resource_param);
                if (pit != env.params.end() && pit->second.isResource() &&
                    pit->second.asResource().id.has_value() &&
                    !pit->second.asResource().id->empty()) {
                    resolvedRes = *pit->second.asResource().id;
                } else if (p.res.has_value()) {
                    resolvedRes = *p.res;
                }
            } else if (cp.has_resource_expr) {
                expr::EvalValue rval;
                if (!evalCompiledExpr(cp.compiled_resource_expr, portPrefix + "/resource_expr", rval)) {
                    env.port = expr::PortEvalInfo{};
                    return false;
                }
                if (rval.isString()) {
                    resolvedRes = rval.asString();
                } else if (rval.isResource() && rval.asResource().id.has_value()) {
                    resolvedRes = *rval.asResource().id;
                }
            } else if (p.res.has_value()) {
                resolvedRes = *p.res;
            }

            if (resolvedRes.empty()) {
                evalPort.resource.clear();
                evalPort.active = false;
                evalPort.amount_per_craft = 0.0;
                continue;
            }

            if (pkg.findResource(resolvedRes) == nullptr) {
                env.port = expr::PortEvalInfo{};
                failRuntimeError(
                    "resolved resource '" + resolvedRes + "' missing from loaded data",
                    portPrefix);
                return false;
            }

            evalPort.resource = resolvedRes;
            env.port.resource = resolvedRes;

            if (enabledRefsPortResource && effEnabled != nullptr) {
                if (!evalBoolOrExpr(*effEnabled, true, enabledSrc, portEnabled)) {
                    env.port = expr::PortEvalInfo{};
                    return false;
                }
                if (!portEnabled) {
                    evalPort.active = false;
                    evalPort.amount_per_craft = 0.0;
                    continue;
                }
            }

            // Evaluate effective `amount`
            const expr::CompiledExpr* effAmount = nullptr;
            std::string amountSrc;
            if (cp.has_amount_expr) {
                effAmount = &cp.compiled_amount_expr;
                amountSrc = portPrefix + "/amount_expr";
            } else if (isIn && crp.input_amount_override.has_value()) {
                effAmount = &(*crp.input_amount_override);
                amountSrc = "recipes/" + recipe->id + "/overrides/input_amount";
            } else if (!isIn && crp.output_amount_override.has_value()) {
                effAmount = &(*crp.output_amount_override);
                amountSrc = "recipes/" + recipe->id + "/overrides/output_amount";
            } else if (isIn) {
                effAmount = &profile->compiled_input_amount;
                amountSrc = "profiles/" + profile->id + "/input_amount";
            } else {
                effAmount = &profile->compiled_output_amount;
                amountSrc = "profiles/" + profile->id + "/output_amount";
            }

            expr::EvalValue amtVal;
            if (!evalCompiledExpr(*effAmount, amountSrc, amtVal)) {
                env.port = expr::PortEvalInfo{};
                return false;
            }
            double amt = amtVal.asNumber();
            if (!std::isfinite(amt) || amt < -1e-9) {
                env.port = expr::PortEvalInfo{};
                std::ostringstream oss;
                oss << "port amount evaluated to " << amt << " (must be >= 0)";
                failRuntimeError(oss.str(), std::move(amountSrc));
                return false;
            }
            if (amt <= 1e-9) {
                evalPort.amount_per_craft = 0.0;
                evalPort.active = false;
            } else {
                evalPort.amount_per_craft = expr::normalizeZero(amt);
                evalPort.active = true;
            }
        }
        env.port = expr::PortEvalInfo{};
        return true;
    };

    if (!evaluateRecipePortGroup(recipe->in, crp.in_ports, 0)) {
        return nodeEval;
    }
    if (!evaluateRecipePortGroup(recipe->out, crp.out_ports, recipe->in.size())) {
        return nodeEval;
    }

    // ========================================================================
    // Step 6: Evaluate `extra_ports` in declaration order (§4.5)
    // ========================================================================
    const size_t extraPortOffset = recipe->in.size() + recipe->out.size();
    for (size_t i = 0; i < profile->extra_ports.size(); ++i) {
        const ExtraPortDef& ep = profile->extra_ports[i];
        EvaluatedPort& evalPort = nodeEval.ports[extraPortOffset + i];
        const std::string epPrefix = "profiles/" + profile->id + "/extra_ports/" + ep.id;

        bool epEnabled = true;
        if (!evalBoolOrExpr(ep.enabled, true, epPrefix + "/enabled", epEnabled)) {
            return nodeEval;
        }
        if (!epEnabled) {
            evalPort.active = false;
            evalPort.amount_per_craft = 0.0;
            continue;
        }

        std::string resolvedRes;
        if (ep.resource.has_value()) {
            resolvedRes = *ep.resource;
        } else if (ep.resource_param.has_value()) {
            auto pit = env.params.find(*ep.resource_param);
            if (pit != env.params.end() && pit->second.isResource() &&
                pit->second.asResource().id.has_value()) {
                resolvedRes = *pit->second.asResource().id;
            }
        } else if (ep.resource_expr.has_value()) {
            expr::EvalValue rval;
            if (!evalCompiledExpr(ep.compiled_resource_expr, epPrefix + "/resource_expr", rval)) {
                return nodeEval;
            }
            if (rval.isString()) {
                resolvedRes = rval.asString();
            } else if (rval.isResource() && rval.asResource().id.has_value()) {
                resolvedRes = *rval.asResource().id;
            }
        }

        if (resolvedRes.empty()) {
            evalPort.resource.clear();
            evalPort.active = false;
            evalPort.amount_per_craft = 0.0;
            continue;
        }

        if (pkg.findResource(resolvedRes) == nullptr) {
            return failRuntimeError(
                "resolved resource '" + resolvedRes + "' missing from loaded data",
                epPrefix);
        }

        evalPort.resource = resolvedRes;

        expr::EvalValue amtVal;
        if (!evalCompiledExpr(ep.compiled_amount, epPrefix + "/amount", amtVal)) {
            return nodeEval;
        }
        double amt = amtVal.asNumber();
        if (!std::isfinite(amt) || amt < -1e-9) {
            std::ostringstream oss;
            oss << "extra_port amount evaluated to " << amt << " (must be >= 0)";
            return failRuntimeError(oss.str(), epPrefix + "/amount");
        }
        if (amt <= 1e-9) {
            evalPort.amount_per_craft = 0.0;
            evalPort.active = false;
        } else {
            evalPort.amount_per_craft = expr::normalizeZero(amt);
            evalPort.active = true;
        }
    }

    // ========================================================================
    // Step 7: Evaluate pre-solve metrics (§4.5, §6.2)
    // ========================================================================
    std::vector<std::string> metricIds = profile->metric_order;
    for (const std::string& mid : crp.metric_override_order) {
        if (std::find(metricIds.begin(), metricIds.end(), mid) == metricIds.end()) {
            metricIds.push_back(mid);
        }
    }

    nodeEval.metric_coeffs.reserve(metricIds.size());
    for (const std::string& mid : metricIds) {
        const bool isOverridden = (crp.metric_overrides.find(mid) != crp.metric_overrides.end());
        const ProfileMetricEntry& mentry = isOverridden
            ? crp.metric_overrides.at(mid)
            : profile->metrics.at(mid);
        const std::string mPrefix = isOverridden
            ? ("recipes/" + recipe->id + "/overrides/metrics/" + mid)
            : ("profiles/" + profile->id + "/metrics/" + mid);

        bool mEnabled = true;
        if (!evalBoolOrExpr(mentry.enabled, true, mPrefix + "/enabled", mEnabled)) {
            return nodeEval;
        }

        if (!mEnabled) {
            nodeEval.metric_coeffs.push_back(EvaluatedMetricCoeff{mid, false, mentry.form, 0.0});
            continue;
        }

        double coeff = 0.0;
        if (mentry.form == MetricFormKind::PerMachine || mentry.form == MetricFormKind::PerCraft) {
            expr::EvalValue cval;
            if (!evalCompiledExpr(mentry.compiled_expr, mPrefix, cval)) {
                return nodeEval;
            }
            coeff = expr::normalizeZero(cval.asNumber());
        }

        nodeEval.metric_coeffs.push_back(EvaluatedMetricCoeff{mid, true, mentry.form, coeff});
    }

    return nodeEval;
}

NodeMetricsResult evaluateNodeMetrics(
    const GamePackage& pkg,
    std::string_view machineId,
    std::string_view recipeId,
    const NodeEval& nodeEval,
    double solvedMachines,
    double solvedCraftsPerSec,
    const ResolvedGlobals& globals,
    std::string_view nodeLabel) {
    NodeMetricsResult result;
    result.node_status = nodeEval.status;
    result.metrics.reserve(nodeEval.metric_coeffs.size());

    if (nodeEval.status == EvalStatus::BLOCKED || nodeEval.status == EvalStatus::ERROR) {
        for (const auto& mc : nodeEval.metric_coeffs) {
            result.metrics.push_back(EvaluatedNodeMetric{
                mc.metric_id,
                false,
                EvalStatus::OK,
                0.0,
                {}
            });
        }
        return result;
    }

    const Machine* machine = pkg.findMachine(machineId);
    const Recipe* recipe = pkg.findRecipe(recipeId);
    const Profile* profile = (machine != nullptr) ? pkg.findProfile(machine->profile) : nullptr;
    const CompiledRecipeProfile* crp = nullptr;
    if (recipe != nullptr && profile != nullptr) {
        auto it = recipe->compiled_by_profile.find(profile->id);
        if (it != recipe->compiled_by_profile.end()) {
            crp = &it->second;
        }
    }

    const std::string effectiveNodeLabel = !nodeLabel.empty()
        ? std::string(nodeLabel)
        : (machine != nullptr ? machine->name : std::string(machineId));

    // Lazily constructed post-solve EvalEnv when at least one `value` metric is enabled
    bool envInitialized = false;
    expr::EvalEnv env;
    ResolvedGlobals fallbackGlobals;
    const ResolvedGlobals* activeGlobals = &globals;

    auto ensurePostSolveEnv = [&]() {
        if (envInitialized) return;
        envInitialized = true;
        env = pkg.base_eval_env;
        if (machine != nullptr && recipe != nullptr) {
            populateMachineAndRecipeEnv(*machine, *recipe, env);
        }
        if (globals.eval_values.empty() && !pkg.globals.empty()) {
            fallbackGlobals = resolveProjectGlobals(pkg, {});
            activeGlobals = &fallbackGlobals;
        }
        env.globals = activeGlobals->eval_values;
        env.params = nodeEval.resolved_param_eval_values;
        env.lets = nodeEval.resolved_lets;
        env.craft_time = nodeEval.craft_time_s;
        env.node.machines = expr::normalizeZero(solvedMachines);
        env.node.crafts = expr::normalizeZero(solvedCraftsPerSec);
    };

    std::unordered_set<std::string> warnedGlobals;

    for (const auto& mc : nodeEval.metric_coeffs) {
        if (!mc.enabled) {
            result.metrics.push_back(EvaluatedNodeMetric{
                mc.metric_id,
                false,
                EvalStatus::OK,
                0.0,
                {}
            });
            continue;
        }

        if (mc.form == MetricFormKind::PerMachine) {
            double val = expr::normalizeZero(mc.coeff * solvedMachines);
            if (!std::isfinite(val)) {
                std::string src = "metrics/" + mc.metric_id;
                std::string formatted = expr::formatRuntimeError(
                    effectiveNodeLabel, "non-finite per_machine metric result", src, recipeId);
                result.metrics.push_back(EvaluatedNodeMetric{
                    mc.metric_id, true, EvalStatus::ERROR, 0.0, formatted
                });
                result.messages.push_back(EvalMessage{EvalStatus::ERROR, std::move(formatted), std::move(src)});
            } else {
                result.metrics.push_back(EvaluatedNodeMetric{
                    mc.metric_id, true, EvalStatus::OK, val, {}
                });
            }
            continue;
        }

        if (mc.form == MetricFormKind::PerCraft) {
            double val = expr::normalizeZero(mc.coeff * solvedCraftsPerSec);
            if (!std::isfinite(val)) {
                std::string src = "metrics/" + mc.metric_id;
                std::string formatted = expr::formatRuntimeError(
                    effectiveNodeLabel, "non-finite per_craft metric result", src, recipeId);
                result.metrics.push_back(EvaluatedNodeMetric{
                    mc.metric_id, true, EvalStatus::ERROR, 0.0, formatted
                });
                result.messages.push_back(EvalMessage{EvalStatus::ERROR, std::move(formatted), std::move(src)});
            } else {
                result.metrics.push_back(EvaluatedNodeMetric{
                    mc.metric_id, true, EvalStatus::OK, val, {}
                });
            }
            continue;
        }

        // Post-solve `value` metric (§6.2, §7.6)
        ensurePostSolveEnv();

        const ProfileMetricEntry* mentry = nullptr;
        std::string mSrc = "metrics/" + mc.metric_id;
        if (crp != nullptr && crp->metric_overrides.count(mc.metric_id)) {
            mentry = &crp->metric_overrides.at(mc.metric_id);
            mSrc = "recipes/" + std::string(recipeId) + "/overrides/metrics/" + mc.metric_id;
        } else if (profile != nullptr && profile->metrics.count(mc.metric_id)) {
            mentry = &profile->metrics.at(mc.metric_id);
            mSrc = "profiles/" + profile->id + "/metrics/" + mc.metric_id;
        }

        if (mentry == nullptr || !mentry->compiled_expr.isValid()) {
            std::string formatted = expr::formatRuntimeError(
                effectiveNodeLabel, "missing compiled expression for post-solve metric", mSrc, recipeId);
            result.metrics.push_back(EvaluatedNodeMetric{
                mc.metric_id, true, EvalStatus::ERROR, 0.0, formatted
            });
            result.messages.push_back(EvalMessage{EvalStatus::ERROR, std::move(formatted), std::move(mSrc)});
            continue;
        }

        EvalStatus metricGlobalStatus = EvalStatus::OK;
        std::vector<EvalMessage> metricGlobalMsgs;
        if (!propagateReferencedGlobalStatuses(
                mentry->compiled_expr, *activeGlobals, warnedGlobals,
                metricGlobalStatus, metricGlobalMsgs)) {
            std::string errText = metricGlobalMsgs.empty()
                ? "referenced global failed"
                : metricGlobalMsgs.front().text;
            result.metrics.push_back(EvaluatedNodeMetric{
                mc.metric_id, true, EvalStatus::ERROR, 0.0, errText
            });
            for (auto& gm : metricGlobalMsgs) {
                result.messages.push_back(std::move(gm));
            }
            continue;
        }
        if (metricGlobalStatus == EvalStatus::WARNING) {
            upgradeStatus(result.node_status, EvalStatus::WARNING);
            for (auto& gm : metricGlobalMsgs) {
                result.messages.push_back(std::move(gm));
            }
        }

        expr::EvalResult eres = expr::evaluate(mentry->compiled_expr, env);
        if (!eres.ok || !std::isfinite(eres.value.asNumber())) {
            std::string formatted = expr::formatRuntimeError(
                effectiveNodeLabel,
                eres.ok ? "non-finite metric value" : eres.error,
                mSrc,
                recipeId);
            result.metrics.push_back(EvaluatedNodeMetric{
                mc.metric_id, true, EvalStatus::ERROR, 0.0, formatted
            });
            result.messages.push_back(EvalMessage{
                EvalStatus::ERROR,
                std::move(formatted),
                std::move(mSrc)
            });
            continue;
        }

        result.metrics.push_back(EvaluatedNodeMetric{
            mc.metric_id,
            true,
            EvalStatus::OK,
            expr::normalizeZero(eres.value.asNumber()),
            {}
        });
    }

    return result;
}

std::vector<MetricTotal> aggregateMetricTotals(
    const GamePackage& pkg,
    std::span<const NodeMetricsResult> nodeResults) {
    std::vector<MetricTotal> totals;
    totals.reserve(pkg.metrics.size());

    for (const MetricDef& mdef : pkg.metrics) {
        if (mdef.aggregate == MetricAggregate::None) {
            continue;
        }

        MetricTotal tot;
        tot.metric_id = mdef.id;
        tot.aggregate = mdef.aggregate;
        tot.split_sign = (mdef.aggregate == MetricAggregate::Sum && mdef.split_sign);
        tot.has_value = (mdef.aggregate == MetricAggregate::Sum);

        for (const NodeMetricsResult& nr : nodeResults) {
            if (nr.node_status != EvalStatus::OK && nr.node_status != EvalStatus::WARNING) {
                continue;
            }
            const EvaluatedNodeMetric* nm = nr.findMetric(mdef.id);
            if (nm == nullptr || !nm->enabled || nm->status != EvalStatus::OK) {
                continue;
            }
            double v = expr::normalizeZero(nm->value);
            if (!std::isfinite(v)) {
                continue;
            }

            ++tot.contributing_nodes;
            switch (mdef.aggregate) {
                case MetricAggregate::Sum: {
                    tot.value = expr::normalizeZero(tot.value + v);
                    if (tot.split_sign) {
                        if (v >= 0.0) {
                            tot.positive_sum = expr::normalizeZero(tot.positive_sum + v);
                        } else {
                            tot.negative_sum = expr::normalizeZero(tot.negative_sum + v);
                        }
                        tot.net_sum = expr::normalizeZero(tot.positive_sum + tot.negative_sum);
                    }
                    tot.has_value = true;
                    break;
                }
                case MetricAggregate::Min: {
                    if (!tot.has_value || v < tot.value) {
                        tot.value = v;
                        tot.has_value = true;
                    }
                    break;
                }
                case MetricAggregate::Max: {
                    if (!tot.has_value || v > tot.value) {
                        tot.value = v;
                        tot.has_value = true;
                    }
                    break;
                }
                case MetricAggregate::None:
                    break;
            }
        }

        totals.push_back(std::move(tot));
    }

    return totals;
}

} // namespace gamedata
