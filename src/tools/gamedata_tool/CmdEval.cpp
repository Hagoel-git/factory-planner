#include "tools/gamedata_tool/Commands.h"

#include "common/FilesystemUtils.h"
#include "core/data/GameDataLoader.h"
#include "core/data/NodeEvaluator.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gamedata_tool {

namespace {

using json = nlohmann::ordered_json;

constexpr int64_t kMaxSweepValues = 32;

bool parseKeyValueParam(
    std::string_view spec,
    std::string& outKey,
    gamedata::ParamValue& outVal,
    std::string& outErr) {
    size_t eq = spec.find('=');
    if (eq == std::string_view::npos || eq == 0) {
        outErr = "expected 'key=json_value', got '" + std::string(spec) + "'";
        return false;
    }
    outKey = std::string(spec.substr(0, eq));
    std::string_view valStr = spec.substr(eq + 1);
    if (valStr.empty()) {
        outErr = "missing JSON value for '" + outKey + "'";
        return false;
    }

    nlohmann::json parsed = nlohmann::json::parse(valStr.begin(), valStr.end(), nullptr, false);
    if (parsed.is_discarded()) {
        outErr = "invalid JSON literal for '" + outKey + "': '" + std::string(valStr) +
                 "' (strings must be JSON-quoted, e.g. " + outKey + "='\"hv\"')";
        return false;
    }

    auto pv = gamedata::ParamValue::fromJson(parsed);
    if (!pv.has_value()) {
        outErr = "unsupported parameter value type for '" + outKey +
                 "' (expected number, bool, string, null, or array of strings/nulls)";
        return false;
    }

    outVal = std::move(*pv);
    return true;
}

std::string formatDoubleCompact(double v) {
    if (!std::isfinite(v)) return "NaN";
    if (std::abs(v) < 1e-12) v = 0.0;
    std::ostringstream oss;
    oss << std::setprecision(6) << v;
    return oss.str();
}

std::string metricFormToString(gamedata::MetricFormKind form) {
    switch (form) {
        case gamedata::MetricFormKind::PerMachine:
            return "per_machine";
        case gamedata::MetricFormKind::PerCraft:
            return "per_craft";
        case gamedata::MetricFormKind::Value:
            return "value";
    }
    return "per_machine";
}

bool approxEqual(double actual, double expected, double tol) {
    if (!std::isfinite(actual) || !std::isfinite(expected)) {
        return false;
    }
    const double diff = std::abs(actual - expected);
    const double scale = std::max(1.0, std::max(std::abs(actual), std::abs(expected)));
    return diff <= tol * scale;
}

// Classifies whether a parameter was clamped or fell back during evaluation.
std::optional<std::string> detectParamAdjustment(
    const gamedata::NodeEval& eval,
    const std::string& paramId,
    const std::unordered_map<std::string, gamedata::ParamValue>& storedParams) {
    const std::string src = "parameters/" + paramId;
    for (const auto& msg : eval.messages) {
        if (msg.source == src && msg.severity == gamedata::EvalStatus::WARNING) {
            if (msg.text.find("clamped") != std::string::npos ||
                msg.text.find("rounding") != std::string::npos) {
                return "clamped";
            }
            return "fallback";
        }
    }
    auto storedIt = storedParams.find(paramId);
    auto resIt = eval.resolved_params.find(paramId);
    if (storedIt != storedParams.end() && resIt != eval.resolved_params.end() &&
        storedIt->second != resIt->second) {
        return "fallback";
    }
    return std::nullopt;
}

struct SingleEvalOutput {
    std::string recipe_id;
    std::string machine_id;
    std::string profile_id;
    gamedata::NodeEval node_eval;
    std::optional<gamedata::NodeMetricsResult> metrics_result;
    std::unordered_map<std::string, gamedata::ParamValue> input_params;
};

SingleEvalOutput evaluateSingle(
    const gamedata::GamePackage& pkg,
    const std::string& recipeId,
    const std::string& machineId,
    const std::unordered_map<std::string, gamedata::ParamValue>& params,
    const gamedata::ResolvedGlobals& globals,
    std::optional<double> machinesCount) {
    SingleEvalOutput out;
    out.recipe_id = recipeId;
    out.machine_id = machineId;
    out.input_params = params;
    if (const auto* m = pkg.findMachine(machineId)) {
        out.profile_id = m->profile;
    }
    out.node_eval = gamedata::evaluateNode(pkg, machineId, recipeId, params, globals);
    if (machinesCount.has_value()) {
        const double mCount = *machinesCount;
        const double crafts = (out.node_eval.craft_time_s > 0.0)
            ? (mCount / out.node_eval.craft_time_s)
            : 0.0;
        out.metrics_result = gamedata::evaluateNodeMetrics(
            pkg, machineId, recipeId, out.node_eval, mCount, crafts, globals);
    }
    return out;
}

json singleEvalToJson(
    const gamedata::GamePackage& pkg,
    const SingleEvalOutput& ev,
    gamedata::RateUnit rateUnit,
    std::optional<double> machinesCount) {
    json j;
    j["recipe"] = ev.recipe_id;
    j["machine"] = ev.machine_id;
    j["profile"] = ev.profile_id;
    j["status"] = gamedata::evalStatusToString(ev.node_eval.status);

    json msgs = json::array();
    for (const auto& m : ev.node_eval.messages) {
        msgs.push_back({
            {"severity", gamedata::evalStatusToString(m.severity)},
            {"source", m.source},
            {"text", m.text}
        });
    }
    if (ev.metrics_result.has_value()) {
        for (const auto& m : ev.metrics_result->messages) {
            msgs.push_back({
                {"severity", gamedata::evalStatusToString(m.severity)},
                {"source", m.source},
                {"text", m.text}
            });
        }
    }
    j["messages"] = std::move(msgs);

    // Resolved parameters in profile order (deterministic)
    json paramsJson = json::object();
    std::vector<std::string> paramOrder;
    if (const auto* prof = pkg.findProfile(ev.profile_id)) {
        paramOrder = prof->params;
    } else {
        for (const auto& [k, _] : ev.node_eval.resolved_params) {
            paramOrder.push_back(k);
        }
        std::sort(paramOrder.begin(), paramOrder.end());
    }
    for (const auto& pid : paramOrder) {
        auto it = ev.node_eval.resolved_params.find(pid);
        if (it == ev.node_eval.resolved_params.end()) continue;
        json pj;
        pj["value"] = it->second.toJson();
        auto visIt = ev.node_eval.param_visible.find(pid);
        pj["visible"] = (visIt != ev.node_eval.param_visible.end()) ? visIt->second : true;
        auto adj = detectParamAdjustment(ev.node_eval, pid, ev.input_params);
        if (adj.has_value()) {
            pj["adjusted"] = *adj;
        } else {
            pj["adjusted"] = nullptr;
        }
        paramsJson[pid] = std::move(pj);
    }
    j["params"] = std::move(paramsJson);
    j["craft_time_s"] = ev.node_eval.craft_time_s;
    j["rate_unit"] = gamedata::rateUnitToString(rateUnit);

    json portsJson = json::array();
    for (const auto& p : ev.node_eval.ports) {
        double ratePerMachine = 0.0;
        if (p.active && ev.node_eval.craft_time_s > 0.0) {
            double perSec = p.amount_per_craft / ev.node_eval.craft_time_s;
            ratePerMachine = pkg.toDisplayRate(perSec, rateUnit) * pkg.resourceDisplayScale(p.resource);
        }
        portsJson.push_back({
            {"slot_key", p.slot_key},
            {"dir", p.dir},
            {"resource", p.resource},
            {"active", p.active},
            {"amount_per_craft", p.amount_per_craft},
            {"rate_per_machine", ratePerMachine}
        });
    }
    j["ports"] = std::move(portsJson);

    json coeffsJson = json::object();
    for (const auto& mc : ev.node_eval.metric_coeffs) {
        coeffsJson[mc.metric_id] = {
            {"enabled", mc.enabled},
            {"form", metricFormToString(mc.form)},
            {"coeff", mc.coeff}
        };
    }
    j["metric_coeffs"] = std::move(coeffsJson);

    if (ev.metrics_result.has_value() && machinesCount.has_value()) {
        j["machines"] = *machinesCount;
        json metJson = json::object();
        for (const auto& nm : ev.metrics_result->metrics) {
            const auto* mdef = pkg.findMetric(nm.metric_id);
            json mj;
            mj["enabled"] = nm.enabled;
            mj["status"] = gamedata::evalStatusToString(nm.status);
            mj["value"] = nm.value;
            mj["unit"] = mdef ? mdef->unit : std::string{};
            if (!nm.error_message.empty()) {
                mj["error"] = nm.error_message;
            }
            metJson[nm.metric_id] = std::move(mj);
        }
        j["metrics"] = std::move(metJson);
    }

    return j;
}

void printSingleEvalHuman(
    const gamedata::GamePackage& pkg,
    const SingleEvalOutput& ev,
    gamedata::RateUnit rateUnit,
    std::optional<double> machinesCount,
    std::ostream& out) {
    out << "Recipe:  " << ev.recipe_id << "\n";
    out << "Machine: " << ev.machine_id << " (profile: " << ev.profile_id << ")\n";
    out << "Status:  " << gamedata::evalStatusToString(ev.node_eval.status) << "\n";

    for (const auto& msg : ev.node_eval.messages) {
        out << "  [" << gamedata::evalStatusToString(msg.severity) << "] "
            << msg.source << ": " << msg.text << "\n";
    }
    if (ev.metrics_result.has_value()) {
        for (const auto& msg : ev.metrics_result->messages) {
            out << "  [" << gamedata::evalStatusToString(msg.severity) << "] "
                << msg.source << ": " << msg.text << "\n";
        }
    }

    std::vector<std::string> paramOrder;
    if (const auto* prof = pkg.findProfile(ev.profile_id)) {
        paramOrder = prof->params;
    }
    if (!paramOrder.empty()) {
        out << "Parameters:\n";
        for (const auto& pid : paramOrder) {
            auto it = ev.node_eval.resolved_params.find(pid);
            if (it == ev.node_eval.resolved_params.end()) continue;
            auto visIt = ev.node_eval.param_visible.find(pid);
            bool visible = (visIt != ev.node_eval.param_visible.end()) ? visIt->second : true;
            auto adj = detectParamAdjustment(ev.node_eval, pid, ev.input_params);
            out << "  " << pid << " = " << it->second.toJson().dump();
            if (!visible) out << " [hidden]";
            if (adj.has_value()) out << " [" << *adj << "]";
            out << "\n";
        }
    }

    out << "Craft time: " << formatDoubleCompact(ev.node_eval.craft_time_s) << " s\n";

    const std::string rateSuffix = gamedata::rateUnitSuffix(rateUnit);
    out << "Ports:\n";
    for (const auto& p : ev.node_eval.ports) {
        out << "  " << std::left << std::setw(10) << p.slot_key << " "
            << std::setw(24) << (p.resource.empty() ? "(unset)" : p.resource) << " ";
        if (!p.active) {
            out << "inactive\n";
            continue;
        }
        double ratePerMachine = 0.0;
        if (ev.node_eval.craft_time_s > 0.0) {
            double perSec = p.amount_per_craft / ev.node_eval.craft_time_s;
            ratePerMachine = pkg.toDisplayRate(perSec, rateUnit) * pkg.resourceDisplayScale(p.resource);
        }
        std::string resUnit = pkg.resourceDisplayUnit(p.resource);
        out << "active  " << formatDoubleCompact(p.amount_per_craft) << "/craft  ("
            << formatDoubleCompact(ratePerMachine)
            << (resUnit.empty() ? "" : (" " + resUnit)) << rateSuffix << " per machine)\n";
    }

    if (!ev.node_eval.metric_coeffs.empty()) {
        out << "Metric coefficients:\n";
        for (const auto& mc : ev.node_eval.metric_coeffs) {
            const auto* mdef = pkg.findMetric(mc.metric_id);
            out << "  " << mc.metric_id << " (" << metricFormToString(mc.form) << "): ";
            if (!mc.enabled) {
                out << "disabled\n";
            } else if (mc.form == gamedata::MetricFormKind::Value) {
                out << "(post-solve)\n";
            } else {
                out << formatDoubleCompact(mc.coeff);
                if (mdef && !mdef->unit.empty()) out << " " << mdef->unit;
                out << "\n";
            }
        }
    }

    if (ev.metrics_result.has_value() && machinesCount.has_value()) {
        out << "Metrics (at " << formatDoubleCompact(*machinesCount) << " machines):\n";
        for (const auto& nm : ev.metrics_result->metrics) {
            const auto* mdef = pkg.findMetric(nm.metric_id);
            out << "  " << nm.metric_id << ": ";
            if (!nm.enabled) {
                out << "disabled\n";
            } else if (nm.status == gamedata::EvalStatus::ERROR) {
                out << "ERROR (" << nm.error_message << ")\n";
            } else {
                out << formatDoubleCompact(nm.value);
                if (mdef && !mdef->unit.empty()) out << " " << mdef->unit;
                out << "\n";
            }
        }
    }
}

int runEvalCases(
    const gamedata::GamePackage& pkg,
    const Args& args,
    std::ostream& out,
    std::ostream& err) {
    const std::filesystem::path casesPath = pathFromUtf8(args.cases_path);
    std::error_code ec;
    if (!std::filesystem::exists(casesPath, ec)) {
        err << "error: cases path does not exist: " << args.cases_path << "\n";
        return kExitFatal;
    }

    std::vector<std::filesystem::path> caseFiles;
    if (std::filesystem::is_directory(casesPath, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 casesPath, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (entry.is_regular_file(ec) && entry.path().extension() == ".json") {
                caseFiles.push_back(entry.path());
            }
        }
        std::sort(caseFiles.begin(), caseFiles.end(),
                  [](const auto& a, const auto& b) {
                      return a.generic_string() < b.generic_string();
                  });
        if (caseFiles.empty()) {
            err << "error: no .json case files found in " << args.cases_path << "\n";
            return kExitFatal;
        }
    } else if (std::filesystem::is_regular_file(casesPath, ec)) {
        caseFiles.push_back(casesPath);
    } else {
        err << "error: cases path is neither a regular file nor a directory: " << args.cases_path << "\n";
        return kExitFatal;
    }

    struct CaseOutcome {
        std::string file;
        std::string name;
        bool passed = true;
        std::vector<std::string> diffs;
    };

    std::vector<CaseOutcome> outcomes;
    size_t passCount = 0;
    size_t failCount = 0;

    for (const auto& cf : caseFiles) {
        std::ifstream ifs(cf, std::ios::binary);
        if (!ifs) {
            err << "error: cannot open cases file: " << pathToUtf8(cf) << "\n";
            return kExitFatal;
        }
        nlohmann::json root = nlohmann::json::parse(ifs, nullptr, false);
        if (root.is_discarded() || !root.is_array()) {
            err << "error: cases file must contain a JSON array: " << pathToUtf8(cf) << "\n";
            return kExitFatal;
        }

        const std::string fileLabel = pathToUtf8(cf);
        for (size_t idx = 0; idx < root.size(); ++idx) {
            CaseOutcome co;
            co.file = fileLabel;
            const nlohmann::json& cj = root[idx];
            if (!cj.is_object()) {
                co.name = "case #" + std::to_string(idx);
                co.passed = false;
                co.diffs.push_back("case entry must be a JSON object");
                ++failCount;
                outcomes.push_back(std::move(co));
                continue;
            }
            co.name = cj.value("name", "case #" + std::to_string(idx));

            double tol = 1e-6;
            if (cj.contains("tol")) {
                if (!cj["tol"].is_number() || cj["tol"].get<double>() < 0.0 ||
                    !std::isfinite(cj["tol"].get<double>())) {
                    co.passed = false;
                    co.diffs.push_back("invalid 'tol' (must be finite number >= 0)");
                    ++failCount;
                    outcomes.push_back(std::move(co));
                    continue;
                }
                tol = cj["tol"].get<double>();
            }

            if (!cj.contains("recipe") || !cj["recipe"].is_string()) {
                co.passed = false;
                co.diffs.push_back("missing required string 'recipe'");
                ++failCount;
                outcomes.push_back(std::move(co));
                continue;
            }
            const std::string recipeId = cj["recipe"].get<std::string>();
            const gamedata::Recipe* recipe = pkg.findRecipe(recipeId);
            if (recipe == nullptr) {
                co.passed = false;
                co.diffs.push_back("unknown recipe '" + recipeId + "'");
                ++failCount;
                outcomes.push_back(std::move(co));
                continue;
            }

            std::string machineId;
            if (cj.contains("machine") && !cj["machine"].is_null()) {
                if (!cj["machine"].is_string()) {
                    co.passed = false;
                    co.diffs.push_back("'machine' must be a string");
                    ++failCount;
                    outcomes.push_back(std::move(co));
                    continue;
                }
                machineId = cj["machine"].get<std::string>();
            } else if (!recipe->compatible_machine_ids.empty()) {
                machineId = recipe->compatible_machine_ids.front();
            }

            const gamedata::Machine* machine = pkg.findMachine(machineId);
            if (machine == nullptr) {
                co.passed = false;
                co.diffs.push_back("unknown machine '" + machineId + "'");
                ++failCount;
                outcomes.push_back(std::move(co));
                continue;
            }
            if (!pkg.canMachineRunRecipe(machineId, recipeId)) {
                co.passed = false;
                co.diffs.push_back("machine '" + machineId + "' cannot run recipe '" + recipeId + "'");
                ++failCount;
                outcomes.push_back(std::move(co));
                continue;
            }

            const gamedata::Profile* profile = pkg.findProfile(machine->profile);
            std::unordered_map<std::string, gamedata::ParamValue> caseParams;
            if (cj.contains("params") && !cj["params"].is_null()) {
                if (!cj["params"].is_object()) {
                    co.passed = false;
                    co.diffs.push_back("'params' must be an object");
                } else {
                    for (auto it = cj["params"].begin(); it != cj["params"].end(); ++it) {
                        const std::string& pid = it.key();
                        if (profile == nullptr ||
                            std::find(profile->params.begin(), profile->params.end(), pid) ==
                                profile->params.end()) {
                            co.passed = false;
                            co.diffs.push_back(
                                "unknown parameter '" + pid + "' for profile '" + machine->profile + "'");
                            continue;
                        }
                        auto pv = gamedata::ParamValue::fromJson(it.value());
                        if (!pv.has_value()) {
                            co.passed = false;
                            co.diffs.push_back("invalid value for parameter '" + pid + "'");
                            continue;
                        }
                        caseParams[pid] = std::move(*pv);
                    }
                }
            }

            std::unordered_map<std::string, gamedata::ParamValue> caseGlobals;
            if (cj.contains("globals") && !cj["globals"].is_null()) {
                if (!cj["globals"].is_object()) {
                    co.passed = false;
                    co.diffs.push_back("'globals' must be an object");
                } else {
                    for (auto it = cj["globals"].begin(); it != cj["globals"].end(); ++it) {
                        const std::string& gid = it.key();
                        if (pkg.findGlobal(gid) == nullptr) {
                            co.passed = false;
                            co.diffs.push_back("unknown global '" + gid + "'");
                            continue;
                        }
                        auto gv = gamedata::ParamValue::fromJson(it.value());
                        if (!gv.has_value()) {
                            co.passed = false;
                            co.diffs.push_back("invalid value for global '" + gid + "'");
                            continue;
                        }
                        caseGlobals[gid] = std::move(*gv);
                    }
                }
            }

            std::optional<double> caseMachines;
            if (cj.contains("machines") && !cj["machines"].is_null()) {
                if (!cj["machines"].is_number() || cj["machines"].get<double>() < 0.0 ||
                    !std::isfinite(cj["machines"].get<double>())) {
                    co.passed = false;
                    co.diffs.push_back("invalid 'machines' value");
                } else {
                    caseMachines = cj["machines"].get<double>();
                }
            }

            if (!co.passed) {
                ++failCount;
                outcomes.push_back(std::move(co));
                continue;
            }

            const gamedata::ResolvedGlobals resolvedGlobals =
                gamedata::resolveProjectGlobals(pkg, caseGlobals);
            const SingleEvalOutput ev = evaluateSingle(
                pkg, recipeId, machineId, caseParams, resolvedGlobals, caseMachines);

            if (cj.contains("expect") && cj["expect"].is_object()) {
                const nlohmann::json& exp = cj["expect"];

                if (exp.contains("status")) {
                    std::string expectedStatus = exp["status"].is_string()
                        ? exp["status"].get<std::string>()
                        : exp["status"].dump();
                    std::string actualStatus = gamedata::evalStatusToString(ev.node_eval.status);
                    if (actualStatus != expectedStatus) {
                        co.passed = false;
                        co.diffs.push_back("status: expected " + expectedStatus + ", got " + actualStatus);
                    }
                }

                if (exp.contains("craft_time_s")) {
                    if (!exp["craft_time_s"].is_number()) {
                        co.passed = false;
                        co.diffs.push_back("expect.craft_time_s must be a number");
                    } else {
                        double expectedCt = exp["craft_time_s"].get<double>();
                        if (!approxEqual(ev.node_eval.craft_time_s, expectedCt, tol)) {
                            co.passed = false;
                            co.diffs.push_back(
                                "craft_time_s: expected " + formatDoubleCompact(expectedCt) +
                                ", got " + formatDoubleCompact(ev.node_eval.craft_time_s));
                        }
                    }
                }

                if (exp.contains("ports") && exp["ports"].is_object()) {
                    for (auto it = exp["ports"].begin(); it != exp["ports"].end(); ++it) {
                        const std::string& slot = it.key();
                        const nlohmann::json& pexp = it.value();
                        const gamedata::EvaluatedPort* port = ev.node_eval.findPort(slot);
                        if (port == nullptr) {
                            co.passed = false;
                            co.diffs.push_back("port '" + slot + "': not found");
                            continue;
                        }
                        if (pexp.is_number()) {
                            double expectedAmt = pexp.get<double>();
                            if (expectedAmt > 0.0 && !port->active) {
                                co.passed = false;
                                co.diffs.push_back(
                                    "port '" + slot + "': expected active amount " +
                                    formatDoubleCompact(expectedAmt) + ", got inactive");
                            } else if (!approxEqual(port->amount_per_craft, expectedAmt, tol)) {
                                co.passed = false;
                                co.diffs.push_back(
                                    "port '" + slot + "': expected " + formatDoubleCompact(expectedAmt) +
                                    ", got " + formatDoubleCompact(port->amount_per_craft));
                            }
                        } else if (pexp.is_boolean()) {
                            bool expectedActive = pexp.get<bool>();
                            if (port->active != expectedActive) {
                                co.passed = false;
                                co.diffs.push_back(
                                    "port '" + slot + "'.active: expected " +
                                    (expectedActive ? "true" : "false") + ", got " +
                                    (port->active ? "true" : "false"));
                            }
                        } else if (pexp.is_object()) {
                            if (pexp.contains("active") && pexp["active"].is_boolean()) {
                                bool ea = pexp["active"].get<bool>();
                                if (port->active != ea) {
                                    co.passed = false;
                                    co.diffs.push_back(
                                        "port '" + slot + "'.active: expected " +
                                        (ea ? "true" : "false") + ", got " +
                                        (port->active ? "true" : "false"));
                                }
                            }
                            if (pexp.contains("resource") && pexp["resource"].is_string()) {
                                std::string er = pexp["resource"].get<std::string>();
                                if (port->resource != er) {
                                    co.passed = false;
                                    co.diffs.push_back(
                                        "port '" + slot + "'.resource: expected '" + er +
                                        "', got '" + port->resource + "'");
                                }
                            }
                            const char* amtKey = pexp.contains("amount") ? "amount"
                                : (pexp.contains("amount_per_craft") ? "amount_per_craft" : nullptr);
                            if (amtKey != nullptr && pexp[amtKey].is_number()) {
                                double ea = pexp[amtKey].get<double>();
                                if (!approxEqual(port->amount_per_craft, ea, tol)) {
                                    co.passed = false;
                                    co.diffs.push_back(
                                        "port '" + slot + "'.amount: expected " + formatDoubleCompact(ea) +
                                        ", got " + formatDoubleCompact(port->amount_per_craft));
                                }
                            }
                        }
                    }
                }

                if (exp.contains("metric_coeffs") && exp["metric_coeffs"].is_object()) {
                    for (auto it = exp["metric_coeffs"].begin(); it != exp["metric_coeffs"].end(); ++it) {
                        const std::string& mid = it.key();
                        const nlohmann::json& mexp = it.value();
                        const gamedata::EvaluatedMetricCoeff* mc = ev.node_eval.findMetric(mid);
                        if (mc == nullptr) {
                            co.passed = false;
                            co.diffs.push_back("metric_coeff '" + mid + "': not found");
                            continue;
                        }
                        if (mexp.is_number()) {
                            double ecVal = mexp.get<double>();
                            if (!mc->enabled) {
                                co.passed = false;
                                co.diffs.push_back(
                                    "metric_coeff '" + mid + "': expected " + formatDoubleCompact(ecVal) +
                                    ", got disabled");
                            } else if (!approxEqual(mc->coeff, ecVal, tol)) {
                                co.passed = false;
                                co.diffs.push_back(
                                    "metric_coeff '" + mid + "': expected " + formatDoubleCompact(ecVal) +
                                    ", got " + formatDoubleCompact(mc->coeff));
                            }
                        } else if (mexp.is_object()) {
                            if (mexp.contains("enabled") && mexp["enabled"].is_boolean() &&
                                mc->enabled != mexp["enabled"].get<bool>()) {
                                co.passed = false;
                                co.diffs.push_back("metric_coeff '" + mid + "'.enabled mismatch");
                            }
                            if (mexp.contains("coeff") && mexp["coeff"].is_number() &&
                                !approxEqual(mc->coeff, mexp["coeff"].get<double>(), tol)) {
                                co.passed = false;
                                co.diffs.push_back(
                                    "metric_coeff '" + mid + "': expected " +
                                    formatDoubleCompact(mexp["coeff"].get<double>()) +
                                    ", got " + formatDoubleCompact(mc->coeff));
                            }
                        }
                    }
                }

                if (exp.contains("metrics") && exp["metrics"].is_object()) {
                    if (!ev.metrics_result.has_value()) {
                        co.passed = false;
                        co.diffs.push_back("expect.metrics requires 'machines' to be specified on the case");
                    } else {
                        for (auto it = exp["metrics"].begin(); it != exp["metrics"].end(); ++it) {
                            const std::string& mid = it.key();
                            const nlohmann::json& mexp = it.value();
                            const gamedata::EvaluatedNodeMetric* nm = ev.metrics_result->findMetric(mid);
                            if (nm == nullptr) {
                                co.passed = false;
                                co.diffs.push_back("metric '" + mid + "': not found");
                                continue;
                            }
                            if (mexp.is_number()) {
                                double evVal = mexp.get<double>();
                                if (!nm->enabled || nm->status == gamedata::EvalStatus::ERROR) {
                                    co.passed = false;
                                    co.diffs.push_back(
                                        "metric '" + mid + "': expected " + formatDoubleCompact(evVal) +
                                        ", got " + (!nm->enabled ? "disabled" : "ERROR"));
                                } else if (!approxEqual(nm->value, evVal, tol)) {
                                    co.passed = false;
                                    co.diffs.push_back(
                                        "metric '" + mid + "': expected " + formatDoubleCompact(evVal) +
                                        ", got " + formatDoubleCompact(nm->value));
                                }
                            } else if (mexp.is_object()) {
                                if (mexp.contains("enabled") && mexp["enabled"].is_boolean() &&
                                    nm->enabled != mexp["enabled"].get<bool>()) {
                                    co.passed = false;
                                    co.diffs.push_back("metric '" + mid + "'.enabled mismatch");
                                }
                                if (mexp.contains("status") && mexp["status"].is_string() &&
                                    gamedata::evalStatusToString(nm->status) != mexp["status"].get<std::string>()) {
                                    co.passed = false;
                                    co.diffs.push_back("metric '" + mid + "'.status mismatch");
                                }
                                if (mexp.contains("value") && mexp["value"].is_number() &&
                                    !approxEqual(nm->value, mexp["value"].get<double>(), tol)) {
                                    co.passed = false;
                                    co.diffs.push_back(
                                        "metric '" + mid + "': expected " +
                                        formatDoubleCompact(mexp["value"].get<double>()) +
                                        ", got " + formatDoubleCompact(nm->value));
                                }
                            }
                        }
                    }
                }

                if (exp.contains("resolved_params") && exp["resolved_params"].is_object()) {
                    for (auto it = exp["resolved_params"].begin(); it != exp["resolved_params"].end(); ++it) {
                        const std::string& pid = it.key();
                        auto resIt = ev.node_eval.resolved_params.find(pid);
                        if (resIt == ev.node_eval.resolved_params.end()) {
                            co.passed = false;
                            co.diffs.push_back("resolved_param '" + pid + "': not found");
                            continue;
                        }
                        auto expectedPv = gamedata::ParamValue::fromJson(it.value());
                        if (!expectedPv.has_value()) {
                            co.passed = false;
                            co.diffs.push_back("resolved_param '" + pid + "': invalid expected value");
                        } else if (expectedPv->isNumber() && resIt->second.isNumber()) {
                            if (!approxEqual(resIt->second.asNumber(), expectedPv->asNumber(), tol)) {
                                co.passed = false;
                                co.diffs.push_back(
                                    "resolved_param '" + pid + "': expected " +
                                    formatDoubleCompact(expectedPv->asNumber()) + ", got " +
                                    formatDoubleCompact(resIt->second.asNumber()));
                            }
                        } else if (resIt->second != *expectedPv) {
                            co.passed = false;
                            co.diffs.push_back(
                                "resolved_param '" + pid + "': expected " + expectedPv->toJson().dump() +
                                ", got " + resIt->second.toJson().dump());
                        }
                    }
                }
            }

            if (co.passed) {
                ++passCount;
            } else {
                ++failCount;
            }
            outcomes.push_back(std::move(co));
        }
    }

    const int exitCode = (failCount == 0) ? kExitOk : kExitError;

    if (args.json_output) {
        json j;
        j["ok"] = (failCount == 0);
        j["passed"] = passCount;
        j["failed"] = failCount;
        j["total"] = outcomes.size();
        json arr = json::array();
        for (const auto& co : outcomes) {
            arr.push_back({
                {"name", co.name},
                {"file", co.file},
                {"passed", co.passed},
                {"diffs", co.diffs}
            });
        }
        j["cases"] = std::move(arr);
        out << j.dump(2) << "\n";
    } else {
        for (const auto& co : outcomes) {
            out << (co.passed ? "PASS  " : "FAIL  ") << co.name << "\n";
            for (const auto& d : co.diffs) {
                out << "  - " << d << "\n";
            }
        }
        out << passCount << "/" << outcomes.size() << " cases passed";
        if (failCount > 0) {
            out << " (" << failCount << " failed)";
        }
        out << ".\n";
    }

    return exitCode;
}

} // namespace

