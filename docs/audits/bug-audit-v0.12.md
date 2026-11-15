# Bug Audit v0.12

## Lens: Expression engine, v4 loader/validator, and NodeEvaluator spec edge cases

### 1. Inherited `requires`, `extra_ports`, and `metrics` are not recompiled in child profiles, crashing with uncaught `std::bad_variant_access`

- **Severity:** crash
- **Location:**
  - `src/core/data/GameDataLoader.cpp:3385-3393`, `3531`, `3581-3640`, `3682-3740`, `3743-3979`, `3982-4028`
  - `src/core/data/NodeEvaluator.cpp:1142-1165`, `1443-1447`, `1495-1498`, `1669-1670`
  - `src/core/expr/ExprEvaluator.cpp:641`, `648`, `682`, `687`
  - `src/core/expr/ExprValue.h:173-179`
- **What goes wrong:**
  In `PackageLoader::runStage6()`, a child profile copies the parent's compiled `Profile` (`Profile prof = valid_profiles_map.at(parent_id);` at `GameDataLoader.cpp:3385-3387`), including the parent's already-compiled `requires_rules`, `extra_ports`, and `metrics` ASTs. Inside `mergeAndCompileProfile()`, step 2 compiles `prof.let` via `expr::compileLetMap(prof.let, prof_env)` (`GameDataLoader.cpp:3531`) without `required_base_types`, so a child profile can override a `let` variable with a different result type (e.g., `number` in the parent, `bool` or `string` in the child).

  Step 4 (`GameDataLoader.cpp:3682-3740`) unconditionally recompiles `craft_time`, `input_amount`, `output_amount`, `port_enabled`, `input_enabled`, and `output_enabled` against the child's updated `prof_env`. However, the other inherited formula lists are **not** recompiled:
  - Step 3 (`3581-3640`) only compiles rules newly present in the child's `"requires"` array, leaving inherited entries in `prof.requires_rules` type-checked against the parent's `let_types`.
  - Step 5 (`3743-3979`) only compiles entries in the child's `"extra_ports"` array, leaving un-overridden inherited `prof.extra_ports` (`resource_expr`, `amount`, `enabled`) stale.
  - Step 6 (`3982-4028`) only compiles entries in the child's `"metrics"` object, leaving un-overridden inherited `prof.metrics` (`enabled`, `per_machine`, `per_craft`, `value`) stale.

  Because `ExprEvaluator` assumes compile-time type-checking guarantees variant alternatives and reads values directly via `std::get<T>(data)` (`ExprValue::asNumber()`, `asBool()`, etc. in `ExprValue.h:173-179`), evaluating the stale inherited ASTs against `env.lets` holding the child's new type throws an uncaught `std::bad_variant_access` exception instead of rejecting the type mismatch at load time or returning an evaluation error (violating Spec §5.8: *"No exception escapes the evaluator or the solver"*).
- **Reproduction / failing test idea:**
  1. Load a package via `gamedata::loadGamePackageFromMemory()` containing:
     - Metric `"power": { "aggregate": "sum" }` (or no metric, using `requires` / `extra_ports`).
     - Parent profile `"parent"`: `"let": { "x": "2" }`, and `"requires": [{ "expr": "x > 1", "message": "low" }]` (and/or `"extra_ports": [{ "id": "coolant", "dir": "in", "resource": "water", "amount": "x * 5" }]`, and/or `"metrics": { "power": { "per_machine": "100 * x" } }`).
     - Child profile `"child"`: `"extends": "parent"`, `"let": { "x": "true" }` (without redefining `requires`, `extra_ports`, or `metrics`), assigned to machine `"mach_child"`.
  2. Observe `loadGamePackageFromMemory()` returns `true` with `report.errors.empty()`.
  3. Call `gamedata::evaluateNode(pkg, "mach_child", "rec", {})` (or `evaluateNodeMetrics` for an inherited post-solve `value` metric referencing `x`): `NodeEvaluator.cpp:1151` binds `env.lets["x"] = EvalValue::boolean(true)`, and then evaluating the un-recompiled `requires_rules[0]` (`NodeEvaluator.cpp:1162` -> `ExprEvaluator.cpp:682` `lr.value.asNumber() > rr.value.asNumber()`) throws uncaught `std::bad_variant_access`, aborting the process.
- **Confidence:** High
- **Status:** fixed

---

### 2. `evaluateNodeMetrics()` discards caller `ResolvedGlobals` and re-evaluates `{}` defaults whenever all project globals are `BLOCKED` or `ERROR`

- **Severity:** wrong result
- **Location:**
  - `src/core/data/NodeEvaluator.cpp:884-888`, `1561-1565`, `1648-1661`
  - `src/core/graph/FactorySolver.cpp:610-620`
- **What goes wrong:**
  `resolveProjectGlobals()` (`NodeEvaluator.cpp:863-892`) only inserts entries into `result.eval_values` (and `result.resolved_values`) when `gres.status == EvalStatus::OK || gres.status == EvalStatus::WARNING` (`NodeEvaluator.cpp:884-888`). When a global fails with `EvalStatus::BLOCKED` or `EvalStatus::ERROR`, its failed status and diagnostics are recorded in `result.status_by_global` and `result.messages_by_global`, while `result.eval_values` contains no entry for it. If every global in `pkg.globals` fails with `BLOCKED` or `ERROR`, `globals.eval_values` is completely empty even though `resolveProjectGlobals()` ran and populated `status_by_global`.

  In `evaluateNodeMetrics()`, `ensurePostSolveEnv()` (`NodeEvaluator.cpp:1561-1565`) checks:
  ```cpp
  if (globals.eval_values.empty() && !pkg.globals.empty()) {
      fallbackGlobals = resolveProjectGlobals(pkg, {});
      activeGlobals = &fallbackGlobals;
  }
  ```
  Treating `globals.eval_values.empty()` as "the caller omitted `globals` and used the `= {}` default parameter" causes `evaluateNodeMetrics()` (called with `factory_graph.getResolvedGlobals()` at `FactorySolver.cpp:617-620`) to throw away the caller's `globals`, re-run `resolveProjectGlobals(pkg, {})` with empty stored values, and evaluate post-solve `value` metrics against default global values (potentially `EvalStatus::OK`) instead of seeing the `BLOCKED`/`ERROR` status in `propagateReferencedGlobalStatuses()` (`NodeEvaluator.cpp:1648-1661`).
