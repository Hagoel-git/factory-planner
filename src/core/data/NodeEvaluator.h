#ifndef NODEEVALUATOR_H
#define NODEEVALUATOR_H

#include "core/data/GameDataV4.h"

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gamedata {

// Resolves project-wide `global.*` parameters in topological dependency order of their
// static expressions (§3.2, §4.5).
// A global's WARNING, BLOCKED, or ERROR status is recorded per global and only propagates
// to nodes whose evaluated expressions actually reference that global.
ResolvedGlobals resolveProjectGlobals(
    const GamePackage& pkg,
    const std::unordered_map<std::string, ParamValue>& storedGlobals = {});

// Resolves the initial §4.5 Step 1 parameter values for a new node or a newly added profile
// parameter on `(machineId, recipeId)` with empty stored parameters (§7.4), so new nodes start
// with valid allowed values and no fallback WARNING.
std::unordered_map<std::string, ParamValue> resolveInitialNodeParams(
    const GamePackage& pkg,
    std::string_view machineId,
    std::string_view recipeId,
    const ResolvedGlobals* globals = nullptr);

// Runs the 7-step pre-solve evaluation pipeline (§4.5) for a node with `(machineId, recipeId)`
// and stored parameter map `nodeParams`, returning a self-contained `NodeEval` (§7.1).
// Never throws exceptions; any runtime error sets `NodeEval::status = EvalStatus::ERROR`
// and locks all ports to 0.0 (`active = false`).
NodeEval evaluateNode(
    const GamePackage& pkg,
    std::string_view machineId,
    std::string_view recipeId,
    const std::unordered_map<std::string, ParamValue>& nodeParams,
    const ResolvedGlobals& globals,
    std::string_view nodeLabel = {});

// Convenience overload that resolves default project globals (`storedGlobals = {}`).
NodeEval evaluateNode(
    const GamePackage& pkg,
    std::string_view machineId,
    std::string_view recipeId,
    const std::unordered_map<std::string, ParamValue>& nodeParams = {},
    std::string_view nodeLabel = {});

// Evaluates post-solve metrics for a single node (§6.2, §7.6) given its pre-solve `NodeEval`
// and LP-solved `solvedMachines` (M_n) and `solvedCraftsPerSec` (crafts_n).
// If a post-solve `value` metric encounters a runtime error, only that metric entry is marked
// `EvalStatus::ERROR` (and excluded from totals) without altering solved LP port/wire flows.
NodeMetricsResult evaluateNodeMetrics(
    const GamePackage& pkg,
    std::string_view machineId,
    std::string_view recipeId,
    const NodeEval& nodeEval,
    double solvedMachines,
    double solvedCraftsPerSec,
    const ResolvedGlobals& globals = {},
    std::string_view nodeLabel = {});

// Aggregates post-solve metrics across all nodes in the graph (§6.1–§6.2, §7.6).
// Metrics with `aggregate: "none"` are omitted from the returned vector.
// Nodes in `BLOCKED`/`ERROR`, disabled metrics (`enabled == false`), and post-solve `value`
// metrics that failed with a runtime `ERROR` are excluded from aggregation.
std::vector<MetricTotal> aggregateMetricTotals(
    const GamePackage& pkg,
    std::span<const NodeMetricsResult> nodeResults);

} // namespace gamedata

#endif // NODEEVALUATOR_H