int runEval(const Args& args, std::ostream& out, std::ostream& err) {
    // Parse CLI --param and --global up front so syntax errors always return kExitUsage (64)
    std::unordered_map<std::string, gamedata::ParamValue> cliParams;
    for (const auto& raw : args.params) {
        std::string key;
        gamedata::ParamValue val;
        std::string parseErr;
        if (!parseKeyValueParam(raw, key, val, parseErr)) {
            err << "error: invalid --param: " << parseErr << "\n";
            return kExitUsage;
        }
        cliParams[std::move(key)] = std::move(val);
    }

    std::unordered_map<std::string, gamedata::ParamValue> cliGlobals;
    for (const auto& raw : args.globals) {
        std::string key;
        gamedata::ParamValue val;
        std::string parseErr;
        if (!parseKeyValueParam(raw, key, val, parseErr)) {
            err << "error: invalid --global: " << parseErr << "\n";
            return kExitUsage;
        }
        cliGlobals[std::move(key)] = std::move(val);
    }

    gamedata::GamePackage pkg;
    gamedata::LoadReport report;
    if (!gamedata::loadGamePackage(pathFromUtf8(args.target), pkg, report, false)) {
        if (!report.errors.empty()) {
            err << "error: failed to load package: " << report.errors.front().formatted << "\n";
        } else {
            err << "error: failed to load package '" << args.target << "'\n";
        }
        return report.fatal ? kExitFatal : kExitError;
    }

    if (!args.cases_path.empty()) {
        return runEvalCases(pkg, args, out, err);
    }

    for (const auto& [gid, _] : cliGlobals) {
        if (pkg.findGlobal(gid) == nullptr) {
            err << "error: unknown global '" << gid << "'\n";
            return kExitUsage;
        }
    }

    const gamedata::Recipe* recipe = pkg.findRecipe(args.recipe);
    if (recipe == nullptr) {
        err << "error: unknown recipe '" << args.recipe << "'\n";
        return kExitError;
    }

    std::vector<std::string> targetMachines;
    if (args.all_machines) {
        targetMachines = recipe->compatible_machine_ids;
    } else if (!args.machine.empty()) {
        if (pkg.findMachine(args.machine) == nullptr) {
            err << "error: unknown machine '" << args.machine << "'\n";
            return kExitError;
        }
        if (!pkg.canMachineRunRecipe(args.machine, args.recipe)) {
            err << "error: machine '" << args.machine << "' cannot run recipe '" << args.recipe << "'\n";
            return kExitError;
        }
        targetMachines.push_back(args.machine);
    } else {
        if (recipe->compatible_machine_ids.empty()) {
            err << "error: recipe '" << args.recipe << "' has no compatible machines\n";
            return kExitError;
        }
        targetMachines.push_back(recipe->compatible_machine_ids.front());
    }

    // Validate CLI --param keys against target profile(s)
    for (const auto& [pid, _] : cliParams) {
        if (args.all_machines) {
            if (pkg.findParameter(pid) == nullptr) {
                err << "error: unknown parameter '" << pid << "'\n";
                return kExitUsage;
            }
        } else {
            const auto* m = pkg.findMachine(targetMachines.front());
            const auto* prof = m ? pkg.findProfile(m->profile) : nullptr;
            if (prof == nullptr ||
                std::find(prof->params.begin(), prof->params.end(), pid) == prof->params.end()) {
                err << "error: unknown parameter '" << pid << "' for machine '"
                    << targetMachines.front() << "' (profile '" << (m ? m->profile : "") << "')\n";
                return kExitUsage;
            }
        }
    }

    gamedata::RateUnit rateUnit = pkg.units.default_rate;
    if (!args.rate.empty()) {
        auto parsedRate = gamedata::rateUnitFromString(args.rate);
        if (!parsedRate.has_value()) {
            err << "error: invalid --rate '" << args.rate << "'\n";
            return kExitUsage;
        }
        rateUnit = *parsedRate;
    }

    const gamedata::ResolvedGlobals resolvedGlobals =
        gamedata::resolveProjectGlobals(pkg, cliGlobals);

    // --sweep mode
    if (!args.sweep_param.empty()) {
        const gamedata::ParameterDef* pdef = pkg.findParameter(args.sweep_param);
        if (pdef == nullptr) {
            err << "error: unknown --sweep parameter '" << args.sweep_param << "'\n";
            return kExitUsage;
        }
        if (pdef->type != gamedata::ParameterType::Enum &&
            pdef->type != gamedata::ParameterType::Int) {
            err << "error: --sweep only supports enum or int parameters ('"
                << args.sweep_param << "' is " << gamedata::parameterTypeToString(pdef->type) << ")\n";
            return kExitUsage;
        }

        json sweepAllJson = json::array();
        for (size_t mi = 0; mi < targetMachines.size(); ++mi) {
            const std::string& mid = targetMachines[mi];
            const auto* m = pkg.findMachine(mid);
            const auto* prof = m ? pkg.findProfile(m->profile) : nullptr;
            if (prof == nullptr ||
                std::find(prof->params.begin(), prof->params.end(), args.sweep_param) ==
                    prof->params.end()) {
                err << "error: parameter '" << args.sweep_param << "' is not part of machine '"
                    << mid << "' profile\n";
                return kExitUsage;
            }

            // Evaluate once to resolve dynamic allowed_options / min / max
            const gamedata::NodeEval baseEval = gamedata::evaluateNode(
                pkg, mid, args.recipe, cliParams, resolvedGlobals);
            auto uiIt = baseEval.param_ui.find(args.sweep_param);
            if (uiIt == baseEval.param_ui.end()) {
                err << "error: could not resolve sweep domain for '" << args.sweep_param << "'\n";
                return kExitError;
            }
            const gamedata::EvaluatedParamUi& ui = uiIt->second;

            std::vector<gamedata::ParamValue> sweepValues;
            if (pdef->type == gamedata::ParameterType::Enum) {
                for (const auto& optKey : ui.allowed_options) {
                    sweepValues.push_back(gamedata::ParamValue::string(optKey));
                }
                if (static_cast<int64_t>(sweepValues.size()) > kMaxSweepValues) {
                    err << "error: --sweep '" << args.sweep_param << "' has "
                        << sweepValues.size() << " options (max " << kMaxSweepValues << ")\n";
                    return kExitUsage;
                }
            } else {
                if (!ui.has_min || !ui.has_max) {
                    err << "error: int parameter '" << args.sweep_param
                        << "' must have both min and max bounds for --sweep\n";
                    return kExitUsage;
                }
                const int64_t lo = std::llround(ui.min_val);
                const int64_t hi = std::llround(ui.max_val);
                const int64_t count = hi - lo + 1;
                if (count <= 0 || count > kMaxSweepValues) {
                    err << "error: --sweep range [" << lo << ", " << hi << "] has "
                        << count << " values (max " << kMaxSweepValues << ")\n";
                    return kExitUsage;
                }
                for (int64_t v = lo; v <= hi; ++v) {
                    sweepValues.push_back(gamedata::ParamValue::number(static_cast<double>(v)));
                }
            }

            if (!args.json_output) {
                if (mi > 0) out << "\n";
                out << "Sweep " << args.sweep_param << " for " << args.recipe
                    << " on " << mid << ":\n";
            }

            for (const auto& sv : sweepValues) {
                auto stepParams = cliParams;
                stepParams[args.sweep_param] = sv;
                const SingleEvalOutput ev = evaluateSingle(
                    pkg, args.recipe, mid, stepParams, resolvedGlobals, args.machines);

                if (args.json_output) {
                    json row = singleEvalToJson(pkg, ev, rateUnit, args.machines);
                    row["sweep_param"] = args.sweep_param;
                    row["sweep_value"] = sv.toJson();
                    sweepAllJson.push_back(std::move(row));
                } else {
                    out << "  " << std::left << std::setw(12) << sv.toJson().dump() << " "
                        << std::setw(8) << gamedata::evalStatusToString(ev.node_eval.status)
                        << " time=" << formatDoubleCompact(ev.node_eval.craft_time_s) << "s";
                    for (const auto& p : ev.node_eval.ports) {
                        if (p.active) {
                            out << "  " << p.slot_key << "=" << formatDoubleCompact(p.amount_per_craft);
                        }
                    }
                    for (const auto& mc : ev.node_eval.metric_coeffs) {
                        if (mc.enabled && mc.form != gamedata::MetricFormKind::Value) {
                            out << "  " << mc.metric_id << "=" << formatDoubleCompact(mc.coeff);
                        }
                    }
                    if (ev.metrics_result.has_value()) {
                        for (const auto& nm : ev.metrics_result->metrics) {
                            if (nm.enabled && nm.status != gamedata::EvalStatus::ERROR) {
                                out << "  " << nm.metric_id << "(val)=" << formatDoubleCompact(nm.value);
                            }
                        }
                    }
                    out << "\n";
                }
            }
        }

        if (args.json_output) {
            json root;
            root["sweep_param"] = args.sweep_param;
            root["rows"] = std::move(sweepAllJson);
            out << root.dump(2) << "\n";
        }
        return kExitOk;
    }

    // Standard single or --all-machines evaluation
    std::vector<SingleEvalOutput> results;
    results.reserve(targetMachines.size());
    for (const auto& mid : targetMachines) {
        results.push_back(evaluateSingle(
            pkg, args.recipe, mid, cliParams, resolvedGlobals, args.machines));
    }

    if (args.json_output) {
        if (args.all_machines) {
            json arr = json::array();
            for (const auto& r : results) {
                arr.push_back(singleEvalToJson(pkg, r, rateUnit, args.machines));
            }
            json root;
            root["recipe"] = args.recipe;
            root["results"] = std::move(arr);
            out << root.dump(2) << "\n";
        } else {
            out << singleEvalToJson(pkg, results.front(), rateUnit, args.machines).dump(2) << "\n";
        }
    } else {
        for (size_t i = 0; i < results.size(); ++i) {
            if (i > 0) out << "\n";
            printSingleEvalHuman(pkg, results[i], rateUnit, args.machines, out);
        }
    }

    return kExitOk;
}

} // namespace gamedata_tool