- **Reproduction / failing test idea:**
  1. Package defines `"option_sets": { "mods": { "options": [{ "key": "m1" }] } }`, `"globals": { "g": { "type": "slots", "option_set": "mods", "count": 1, "allow_empty": false, "default": ["m1"] } }`, and a profile metric `"cost": { "value": "node.machines * (count(global.g) + 1)" }`.
  2. Call `ResolvedGlobals globals = gamedata::resolveProjectGlobals(pkg, {{"g", ParamValue::slots({std::nullopt})}})`: `g` hits `NodeEvaluator.cpp:842-849`, giving `globals.status_by_global["g"] == EvalStatus::BLOCKED` and `globals.eval_values.empty() == true`.
  3. Call `NodeEval ne = gamedata::evaluateNode(pkg, mach, rec, {}, globals)` for a recipe whose pre-solve formulas do not reference `global.g`: `ne.status == EvalStatus::OK`.
  4. Call `NodeMetricsResult nm = gamedata::evaluateNodeMetrics(pkg, mach, rec, ne, 1.0, 1.0, globals)`: instead of propagating `BLOCKED` from `global.g` and marking metric `"cost"` `EvalStatus::ERROR` (excluded from totals per Spec §7.6), `NodeEvaluator.cpp:1561-1564` re-resolves `pkg` globals from `{}` (getting `["m1"]`, status `OK`) and returns `nm.metrics[0].status == EvalStatus::OK` with `value == 2.0`.
- **Confidence:** High
- **Status:** fixed

---

### 3. `port_has_base_resource` defaults/hardcodes `true` for recipe overrides, profile port formulas, and port-bound `resource_choice` expressions

- **Severity:** wrong result
- **Location:**
  - `src/core/expr/ExprEnv.h:135`
  - `src/core/data/GameDataLoader.cpp:2714-2730`, `2762`, `2777-2783`, `3659-3661`, `3714-3716`, `4700-4746`
  - `src/core/expr/ExprCompiler.cpp:1242-1250`
  - `src/core/data/NodeEvaluator.cpp:53`
  - `src/core/expr/ExprEvaluator.cpp:245`, `257`, `531`, `559`
- **What goes wrong:**
  Spec §5.5 (`port.base_resource`) and `ExprCompiler.cpp:1242-1247` specify that `port.base_resource` is only allowed when the recipe port declares a static `"res"` field (`V-EXPR-3`). While `compilePortsForProfile()` in `GameDataLoader.cpp:4785` accurately sets `const bool has_base_res = p.res.has_value();` for per-port `resource_expr`, `enabled`, and `amount_expr`, `ExprEnv::port_has_base_resource` defaults to `true` (`ExprEnv.h:135`) and is left or hardcoded `true` in three other places:
  1. **Recipe-level overrides (`GameDataLoader.cpp:4700-4746`):** `overrides.input_amount` (`4703`), `overrides.output_amount` (`4715`), `overrides.port_enabled`, `overrides.input_enabled`, and `overrides.output_enabled` (`4731`) unconditionally pass `true` for `port_has_base_res`, even when the recipe's input/output ports have no static `"res"`.
  2. **Profile-level port formulas (`GameDataLoader.cpp:3659-3661`, `3714-3716`):** `input_amount`, `output_amount`, `port_enabled`, `input_enabled`, and `output_enabled` compile using `prof_env` with `port_has_base_resource == true`, and Stage 8 never checks compatible recipes whose ports omit `"res"`.
  3. **Port-bound `resource_choice` static expressions (`GameDataLoader.cpp:2714-2730`, `2762`, `2777-2783`):** `visible_if` and `allowed_if` compile with `allow_port_in_static = true` and `port_has_base_resource == true`, even if bound to a recipe port lacking `"res"`.

  Instead of rejecting `port.base_resource` with load-time `V-EXPR-3`, the loader accepts the package with zero errors; at runtime `bindRecipePortToEnv()` sets `outPort.base_resource = p.res` (`std::nullopt` at `NodeEvaluator.cpp:53`), so expressions such as `port.base_resource.id`, `port.base_resource.<attr>`, `has_tag(port.base_resource, ...)`, or `lookup("tbl", port.base_resource)` fail with a runtime `ERROR` and lock all node ports to `0`.
- **Reproduction / failing test idea:**
  1. Define a recipe `"r"` whose input port has no `"res"` field (`{ "resource_expr": "'iron_ore'", "amount": 1 }`) and `"overrides": { "input_amount": "if(port.base_resource.id == 'iron_ore', 1, 2)" }` (or `"port_enabled": "has_tag(port.base_resource, 'ore')"`).
  2. Call `loadGamePackageFromMemory(...)`: passes with 0 errors (`GameDataLoader.cpp:4700-4703` hardcodes `port_has_base_res = true`) instead of reporting `V-EXPR-3`.
  3. Call `evaluateNode(pkg, "default_mach", "r", {})`: `NodeEvaluator.cpp:53` sets `env.port.base_resource = std::nullopt`, triggering `ExprEvaluator.cpp:245` (`"cannot access '.id' on unset (null) resource"`) and placing the node into `EvalStatus::ERROR` with all ports inactive.
- **Confidence:** High
- **Status:** fixed

---

### 4. JSON numeric literals in `extra_ports.amount` and `metrics` are converted via `std::to_string(double)`, zeroing valid values `< 5e-7` and truncating precision

- **Severity:** wrong result
- **Location:**
  - `src/core/data/GameDataLoader.cpp:3202`, `3898`, `5279`
  - `src/core/data/NodeEvaluator.cpp:1453-1456`
- **What goes wrong:**
  When a JSON number literal (rather than a string expression) is provided for:
  - profile `metrics` (`per_machine` / `per_craft` / `value` at `GameDataLoader.cpp:3202`),
  - profile `extra_ports[*].amount` (`GameDataLoader.cpp:3898`), or
  - `recipe.overrides.metrics` (`GameDataLoader.cpp:5279`),

  `GameDataLoader` converts the `double` via `std::to_string(d)` and parses/compiles that string into the expression AST without preserving a `ScalarOrExpr` literal fallback. Because `std::to_string(double)` uses fixed-point `%f` formatting with 6 decimal digits:
  - Any positive number smaller than `5e-7` (e.g., `4e-7`, well above `NodeEvaluator`'s `1e-9` active threshold) rounds to `"0.000000"`.
  - Any fractional literal needing more than 6 decimal places loses precision.
  - On `extra_ports`, `NodeEvaluator.cpp:1453-1456` checks `if (amt <= 1e-9)` and marks `"0.000000"` as `active = false`, `amount_per_craft = 0.0`, silently deactivating valid small positive extra ports.
- **Reproduction / failing test idea:**
  1. Define a profile with `"extra_ports": [{ "id": "catalyst", "dir": "in", "resource": "water", "amount": 4e-7 }]` (JSON number literal) and `"metrics": { "m": { "per_craft": 4e-7 } }`.
  2. Call `loadGamePackageFromMemory(...)`: `GameDataLoader.cpp:3898` and `3202` store `expr_src = std::to_string(4e-7)` = `"0.000000"`.
  3. Call `evaluateNode(pkg, mach, rec, {})`: the `extra_port` evaluates `amt == 0.0 <= 1e-9` (`NodeEvaluator.cpp:1453`), returning `active = false` and `amount_per_craft = 0.0`, and metric `"m"` gets `coeff = 0.0` (whereas writing `"4e-7"` as a JSON string yields `active = true`, `amount_per_craft = 4e-7`, and `coeff = 4e-7`).
- **Confidence:** High
- **Status:** fixed

---

### 5. `resource_choice` parameter used in a recipe port's `resource_param` is marked `is_port_bound` globally in Stage 8 without checking prior Stage 6/8 uses, hiding active parameters on other recipes or profiles

- **Severity:** wrong result
- **Location:**
  - `src/core/data/GameDataLoader.cpp:2755-2758`, `3552-3578`, `3853-3861`, `4657-4665`, `5077-5083`
  - `src/core/data/NodeEvaluator.cpp:58-72`, `286-299`
- **What goes wrong:**
  Spec §3.1 defines a **port-bound `resource_choice` parameter** as *"any `resource_choice` parameter referenced by a recipe port's `resource_param` (or whose `visible_if` / `allowed_if` references `port.*`)"*, and V-PROF-4 forbids using a port-bound parameter outside a recipe port's `resource_param` (note that §9.2 V-PROF-4 parenthetically mentions `port.* in visible_if/allowed_if`).

  In `GameDataLoader.cpp`, Stage 4 (`2755-2758`) sets `uses_port_in_static = true` and `is_port_bound = true` only when `visible_if`/`allowed_if` references `port.*`. Stage 6 (`3555`, `3572`, `3853`) and Stage 8 overrides (`4659`) only forbid `pdef->uses_port_in_static`, allowing any `resource_choice` parameter without `port.*` in `visible_if`/`allowed_if` to be used in `extra_ports[*].resource_param`, `let`, `craft_time`, `metrics`, or shared with another profile. However, Stage 8 (`5077-5083`) then sets `m_pkg.parameters[pit->second].is_port_bound = true` globally on `ParameterDef` if **any** recipe port anywhere in the package references `resource_param` — without retroactively rejecting the Stage 6/8 uses in `V-PROF-4`.

  At runtime, `NodeEvaluator.cpp:286-299` checks `pdef.is_port_bound` for every node in any profile listing that parameter. When `findUniquePortForResourceParam(*recipe, pdef.id)` returns `nullptr` — either on another recipe of the same profile that uses the parameter for `extra_ports` / `let` / `metrics`, or on **any** recipe of a completely different profile sharing that parameter — `NodeEvaluator` marks it hidden (`out.visible = false; out.ui.visible = false`) and binds `bindHiddenDefault(...)`, ignoring the user's stored `nodeParams` value.
- **Reproduction / failing test idea:**
  1. Define `"parameters": { "fuel": { "type": "resource_choice", "resources": ["coal", "wood"], "default": "coal" } }` (no `port.*` in `visible_if`/`allowed_if`).
  2. Profile `"boiler"` lists `"params": ["fuel"]` and `"extra_ports": [{ "id": "fuel_in", "dir": "in", "resource_param": "fuel", "amount": 1 }]`; recipe `"boil_water"` uses a machine with profile `"boiler"`.
  3. Another recipe `"alt_recipe"` (using profile `"boiler"` or an unrelated profile `"furnace"` also listing `"fuel"` in `params`) has `"in": [{ "resource_param": "fuel", "amount": 1 }]`.
  4. Call `loadGamePackageFromMemory(...)`: succeeds with **0 errors** (Stage 6 passes `"boiler"` because `uses_port_in_static` is `false`; Stage 8 at line `5081` sets `parameters["fuel"].is_port_bound = true`).
  5. Evaluate a `"boiler"` node running `"boil_water"` with stored `nodeParams = { "fuel": ParamValue::string("wood") }`: `NodeEvaluator.cpp:286-296` sees `pdef.is_port_bound == true`, finds no matching recipe port on `"boil_water"`, sets `param_visible["fuel"] = false` (hiding it in UI), and binds `"fuel"` to default `"coal"` — emitting `"coal"` instead of `"wood"` on `"xin:fuel_in"`.
- **Confidence:** High
- **Status:** fixed (rejected package at load time with `V-PROF-4` when a parameter used as a recipe port's `resource_param` is also used in `extra_ports`, `let`, `craft_time`, `requires`, `metrics`, etc.)

---

## Lens: Graph/solver v4 integration, WireState, changeMachine undo/redo, and ProjectIo format_version / §8.2 handling

### 6. Failed component metrics leak into `m_totals` and produce spurious metric errors

- **Severity:** wrong result
- **Location:**
  - `src/core/graph/FactorySolver.cpp:63-70`, `602-627`, `717-732`, `1406-1424`
- **What goes wrong:**
  `FactorySolver::solve` isolates LP solve failures per connected component: when a component fails (`INFEASIBLE`, `UNBOUNDED`, or `ERROR`), line `721` calls `zeroComponentGraph(factory_graph, comp)` so that only that component's port/wire rates, `machine_count`, and `crafts` are zeroed (`1406-1424`), preserving solved flows on other components (`docs/solver.md:39`). However, after solving all components, line `732` calls `evaluatePostSolveMetrics(factory_graph)`, which loops over every node in `factory_graph.getNodes()` filtered solely by `isNodeActiveInSolver(factory_graph, node)` (`612-613`). `isNodeActiveInSolver` (`63-70`) only skips nodes with `node.is_unresolved` or `eval->status == gamedata::EvalStatus::ERROR`. Because nodes in a failed component have `machine_count == 0.0` and `crafts == 0.0` (not `EvalStatus::ERROR`), `evaluatePostSolveMetrics` still calls `gamedata::evaluateNodeMetrics` (`617-620`) on them, stores their results in `m_nodeMetrics[node.id]` (`623`), and passes them into `gamedata::aggregateMetricTotals(pkg, allResults)` (`626`). Depending on the metric definitions, failed-component nodes still contribute non-zero constant/parameter-based metrics into `Sum`/`Max` totals and `contributing_nodes`, pull `Min`-aggregated metrics down to `0.0`, or trigger spurious division-by-zero metric `ERROR`s when an expression divides by `machine_count`.
- **Reproduction / failing test idea:**
  Build a v4 game package containing a `Min`-aggregated power metric linear in `machine_count` and/or a `Sum`-aggregated flat per-node metric (e.g., constant `1`). Construct two disconnected components in `FactoryGraph`: Component A with conflicting constraints (`LIMIT` < `TARGET`) so its LP solve returns `INFEASIBLE`, and Component B as a valid solved 2-machine producer. Run `FactorySolver::solve()`. Component A's flows and `machine_count` are zeroed (`0.0`), yet `evaluatePostSolveMetrics` evaluates Component A's nodes and folds them into `m_totals` (`Min` power becomes `0.0` instead of Component B's power, and the flat `Sum` metric counts Component A's failed nodes).
- **Confidence:** High
- **Status:** fixed

---

### 7. `FactoryGraph::changeMachine` silently deletes unresolved recipe ports and their preserved wires

- **Severity:** data loss
- **Location:**
  - `src/core/graph/FactoryGraph.cpp:659-776`, `1386-1403`
- **What goes wrong:**
  On project load, `FactoryGraph::deserializeNode` (`1386-1403`) preserves any stored port slot (`in:`, `xin:`, `out:`, `xout:`) absent from the current game data as an unresolved port (`Port::is_unresolved == true`, `active == false`) so attached wires remain intact on the node in `WireState::INACTIVE`. However, `FactoryGraph::changeMachine` (`659-776`) constructs `newInputPorts` and `newOutputPorts` exclusively from `recipe->in` (`0..recipe->in.size()-1`), `recipe->out` (`0..recipe->out.size()-1`), and `newProfile->extra_ports`, collects only those IDs in `keptPortIds` (`766-767`), and calls `removePort(oldPid)` (`772-776`) on every other entry in `oldAllPorts`. Any unresolved **recipe** port (`in:k` or `out:k` with `is_unresolved == true`) is missing from `keptPortIds` and is permanently deleted along with all `INACTIVE` wires attached to it, even though the recipe itself was not changed.
- **Reproduction / failing test idea:**
  Save a project containing node A (`selected_recipe_key = "smelt"`, `machine_key = "furnace_mk1"`) with a wire connected to output slot `"out:1"`. Load that project against updated game data where `"smelt"` now has only 1 output (`"out:0"`), while both `"furnace_mk1"` and `"furnace_mk2"` remain valid for `"smelt"`. Verify `"out:1"` is present with `is_unresolved == true` and its connection is `WireState::INACTIVE`. Then call `graph.changeMachine(nodeA, "furnace_mk2")`: port `"out:1"` is removed from `node->output_ports` and deleted via `removePort`, and the preserved `INACTIVE` connection disappears from `graph.getConnections()`.
- **Confidence:** High
- **Status:** fixed

---

### 8. `migrateNodeParams` leaves stale `invalid_param_raw` entries, causing un-clearable warnings and corrupted serialized `params`

- **Severity:** wrong result
- **Location:**
  - `src/core/graph/FactoryGraph.cpp:375-385`, `479-489`, `535-565`, `592`, `677`, `946-950`
- **What goes wrong:**
  `FactoryGraph::migrateNodeParams` (`535-565`, called by `setNodeRecipe` at `592` and `changeMachine` at `677`) computes `migrated` parameters for the new machine/recipe profile and replaces `node.params` (`564`), but never clears or prunes `node.invalid_param_raw`. When the new profile no longer defines the previously invalid `param_id`:
  1. `FactoryGraph::evaluateNode` (`375-385`) continues downgrading `eval.status` to `EvalStatus::WARNING` and pushing `"Node parameter '<param_id>' failed validation; using schema default."` (`"parameters/<param_id>"`).
  2. `FactoryGraph::setNodeParam` (`479-489`) checks `profile->params` at `479-481` and returns `false` before reaching the `invalid_param_raw.erase(param_id)` at `487-489`, leaving the stale warning permanently un-clearable through `setNodeParam` or the UI.
  3. `FactoryGraph::serialize` (`946-950`) copies every key/value in `node.invalid_param_raw` into `paramsJson`, writing the discarded parameter's invalid raw JSON back into the serialized `"params"` object of the new machine/recipe profile.
- **Reproduction / failing test idea:**
  Load a project where node A runs machine `"mk1"` (which defines param `"mode"`) with invalid raw value `"mode": 999` in `"params"`. Confirm `node->invalid_param_raw` contains `"mode"` and `getNodeEval(nodeA)` has `EvalStatus::WARNING` with `"parameters/mode"`. Switch node A via `graph.changeMachine(nodeA, "mk2")` where `"mk2"` does not declare `"mode"`. Observe `getNodeEval(nodeA)` still reports `EvalStatus::WARNING` for `"parameters/mode"`, `setNodeParam(nodeA, "mode", ...)` returns `false`, and `graph.serialize()` outputs `"mode": 999` inside node A's `"params"`.
- **Confidence:** High
- **Status:** fixed

---

### 9. `restoreNodeSnapshot` omits `is_unresolved` and `unresolved_json`, losing §8.2 fallback data on `ChangeRecipeCommand::undo`

- **Severity:** data loss
- **Location:**
  - `src/services/UndoRedoSystem.cpp:488-492`
  - `src/core/graph/FactoryGraph.cpp:581-583`
- **What goes wrong:**
  When a deserialized unresolved node (`node->is_unresolved == true`, original serialized payload saved in `node->unresolved_json`) has a valid recipe assigned through `ChangeRecipeCommand` -> `FactoryGraph::setNodeRecipe`, lines `581-583` of `FactoryGraph.cpp` reset `node->is_unresolved = false` and `node->unresolved_json = nlohmann::json::object()`. On undo, `restoreNodeSnapshot` (`UndoRedoSystem.cpp:488-492`) only restores `machine_key`, `selected_recipe_key`, `params`, `input_ports`, and `output_ports`; it does not save or restore `is_unresolved` or `unresolved_json`. Undoing `ChangeRecipeCommand` restores `selected_recipe_key` to the missing key, but leaves `node->is_unresolved == false` and `node->unresolved_json` empty (`{}`), wiping the preserved §8.2 JSON fallback.
- **Reproduction / failing test idea:**
  Deserialize a project containing a node referencing an absent recipe `"removed_recipe"` so `node->is_unresolved == true` and `node->unresolved_json` holds the preserved JSON object. Execute `ChangeRecipeCommand` on that node to assign a valid recipe `"valid_recipe"`, then call `undo()`. `node->selected_recipe_key` reverts to `"removed_recipe"`, but `node->is_unresolved` is `false` and `node->unresolved_json` is empty (`{}`). (Note: `ChangeRecipeCommand` currently has no UI callers in `src/editor/` or `src/app/`, and is exercised via `UndoRedoSystem` API/tests.)
- **Confidence:** High
- **Status:** fixed

---

### 10. `SetGlobalParamCommand::undo` is a no-op when reverting a previously unset global parameter

- **Severity:** wrong result
- **Location:**
  - `src/services/UndoRedoSystem.cpp:596-604` (compare `ChangeNodeParamCommand::undo` at `216-225`)
- **What goes wrong:**
  `SetGlobalParamCommand::execute` (`596-598`) calls `graph.setGlobalParam(paramId, newValue)`. When `oldValue` is `std::nullopt` — i.e., `paramId` was absent from `graph.getGlobals()` and implicitly using its schema default before the command — `SetGlobalParamCommand::undo` (`600-604`) only checks `if (oldValue.has_value())` with no `else` branch to erase `paramId` from `m_globals` and re-evaluate nodes (unlike `ChangeNodeParamCommand::undo` at `219-224`). Calling `undo()` therefore leaves `newValue` active in `m_globals`, node evaluations, and serialized JSON.
- **Reproduction / failing test idea:**
  Construct a `FactoryGraph` with a v4 game package defining global `"difficulty"` (default `1`, absent from `graph.getGlobals()`). Execute `SetGlobalParamCommand("difficulty", std::nullopt, ParamValue::integer(3))`, verify `graph.getGlobals().count("difficulty") == 1`, then call `undo()`. `graph.getGlobals()` still contains `"difficulty"` with value `3`, and nodes referencing `$globals.difficulty` continue evaluating with `3` instead of reverting to `1`. (Note: `SetGlobalParamCommand` currently has no UI callers in `src/editor/` or `src/app/`.)
- **Confidence:** High
- **Status:** fixed

---

### 11. `m_invalidGlobalRaw` attaches validation warnings and `WARNING` status to every node in the graph

- **Severity:** minor
- **Location:**
  - `src/core/graph/FactoryGraph.cpp:375-393`
- **What goes wrong:**
  Whenever any project global fails validation on load and is stored in `m_invalidGlobalRaw`, `FactoryGraph::evaluateNode` (`375-393`) unconditionally sets `eval.status = gamedata::EvalStatus::WARNING` (if `OK`) and appends `"Global parameter '<key>' failed validation; using schema default."` (`"globals/<key>"`) to `eval.messages` on **every** node evaluated, regardless of whether the node's machine, recipe, or metric expressions reference `$globals.<key>`.
- **Reproduction / failing test idea:**
  Load a v4 game package declaring global `"tax_rate"` (integer) and a project with `"globals": { "tax_rate": "not_an_int" }` containing a valid node running a fixed-rate recipe with zero expressions touching `$globals.tax_rate`. After `graph.evaluateAllNodes()`, inspect `getNodeEval(nodeId)`: it reports `EvalStatus::WARNING` with `"globals/tax_rate"` instead of `EvalStatus::OK`.
- **Confidence:** Medium (appears deliberate in `FactoryGraph.cpp:386-392` as a way to surface global errors without a separate global diagnostics list, but produces misleading per-node `WARNING` status on completely unrelated nodes).
- **Status:** fixed (load warnings are recorded in `m_loadWarnings`; per-node `WARNING` only propagates to nodes referencing the global per Spec §3.2)

---

## Lens: V4 node UI: parameter widgets, continuous-edit undo, rate-unit scaling, pointer lifetimes after `solve()`, and popups

### 12. Dangling / swapped `connection` pointer dereference after `"Delete Link"` in `"Link Context Menu"`

- **Severity:** crash
- **Location:**
  - `src/editor/factory_planner/FactoryNodeEditor.cpp:1715`, `1994`, `2011-2021` (contrast `"Node Context Menu"` at `1831-1838`)
  - `src/core/graph/FactoryGraph.cpp:242-250`, `270-278`
- **What goes wrong:**
  `FactoryNodeEditor::handlePopups()` caches `auto connection = m_graph->getConnection(IdUtils::fromLinkId(m_contextLinkId));` (`FactoryNodeEditor.cpp:1994`), which returns `&m_connections[idx]` (`FactoryGraph.cpp:272`). When the user clicks `"Delete Link"` (`2011`), `executeCommand(std::make_unique<RemoveConnectionCommand>(connection->from_port, connection->to_port))` (`2012`) removes the connection from `FactoryGraph::m_connections` via swap-and-pop (`FactoryGraph.cpp:242-250`, swapping with `m_connections.back()` and calling `pop_back()`).

  Unlike `"Node Context Menu"` (`1831-1838`), which sets `node = nullptr` and re-fetches `node` after `RemoveNodeCommand`, `"Link Context Menu"` neither nulls nor re-fetches `connection`. On lines `2015-2020`, when `SettingsManager::instance().getSettings().showDebugInfo` (`showDebug`, `1715`) is enabled, lines `2018-2020` immediately read `connection->id`, `connection->from_port`, and `connection->to_port`:
  - If the deleted connection was at the back of `m_connections` (including any 1-link graph), `connection` points past `m_connections.end()` (undefined behavior / out-of-bounds read, triggering an AddressSanitizer crash or garbage values).
  - If another connection was swapped into its slot, `connection` silently prints that unrelated neighbor connection's IDs.
- **Reproduction / failing test idea:**
  Enable `showDebugInfo = true` in settings. Create two connected nodes (`m_connections.size() == 1`). Right-click the wire to open `"Link Context Menu"` and click `"Delete Link"` (`FactoryNodeEditor.cpp:2011`). In an ASan build or test harness, lines `2018-2020` perform an invalid out-of-bounds read past `m_connections.end()` on that frame; with two connections, deleting the first prints the second connection's `id`, `from_port`, and `to_port`.
- **Confidence:** High
- **Status:** fixed

---

### 13. `ResourceKindDef::display_scale` and `display_unit` (§2.4) are ignored across all pin, tooltip, constraint input, and link rate UI

- **Severity:** wrong result
- **Location:**
  - `src/editor/factory_planner/FactoryNodeEditor.cpp:1859-1860`, `1879-1893`, `1947`, `1961-1964`, `2005-2007`, `2960-2979`, `2982-3017` (contrast metrics at `116-130`)
  - `src/core/data/GameDataV4.h:122-128`
  - `src/core/graph/FactoryGraph.h:39-44`
- **What goes wrong:**
  Per `docs/design/gamedata-v4-spec.md` §2.4 (`122-131`), recipes store amounts in the resource kind's native unit while `ResourceKindDef::display_scale` multiplies values for display and `display_unit` precedes the rate unit (e.g., fluids stored in `L` with `display_scale: 0.001, display_unit: "kL"` should display/accept `"kL/min"`). While `GameDataLoader` loads `display_unit`/`display_scale` onto `ResourceKindDef` and rate metrics (`is_rate`) apply rate scaling (`FactoryNodeEditor.cpp:116-130`), neither `FactoryNodeEditor` nor `FactoryGraph` ever calls `findResourceKind` or applies `display_scale`/`display_unit`:
  1. `formatPinRateText` (`2961`) and `buildPinTooltipText` (`2988-2989`, `2999-3008`) use only `m_graph->toDisplayRate(...)` and append `rateUnitToString(m_graph->getRateUnit())`.
  2. `"Pin Context Menu"` populates `m_contextPinConstraintBuf` (`1859-1860`), labels `"Flow Rate Limit (%s):"` (`1879-1880`), parses edits via `m_graph->fromDisplayRate(...)` (`1892`), and displays `Current Actual Flow` / `Excess` (`1947`, `1961-1964`) without multiplying or dividing by `kind->display_scale` and without showing `kind->display_unit`.
  3. `"Link Context Menu"` displays `m_graph->toDisplayRate(connection->rate)` with only `rateUnitToString` (`2005-2007`).
- **Reproduction / failing test idea:**
  Load a v4 game package with `"resource_kinds": { "fluid": { "label": "Fluid", "display_unit": "kL", "display_scale": 0.001 } }`, resource `"water"` (`kind: "fluid"`), and a recipe producing `1000` units/s of `"water"` at `RateUnit::PerSecond`. `formatPinRateText(*outPort)` renders `"1000.00"` instead of `"1.00"`, `buildPinTooltipText` / `"Link Context Menu"` render `"/s"` instead of `" kL/s"`, and entering `"2"` in `"Pin Context Menu"` `"Flow Rate Limit"` stores `user_constraint = 2.0` canonical units/s instead of `2000.0`.
- **Confidence:** High
- **Status:** won't fix (user confirmed current pin/link rate display behavior should stay as-is)

---

### 14. `m_storedPopupPosition` is captured in canvas coordinates after `ed::Resume()`, causing pin-drag `"Create new node"` to double-apply `ed::ScreenToCanvas`

- **Severity:** wrong result
- **Location:**
  - `src/editor/factory_planner/FactoryNodeEditor.cpp:1560-1565`, `1677-1683`, `2049`, `2128-2130`
  - `external/imgui/imgui.cpp:10584-10590` (`ImGui::GetMousePosOnOpeningCurrentPopup`)
  - `external/imgui-node-editor/imgui_canvas.cpp:272-296`, `318-321`, `481-482`
- **What goes wrong:**
  In `handleUserInteractions()` (`FactoryNodeEditor.cpp:1560-1565`, inside `ed::Begin()`'s canvas local space), dropping a new connection wire onto empty canvas runs:
  ```cpp
  ed::Suspend();
  ImGui::OpenPopup("Create new node");
  ed::Resume();
  m_storedPopupPosition = ImGui::GetMousePosOnOpeningCurrentPopup();
  ```
  `ed::Resume()` re-enters canvas local space (`Canvas::EnterLocalSpace`), overwriting `io.MousePos` with `(screenMouse - m_ViewTransformPosition) * m_View.InvScale` (`imgui_canvas.cpp:481-482`). Because `ImGui::BeginPopup("Create new node")` has not executed yet (`g.BeginPopupStack.Size == 0`), `ImGui::GetMousePosOnOpeningCurrentPopup()` (`imgui.cpp:10584-10590`) falls through to `return g.IO.MousePos;`, saving **canvas coordinates** into `m_storedPopupPosition`.

  When a recipe is chosen (`2128`) from the pin-drag context (`hasContext == true` at `2049`), line `2130` computes `ImVec2 nodePos = hasContext ? ed::ScreenToCanvas(m_storedPopupPosition) : ...;`. `ed::ScreenToCanvas` (`Canvas::ToLocal`, `imgui_canvas.cpp:318-321`) transforms an already-canvas point a **second time** (`(canvasPos - m_ViewTransformPosition) * m_View.InvScale`). (`ed::ShowBackgroundContextMenu` at `1682` also captures `m_storedPopupPosition` after `ed::Resume()`, though `hasContext == false` at `2130` happens to bypass `m_storedPopupPosition`.)
- **Reproduction / failing test idea:**
  With the canvas zoomed (`scale != 1.0`) or panned (`m_ViewTransformPosition != (0, 0)`, which also holds whenever the canvas window origin is non-zero), drag a wire from any pin to empty space (`ed::QueryNewNode`) and pick a recipe in `"Create new node"`. The newly created node is placed at `ed::ScreenToCanvas(canvasDropPos)` instead of `canvasDropPos` (e.g., at scale `0.5` and pan offset `(-500, 0)`, dropping at screen `(100, 200)` / canvas `(1200, 400)` computes `ScreenToCanvas((1200, 400)) = (3400, 800)` before port offset, spawning the node far off-screen).
- **Confidence:** High
- **Status:** fixed

---

### 15. Active continuous parameter edits are omitted from `isDirty()` and `saveAs()`, enabling `Ctrl+W`/`Ctrl+Q` silent data loss and `Ctrl+Shift+S` unsynced/un-undone saves

- **Severity:** data loss
- **Location:**
  - `src/editor/factory_planner/FactoryNodeEditor.h:103` (`isDirty()`)
  - `src/editor/factory_planner/FactoryNodeEditor.cpp:507-529` (`saveAs()`), `531-580` (`copy()`), `1896-1899`, `2280-2290` (`applyLiveParamValue()`) (contrast `save()` at `495` and `executeCommand()` at `821, 830`)
  - `src/app/Application.cpp:692-718`, `1331`, `1354`, `1792`
- **What goes wrong:**
  Continuous scalar parameter edits (`Drag`, `Slider`, `Input`, and `Stepper`'s drag/text control) mutate `node->params` live via `applyLiveParamValue()` (`FactoryNodeEditor.cpp:2284`), deferring `ChangeNodeParamCommand` (and deferring `m_solver->solve()` on graphs with `>= 1000` nodes) until `commitActiveParamEdit()`. Two related gaps create data-loss and stale-save paths:
  1. `applyLiveParamValue()` never marks `m_isDirty = true`, and `FactoryNodeEditor::isDirty()` (`FactoryNodeEditor.h:103`) returns only `m_isDirty` without checking `m_activeParamEdit.active && m_activeParamEdit.dirty` (live `"Pin Context Menu"` edits at `1896-1898` similarly mutate `port->user_constraint` without setting `m_isDirty`). Meanwhile `Application::handleShortcuts()` (`Application.cpp:692-706`) handles `Ctrl+Q` and `Ctrl+W` **outside** `if (!io.WantTextInput)` (`719`). If the user opens a saved project (`isDirty() == false`), begins typing a parameter value inside a scalar `Input` or `Ctrl+Click` drag widget (while keyboard focus stays in the text field), and presses `Ctrl+W` or `Ctrl+Q` (or `Ctrl+Q` while `"Pin Context Menu"` is open and has live-edited a constraint), `requestCloseEditor` / `requestQuit` sees `editor->isDirty() == false` (`1331, 1354`) and immediately closes the tab / exits with **no unsaved-changes dialog**, discarding the edit.
  2. `FactoryNodeEditor::saveAs()` (`507-529`) never calls `commitActiveParamEdit()` (unlike `save()` at `495`). Saving via `saveAs()` while `m_activeParamEdit.dirty` is `true` writes the updated `node->params` to disk without solving `>= 1000`-node graphs (leaving serialized `machine_count`, `port.rate`, and `connection.rate` stale), and under `SaveAsMode::SwitchToNewFile` (`518-523`) resets `m_isDirty = false` while `m_activeParamEdit` remains uncommitted without an undo entry. (`copy()` at `531-580` similarly omits `commitActiveParamEdit()`, copying live-edited params with stale rates on `>= 1000`-node graphs.)
- **Reproduction / failing test idea:**
  Open a saved `.fpp` project (`editor->isDirty() == false`). Type a new value into a node's `Input` parameter (without pressing Enter or blurring the field) so `node->params` is updated via `applyLiveParamValue()` (`m_activeParamEdit.dirty == true`). Check `editor->isDirty()` (returns `false`) and invoke `Ctrl+W` / `requestCloseEditor` or `Ctrl+Q` / `requestQuit`: the editor closes with no prompt and the edit is lost. Separately, call `editor->saveAs(outPath, SaveAsMode::SwitchToNewFile)` while `m_activeParamEdit.dirty == true` on a `>= 1000`-node graph: `outPath` contains the new parameter value paired with old pre-edit `machine_count` and `rate` values, and `isDirty()` becomes `false` with `canUndo() == false`.
- **Confidence:** High
- **Status:** fixed

---

### 16. Quadtree culling in `drawNodes()` drops the node with an active `m_activeParamEdit`, stranding uncommitted edits / deferred solves and suppressing all node tooltips

- **Severity:** wrong result
- **Location:**
  - `src/editor/factory_planner/FactoryNodeEditor.cpp:1015-1057`, `1374-1375`, `1452`, `2251-2278`, `2565-2609`
- **What goes wrong:**
  `drawNodes()` queries `m_nodeQuadtree` (`1015-1020`) and skips nodes outside `canvasMin..canvasMax`. Lines `1023-1057` explicitly keep dragged nodes (`m_draggingNodes`), selected nodes, and active pin-creation nodes in `visibleNodeIds`, but do **not** keep `m_activeParamEdit.nodeId`.

  If a node body scalar widget (`UiPlacement::Body`) is actively being edited (`m_activeParamEdit.active == true && m_activeParamEdit.dirty == true`) and the node moves outside the viewport (e.g., panning with middle/right mouse or zooming while a body `InputScalar` or drag is active), the node is culled from `drawNodes()`. Because `drawSingleParameterWidget()` stops running for that node, `trackContinuousScalar()` (`2565-2609`) never sees `ImGui::IsItemDeactivatedAfterEdit()` or `Enter`, so `commitActiveParamEdit()` (`2251-2278`) never triggers when the user releases the mouse button or clicks elsewhere:
  1. `ChangeNodeParamCommand` is not pushed (`m_isDirty` stays unchanged if clean, `canUndo()` stays `false` if the undo stack was empty).
  2. On `>= 1000`-node graphs, `applyLiveParamValue()` skipped solving (`2287-2289`), so `m_solver->solve()` remains skipped indefinitely.
  3. Line `1452` (`else if (ed::GetHoveredNode() == nodeId && !m_activeParamEdit.active && !ImGui::IsAnyItemActive())`) disables **all** node hover tooltips (`buildNodeTooltipText`) across every node on the canvas while `m_activeParamEdit.active` remains `true`.
- **Reproduction / failing test idea:**
  Place a node with a `UiPlacement::Body` `Input` parameter in a clean project (`isDirty() == false`, empty undo stack). Focus and type a new value into its body `Input` widget (`m_activeParamEdit.active == true, dirty == true`), then pan/zoom the canvas view so that node leaves `canvasMin..canvasMax` and click empty canvas. Because the node is culled from `visibleNodeIds`, `commitActiveParamEdit()` is not called: `canUndo()` remains `false`, `isDirty()` remains `false`, and hovering any visible node fails to set `deferredTooltip` (`1452`) because `m_activeParamEdit.active` stays `true`.
- **Confidence:** High
- **Status:** fixed

---

### 17. `NodeEvaluator` early-return on `BLOCKED`/`ERROR` parameter causes `resolveEffectiveParamUi` to bypass `visible_if`, `allowed_if`, dynamic bounds, and `slots` `count` downstream

- **Severity:** wrong result
- **Location:**
  - `src/editor/factory_planner/FactoryNodeEditor.cpp:161-204` (`resolveEffectiveParamUi`), `2491-2501` (`drawNodeParameters`)
  - `src/core/data/NodeEvaluator.cpp:1105-1126` (`evaluateNode`)
- **What goes wrong:**
  In `NodeEvaluator::evaluateNode` (`NodeEvaluator.cpp:1105-1126`), parameters in `profile->params` are evaluated sequentially; as soon as any parameter `p_i` evaluates to `EvalStatus::BLOCKED` or `EvalStatus::ERROR`, line `1124` returns immediately before evaluating any subsequent parameter `p_j` (`j > i`). Consequently, `eval->param_ui` and `eval->param_visible` contain no entries for downstream parameters `p_j`.

  When `drawNodeParameters` (`2492-2501`) calls `resolveEffectiveParamUi(gameData, eval, *pdef)` (`161-204`) for `p_j`, lookup `eval->param_ui.find(pdef.id)` misses and runs the fallback path (`170-203`):
  1. `if (eval)` (`171-174`) executes a lookup into `eval->param_visible` that misses, bypassing `else if (pdef.visible_if.is_present && pdef.visible_if.is_literal)` (`175-177`). Thus `fallback.visible` defaults to `true` even when `"visible_if"` is literal `false` (as well as when `"visible_if"` is an expression), displaying hidden parameters in the UI.
  2. Lines `194-201` populate `fallback.allowed_options` with every option in `pdef.option_set` and `fallback.allowed_resources` with all `pdef.candidate_resources`, ignoring `allowed_if` filters; dynamic `min`/`max` expressions are dropped; and dynamic `Slots` `count` expressions fall back to `0` (`"(0 slots)"`).
  *(Additionally, for a downstream `Int` parameter with fractional literal bounds like `min: 0.4, max: 0.6`, fallback rounding at `187-188` yields `min_val = 1 > max_val = 0`, reaching `std::clamp(val64, min64, max64)` at `2709` with `min64 > max64`.)*
- **Reproduction / failing test idea:**
  Define a profile with `params: ["p1", "p2", "p3"]`, where `p1` is a required `ResourceChoice` (`"allow_none": false`, no default, so `evaluateNode` returns `BLOCKED` at `NodeEvaluator.cpp:1124`), `p2` has `"visible_if": "false"` (literal false), and `p3` is an `Enum` over option set `["a", "b"]` where `"b"` has `"allowed_if": "false"`. On a newly placed node (`p1` unset, `eval != nullptr`), `resolveEffectiveParamUi` returns `visible == true` for `p2` and `allowed_options == {"a", "b"}` for `p3`, rendering `p2` and letting the user select forbidden option `"b"`.
- **Confidence:** High
- **Status:** fixed

---

### 18. `UiWidget::Stepper` for `Number` lacks a `std::isfinite` guard, allowing `NaN`/`Inf` via `Ctrl+Click` input to corrupt `node->params` and serialize as `null`

- **Severity:** wrong result (with stored value loss across save/reload)
- **Location:**
  - `src/editor/factory_planner/FactoryNodeEditor.cpp:2656-2663` (contrast `Slider`/`Drag`/`Input` at `2688-2695`)
  - `external/imgui/imgui_widgets.cpp:2380-2387` (`DataTypeApplyFromText`), `2434-2441` (`DataTypeClampT`), `3755` (`TempInputScalar`)
  - `src/core/data/NodeEvaluator.cpp:412-415`
  - `src/core/data/GameDataV4.h:275-282`, `296-300`
  - `src/core/graph/FactoryGraph.cpp:939` + `external/nlohmann/json.hpp:19520-19527` (`dump_float`)
- **What goes wrong:**
  For `Number` parameters rendered with `Slider`, `Drag`, or `Input`, `FactoryNodeEditor.cpp:2689-2691` explicitly rejects non-finite values with `if (!std::isfinite(val))`. The `UiWidget::Stepper` branch (`2656-2663`), however, calls `ImGui::DragScalar("##val", ImGuiDataType_Double, &val, ...)` (`2656`) with **no** `std::isfinite(val)` check. `DragScalar` allows `Ctrl+Click` text input (`TempInputScalar`), where `DataTypeApplyFromText` (`imgui_widgets.cpp:2380-2387`) runs `sscanf(buf, "%lf", &val)` and accepts `"nan"` or `"inf"`.

  Because `DataTypeClampT` (`imgui_widgets.cpp:2438-2439`) and `FactoryNodeEditor.cpp:2659-2660` compare `*v < min` and `*v > max`, both comparisons evaluate `false` for `NaN` (even when `min`/`max` bounds and `ImGuiSliderFlags_AlwaysClamp` are present), and `Inf` also passes when either bound is absent (`minPtr == nullptr`, `2632-2633`). `trackContinuousScalar` then stores `NaN`/`Inf` into `node->params[pdef.id]` and pushes `ChangeNodeParamCommand(..., NaN)` onto the undo stack. `NodeEvaluator.cpp:412-415` emits a persistent `WARNING` (`"Invalid stored value..., using default"`) and substitutes `defVal` in `eval->resolved_params` (hiding the corrupted stored value in the UI), while `node->params[pdef.id]` remains `NaN`/`Inf`. On save (`FactoryGraph.cpp:939` -> `ParamValue::toJson()` -> `dump_float`), `nlohmann::json` serializes non-finite doubles as JSON `null`, which `ParamValue::fromJson()` reads back as `ParamValue::null()`, losing the original numeric value across save/reload.
- **Reproduction / failing test idea:**
  Create a node with a `Number` parameter configured with `"ui": { "widget": "stepper" }` (even with finite `"min": 50, "max": 250`). `Ctrl+Click` the middle `DragScalar` widget (`FactoryNodeEditor.cpp:2656`), type `"nan"`, and press `Enter`. Verify `node->params[paramId].asNumber()` is `std::isnan(...) == true`, the node displays `[WARN]` (`"Invalid stored value..., using default"`), and `graph.serialize()` outputs `null` for that parameter under `"params"`.
- **Confidence:** High
- **Status:** fixed

