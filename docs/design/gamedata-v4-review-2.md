# GameData v4 Specification — Adversarial Review (Iteration 2)

**Reviewed document:** [`docs/design/gamedata-v4-spec.md`](file:///home/hagoel/Projects/factory-planner/docs/design/gamedata-v4-spec.md) (post-R-01..R-18 revision)  
**Reference inputs:** [`docs/ROADMAP.md`](file:///home/hagoel/Projects/factory-planner/docs/ROADMAP.md), [`docs/design/mechanics-matrix.md`](file:///home/hagoel/Projects/factory-planner/docs/design/mechanics-matrix.md), [`docs/design/gamedata-v4-review.md`](file:///home/hagoel/Projects/factory-planner/docs/design/gamedata-v4-review.md), [`docs/solver.md`](file:///home/hagoel/Projects/factory-planner/docs/solver.md), [`src/core/graph/FactorySolver.cpp`](file:///home/hagoel/Projects/factory-planner/src/core/graph/FactorySolver.cpp), [`src/core/graph/FactoryGraph.cpp`](file:///home/hagoel/Projects/factory-planner/src/core/graph/FactoryGraph.cpp)

---

## 1. Findings Ranked by Severity (with Concrete Fixes)

### Summary Index

| ID | Severity | Category | Title |
|---|---|---|---|
| **R2-01** | **CRITICAL** | Mechanics (M45) / Evaluation | Recipe-port `resource_param` with `port.*` in `allowed_if` (§2.8, §4.5, §5.4) is broken by Step 1 vs. Step 5 evaluation order, unbound on fixed-`res` recipes, and missing `has_tag()` |
| **R2-02** | **CRITICAL** | Solver / Topology | Sink classification with unwired active `extra_ports` (§7.4) contradicts Export detection ($\mathcal{X}_K$) and breaks the Sink-Dedicated Subgraph ($\mathcal{S}^+_K$) |
| **R2-03** | **HIGH** | Evaluation / Wire State | Evaluating `resource_expr` before `enabled` (§4.5 Steps 5–6) crashes guarded ports, and `INVALID` vs. `INACTIVE` wire states (§7.4) contradict each other |
| **R2-04** | **HIGH** | Serialization / Graph | Extra-port slot key `x:<id>` (§4.3, §8.1) loses port direction (`in` vs. `out`) on unresolved nodes/ports, and `deserializeConnection` drops `INVALID` / unresolved wires |
| **R2-05** | **HIGH** | Evaluation / Parameters | Binding a hidden `allow_none: false` parameter with no literal `default` to `null` (§3.1, §4.5 Step 1) crashes Step 2 (`let`) with a runtime `ERROR` |
| **R2-06** | **HIGH** | Mechanics (M42) / Types | Mutual exclusion of `res` vs. `resource_param`/`resource_expr` on recipe ports (§2.8) breaks optional input replacements, loses static base resources, and hits `if(bool, resource, string)` type errors |
| **R2-07** | **HIGH** | Validation / Cascade | Missing cascade stages (`attributes`, `resource_kinds`), 40k secondary error flooding from 1 profile error, `V-MET-1` dropping entire profiles, and unvalidated `resource_choice`/`slots` defaults |
| **R2-08** | **MEDIUM** | Validation / `resource_expr` | Validating all string literals in `resource_expr` as resource IDs (§5.7, `V-REF-1`) rejects `""` (inactive sentinel) and string comparisons, while missing string attributes (`burnt_result`) |
| **R2-09** | **MEDIUM** | Expression Language | Ambiguities in floating-point `%`, `resource == resource` equality, `is_set(global.x)`, array `lookup()` index rounding, and `.id`/`.key` attribute shadowing |
| **R2-10** | **MEDIUM** | Overrides & Inheritance | `overrides.let` can change a variable's type and break profile formulas at runtime, `overrides.metrics` scope is ambiguous, and `extra_ports` inheritance order is unspecified |
| **R2-11** | **MEDIUM** | Globals / Metrics / Graph | Global errors/warnings infect unrelated nodes, `aggregate: "min"`/`"max"` is corrupted by `0` contributions from disabled/error nodes, post-solve runtime errors are undefined, and `changeMachine` omits `node.params` cleanup |
| **R2-12** | **MEDIUM** | 50k Performance / Loader | Per-machine vs. per-profile `V-OVR-2` validation, `nlohmann::json` duplicate-key callback overwriting (`V-ID-2`), nested `/attributes` shard merging, and `include` path deduplication |
| **R2-13** | **LOW** | Defaults & Warnings | `default.input_amount` ignores `port.chance`, `V-W-3` triggers false positives on GTNH multi-slot outputs, `V-W-2` misses `setup`/`extra_ports` resources, and Example 10.2 allows negative `oc` when `time_raw < 1` |

---

### Detailed Findings & Suggested Fixes

#### R2-01 · CRITICAL · Recipe-port `resource_param` with `port.*` in `allowed_if` (§2.8, §4.5, §5.4) is broken by Step 1 vs. Step 5 evaluation order, unbound on fixed-`res` recipes, and missing `has_tag()`
- **Where:** Spec §2.8 (lines 196, 200), §3.1 (lines 252–253), §4.5 (Steps 1 & 5, lines 409–415), §5.3 (line 467), §5.4 (line 479), §5.6.
- **What goes wrong:**
  To support **M45** (Against the Storm ingredient category slots & GTNH OreDict slots), Revision 1 added `resource_param` to recipe ports (§2.8) and added `port.* (only when allowed_if is evaluated for a recipe port's resource_param)` to the `static` context (§5.4 line 479). Trying to express a multi-slot building (e.g. an Against the Storm *Cookhouse* running *Skewers* with category choices on `in:0` and `in:1`, and *Porridge* with a category choice on `in:0` and fixed `"res": "drizzle_water"` on `in:1`) exposes three fatal flaws:
  1. **Unbound `port.*` in §4.5 Step 1:** Node parameters (`P.params = ["slot_0", "slot_1"]`) are resolved in **§4.5 Step 1**, whereas recipe ports are not evaluated until **§4.5 Step 5**. When the node is set to *Porridge* (where `in:1` is fixed `"res": "drizzle_water"` and does **not** use `"resource_param": "slot_1"`), Step 1 still evaluates `slot_1.allowed_if` (which references `port.tag`), **with no recipe port bound to `slot_1`**! Furthermore, because `port.*` is not allowed in `visible_if` (§5.4 line 479), `slot_1` remains visible in the UI on *Porridge* even though no port uses it. And if two ports on the same recipe reference the same `resource_param`, `port.*` in Step 1 is ambiguous.
  2. **No `has_tag()` function or `.tags` access in the expression language:** Resources declare `"tags": ["ore", ...]` (§2.6), and a resource like `"mushrooms"` belongs to multiple ingredient categories (`"skewer_meat"`, `"porridge_grain"`, `"pie_filling"`). However, `resource_choice` (§3.1 line 253) only accepts a single static `"tag"` string on the parameter definition, `resource` (§5.3 line 467) has no `.tags` field, and §5.6 has no `has_tag(resource, string)` function! Consequently, `allowed_if` on `slot_0` **cannot** test whether candidate `option` carries the recipe port's tag (`port.tag`).
  3. **Compile-time context ambiguity (`V-EXPR-3`):** When compiling `parameters.<id>.allowed_if` at load time, the parameter definition in `"parameters"` does not declare whether it is a recipe-port `resource_param`, so the compiler does not know whether `port.*` is legal in `allowed_if` until checking `V-PROF-4` / `V-OVR-2`.
- **Suggested Fix:**
  1. Add `has_tag(resource, tag_str) -> bool` to §5.6 Built-in functions (returning `true` iff the resource's `tags` list contains `tag_str`).
  2. In §3.1, §4.5 Step 1, §5.4, and `V-PROF-4`: allow `port.*` (excluding `port.resource`) in both `visible_if` and `allowed_if` of a `resource_choice` parameter. Define any `resource_choice` parameter `P` referenced by a recipe port's `resource_param` (or whose `visible_if`/`allowed_if` references `port.*`) as **port-bound**:
     - Enforce in `V-PROF-4` that at most **one** port per recipe may specify `"resource_param": "<P>"`, and that any parameter whose `visible_if`/`allowed_if` uses `port.*` is only used as a recipe-port `resource_param`.
     - In §4.5 Step 1 (and UI rendering), when resolving a port-bound parameter `P`: look up the unique recipe port on the current recipe with `"resource_param": "<P>"`. If **no** port on the current recipe references `P`, automatically treat `P` as **hidden** (`visible_if = false`, binding `P` to its fallback default without evaluating `visible_if` or `allowed_if`). Otherwise, bind `port.*` to that recipe port during `P`'s `visible_if` and `allowed_if` evaluation.

---

#### R2-02 · CRITICAL · Sink classification with unwired active `extra_ports` (§7.4) contradicts Export detection ($\mathcal{X}_K$) and breaks the Sink-Dedicated Subgraph ($\mathcal{S}^+_K$)
- **Where:** Spec §0 R1 (line 20), §7.2 (line 620), §7.4 (line 649) vs. [`FactorySolver::analyzeComponent`](file:///home/hagoel/Projects/factory-planner/src/core/graph/FactorySolver.cpp#L227-L298).
- **What goes wrong:**
  §7.4 line 649 defines a node $n$ as a **sink** ($n \in \mathcal{S}_K$) iff it has *"zero active recipe output ports (`out:*` with `active == true && amount_per_craft > 1e-9`) and zero active extra output ports (`x:*`) that have an `OK` outgoing wire"*.
  Consider a Burner Incinerator / Waste Boiler node $n$ (`recipe.in = [["waste", 1]]`, `recipe.out = []`) that has an active extra output port `x:ash` (`dir: "out"`, `active == true`, `amount_per_craft > 1e-9`) which is currently **unwired** ($W^+(\text{x:ash}) = \emptyset$):
  1. By §7.4 line 649, because `x:ash` has no `OK` outgoing wire, node $n$ is placed in **`comp.sink_nodes` ($\mathcal{S}_K$)**.
  2. By §7.2 line 620, `x:ash` is an active output port in `comp.output_ports` ($\mathcal{P}^{\text{out}}_K$) with no outgoing `OK` wire. In `FactorySolver::analyzeComponent` (`FactorySolver.cpp` lines 246–252), every active output port with no outgoing wire is placed into **`comp.export_ports` ($\mathcal{X}_K$)**!
  3. Next, Step 2 of `AUTO` resolution (`FactorySolver.cpp` lines 270–298) computes the Sink-Dedicated Subgraph $\mathcal{S}^+_K$:
     $$B = \{\nu(p) \mid p \in \mathcal{X}_K \cup G^{\text{pre}}_K \cup P^{\text{push,expl}}_K\} \cup (\mathcal{N}_K \setminus R_{\text{sink}}), \qquad \mathcal{S}^+_K = R_{\text{sink}} \setminus \text{BackwardBFS}(B)$$
     Because $\text{x:ash} \in \mathcal{X}_K$ and $\nu(\text{x:ash}) = n$, **the sink node $n$ itself is placed in $B$**, which removes $n$ and every node upstream of $n$ from $\mathcal{S}^+_K$ ($\mathcal{S}^+_K = \emptyset$)!
  4. Because $n \notin \mathcal{S}^+_K$, Stage 2c.1 (Mixed-Splitter Sink-Branch Minimization) and Stage 3b (Minimal Sink-Dedicated Port Flow) fail to treat $n$ as a sink branch.
  5. Additionally, for nodes in `BLOCKED` or `ERROR` status (or nodes with zero active ports), all ports are inactive, so §7.4 lines 648–649 simultaneously classify them as both raw sources and sinks (`comp.sink_nodes`), and §7.2 line 620 states that inactive ports have `rate = 0.0` and `excess_rate = 0.0` written directly, but forgets to state that `node.machine_count` (and `node.crafts`) on `BLOCKED`/`ERROR`/zero-flow nodes must also be written as `0.0`.
- **Suggested Fix:**
  1. In §7.4 line 649, define a node as a **sink** in component $K$ iff it has **at least one active input port** and **zero active output ports of any kind** (`out:*` or `x:*` with `active == true && amount_per_craft > 1e-9`). This guarantees $\mathcal{S}_K \cap \{\nu(p) \mid p \in \mathcal{X}_K\} = \emptyset$.
  2. In §7.4 line 648, require a **raw source** to have **at least one active output port** (in addition to zero active recipe input ports and zero active extra input ports with an `OK` incoming wire).
  3. In §7.2 line 620, explicitly state that nodes in `BLOCKED` or `ERROR` status (or with no active port having $a > 10^{-9}$) are excluded from `comp.raw_ports` and `comp.sink_nodes` and have `node.machine_count = 0.0` and `node.crafts = 0.0` written directly.

---

#### R2-03 · HIGH · Evaluating `resource_expr` before `enabled` (§4.5 Steps 5–6) crashes guarded ports, and `INVALID` vs. `INACTIVE` wire states (§7.4) contradict each other
- **Where:** Spec §4.5 Steps 5 & 6 (lines 415–416), §5.3 (line 471), §5.4 (line 480), §5.5 (line 504), §7.4 (lines 641–643).
- **What goes wrong:**
  1. **Step 5 & 6 evaluation order bug (`resource_expr` before `enabled`):** In §4.5 Steps 5 and 6, the evaluator resolves the port's resource (`res`, `resource_param`, or `resource_expr`) **before** evaluating `enabled`. Suppose a burner profile defines an extra output port for ash/spent fuel (M14):
     ```json
     { "id": "spent", "dir": "out", "resource_expr": "param.fuel.burnt_result",
       "amount": "...", "enabled": "is_set(param.fuel)" }
     ```
     where `param.fuel` is a `resource_choice` with `allow_none: true` (`default: null`). When `param.fuel` is `null` (unset), Step 6 evaluates `resource_expr` (`param.fuel.burnt_result`) **before** checking `enabled` (`is_set(param.fuel)`). By §5.3 line 471, reading `.attr` on an unset `allow_none` parameter is a **runtime `ERROR`**, so the disabled extra port crashes the node into `ERROR` status!
  2. **Contradiction between `INACTIVE` and `INVALID` wire states (§7.4 lines 642–643):**
     - Line 642: `INACTIVE: at least one end is inactive.`
     - Line 643: `INVALID: both ends are active (or have resolved resources) and the resources differ.`
     Every port with a fixed `res`/`resource` (or a `resource_param`/`resource_expr` that resolved before `enabled` returned `false`) *has a resolved resource* even when `active == false`. Thus, a wire between an active `"water"` port and an inactive (`enabled == false`) `"cryofluid"` booster port matches **both** `INACTIVE` and `INVALID`!
  3. **`port.resource` in `resource_expr` (§5.4 vs. §5.5):** §5.4 line 480 puts `port.*` in scope for all recipe port formulas (`enabled`, `amount_expr`, `resource_expr`), whereas §5.5 line 504 states `port.resource` is the *resolved* port resource available only in `enabled` and `amount_expr` after resource resolution.
- **Suggested Fix:**
  1. In §4.5 Step 6 (`extra_ports`), evaluate `enabled` **first**; if `false`, mark `active = false` and `amount_per_craft = 0` without evaluating `resource_expr` or `amount`. In §4.5 Step 5 (recipe ports), forbid `port.resource` inside `resource_expr` (`V-EXPR-3`), and if `enabled` does not reference `port.resource`, evaluate `enabled` before `resource_expr` (or short-circuit `resource_expr` when `enabled` is `false`).
  2. In §7.4 lines 641–643, remove the contradictory parenthetical `(or have resolved resources)` so wire states are mutually exclusive:
     - `OK`: both ends are active (`active == true && amount_per_craft > 1e-9`) and their resolved resources match.
     - `INVALID`: both ends are active and their resolved resources differ.
     - `INACTIVE`: at least one end is inactive.

---

#### R2-04 · HIGH · Extra-port slot key `x:<id>` (§4.3, §8.1) loses port direction (`in` vs. `out`) on unresolved nodes/ports, and `deserializeConnection` drops `INVALID` / unresolved wires
- **Where:** Spec §4.3 (line 378), §7.4 (lines 643, 645), §8.1 (line 698), §8.2 (lines 721, 724) vs. [`FactoryGraph.cpp`](file:///home/hagoel/Projects/factory-planner/src/core/graph/FactoryGraph.cpp#L533-L570).
- **What goes wrong:**
  1. **Unresolved extra ports lose direction (`in` vs. `out`):** In §8.1, `"ports"` in the project file maps slot keys to port IDs (`{ "in:0": 40, "out:0": 41, "x:water": 42, "x:ash": 43 }`). While `"in:0"` and `"out:0"` encode whether the port belongs to `node.input_ports` or `node.output_ports`, §4.3 line 378 uses `"x:<id>"` for **both** input and output extra ports! When loading a project against degraded or modified data where a node's machine/profile is missing (§8.2 line 721) or a stored extra-port slot key was removed from the profile (§8.2 line 724: *"That port is created as an unresolved port"*), the loader **cannot determine whether `"x:ash"` or `"x:water"` is an input port or an output port**! Moreover, on a machine change (§7.4 line 645), if Profile A defines `{"id": "aux", "dir": "in"}` and Profile B defines `{"id": "aux", "dir": "out"}`, matching by `x:aux` preserves incoming wires on a port that has switched to an output port.
  2. **`FactoryGraph::deserializeConnection` drops `INVALID` and unresolved wires:** In `FactoryGraph.cpp` lines 555–560, `deserializeConnection` calls `isValidConnection(from_port, to_port)`, which rejects any wire where `from_port->resource_key != to_port->resource_key`. If a user saves a project containing an `INVALID` wire (§7.4: *"never deleted automatically"*) or loads a project with an unresolved node/port (`resource_key == ""`, §8.2: *"never loses work"*), `deserializeConnection` rejects and **silently deletes** the wire on load.
- **Suggested Fix:**
  1. In §4.3, §7.4, and §8.1, change extra-port slot keys from `x:<id>` to **`xin:<id>`** (when `dir == "in"`) and **`xout:<id>`** (when `dir == "out"`). Every slot key (`in:i`, `out:i`, `xin:id`, `xout:id`) then self-describes its direction.
  2. In §7.4 and §8.2, distinguish **interactive wiring (`addConnection`)**—which requires both ports to be active with matching non-empty resolved resources—from **project deserialization (`deserializeConnection`)**, which must accept any saved connection between an existing output port and an existing input port regardless of `resource_key` (running `evaluateNode()` on all nodes during load before computing wire `OK`/`INACTIVE`/`INVALID` states).

---

#### R2-05 · HIGH · Binding a hidden `allow_none: false` parameter with no literal `default` to `null` (§3.1, §4.5 Step 1) crashes Step 2 (`let`) with a runtime `ERROR`
- **Where:** Spec §3.1 (lines 247, 257, 262), §4.5 Step 1 (line 410), §8.1 (line 709).
- **What goes wrong:**
  1. **Hidden `allow_none: false` parameter becomes `null`:** By §3.1 line 247, `"default"` is optional for `enum` and `resource_choice` parameters even when `allow_none` is `false`. By §3.1 line 257 and §4.5 Step 1 (line 410), when `visible_if` is `false`, the parameter is *"bound directly to its declared `default` **(or `null` / empty slots if no literal default)** without evaluating `min`, `max`, `count`, or `allowed_if`"*.
     If a profile has an `enum` parameter `coil` (or `resource_choice` parameter `fuel`) with `allow_none: false`, no literal `"default"`, and `"visible_if": "machine.uses_coils"`, then on a machine with `uses_coils == false`, Step 1 binds `param.coil` to **`null`**! In Step 2, any `let` expression reading `param.coil.heat` (relying on `allow_none: false` guaranteeing a non-null option) hits `null` and fails with a **runtime `ERROR`** (§5.7 line 551)!
  2. **Project file encoding violation (§8.1 line 709):** §8.1 requires `node.params` to serialize every parameter of the profile (including hidden ones), and only permits `null` for `allow_none` parameters.
  3. **False `WARNING` on newly created nodes (§3.1 line 262):** When a parameter declares a static `"default": "coal"` and `allowed_if: "option.fuel_cat == machine.fuel_cat"`, creating a new `biochamber` node (`fuel_cat == "nutrients"`) initializes `node.params["fuel"] = "coal"`, which fails `allowed_if`, falls back to `"nutrients"`, and triggers a permanent node `WARNING` (§3.1 line 262: *"If a stored value had to fall back, the node receives a WARNING"*) before the user has even touched the node!
- **Suggested Fix:**
  1. In §3.1 line 257 and §4.5 Step 1: when `visible_if` is `false` and no literal `default` is declared on an `enum` or `resource_choice` parameter with `allow_none: false`, bind it to the **first option in `option_set`** (or the **first candidate resource in declaration order**), and bind to `null` only when `allow_none: true`. (Since `V-PAR-3` and `V-REF-5` guarantee non-empty candidates at load time, this requires no expression evaluation and guarantees `allow_none: false` parameters are never `null`.)
  2. In §7.4 / §7.5: when initializing `node.params` on `addNode()`, `changeMachine()`, or `setNodeRecipe()`, initialize newly added parameters to their resolved Step 1 value for the node's `(machine, recipe)` so newly created nodes start with a valid allowed value and no fallback `WARNING`.

---

#### R2-06 · HIGH · Mutual exclusion of `res` vs. `resource_param`/`resource_expr` on recipe ports (§2.8) breaks optional input replacements (M42), loses static base resources, and hits `if(bool, resource, string)` type errors
- **Where:** Spec §2.8 (line 196), §4.1 (line 340), §5.3 (lines 465–467), §5.5 (line 499), §5.6 (line 519).
- **What goes wrong:**
  In §2.8 line 196, a long-form recipe port must specify **exactly one** of `res`, `resource_param`, or `resource_expr`. Consider **M42 (Anno 1800 Specialist Input Replacements)**, where a Sewing Machine recipe normally inputs `"steel"` on `in:0`, and an optional Trade Union specialist parameter (`param.input_override`, `type: "resource_choice"`, `allow_none: true`, `default: null`) replaces `"steel"` with `"iron-ore"` when set:
  1. Because `res` and `resource_param` are mutually exclusive, setting `"resource_param": "input_override"` on `in:0` marks `in:0` **inactive** (`active = false`, `amount = 0`) whenever `param.input_override` is `null` (the default!) instead of falling back to `"steel"`.
  2. If the author instead uses `"resource_expr"` on `in:0`, writing `"if(is_set(param.input_override), param.input_override, 'steel')"` fails at compile time with **`V-EXPR-4`** because `param.input_override` has type `resource` while `'steel'` has type `string`, and `if(bool, T, T)` requires identical branch types (§5.6 line 519).
  3. Worse, omitting `"res": "steel"` to use `"resource_expr"` strips the static base resource from the recipe port: `recipe.input_items_total` (§5.5 line 499, precomputed at load time from static port resources) and static recipe-by-resource queries (Data Browser in Step 4, `Generate` in Step 7) can no longer see that the Sewing Machine recipe consumes `"steel"`.
- **Suggested Fix:**
  1. In §2.8 and `V-PROF-4`, allow `"res"` (the static base resource) to be specified **alongside** `"resource_param"` or `"resource_expr"`, exposed in `port.*` (§5.5) as `port.base_resource` (`resource`). When `"res"` is present alongside `"resource_param"` and the parameter is `null` (`unset`), the port resolves to `"res"` instead of becoming inactive.
  2. In §5.5 line 499, define `recipe.input_items_total`, `recipe.input_fluids_total`, `recipe.output_items_total`, and `recipe.output_fluids_total` in pre-solve / extra-port contexts as summing over the resolved recipe port resources (falling back to `res` in static contexts).

---

#### R2-07 · HIGH · Validation Cascade (§9.1, §9.2): Missing cascade stages (`attributes`, `resource_kinds`), 40k secondary error flooding from 1 profile error, `V-MET-1` dropping entire profiles, and unvalidated `resource_choice`/`slots` defaults
- **Where:** Spec §9.1 (lines 731–749), §9.2 (`V-REF-1`, `V-REF-3`, `V-REF-4`, `V-REF-5`, `V-ATTR-2`, `V-PAR-1`, `V-PAR-3`, `V-MET-1`).
- **What goes wrong:**
  1. **1 profile error floods `LoadReport.errors` with 40,000+ secondary cascade errors:** If `profiles/gt_single` has 1 typo (`V-EXPR-2`), and the validator also records `V-REF-4` errors on all 50 machines using `gt_single` and `V-REF-3` errors on all 40,000 GTNH recipes using those machines into `LoadReport.errors`, the error dialog reports **40,051 errors**, burying the 1 root cause!
  2. **`attributes` and `resource_kinds` are missing from the §9.1 cascade diagram:** `V-ATTR-2` can drop an attribute declaration in `/attributes` and `V-STRUCT-2` can drop an entry in `/resource_kinds`, but neither appears in the §9.1 cascade.
  3. **Dropping a broken `Metric` (`V-MET-1`) drops all `Profiles`, `Machines`, and `Recipes`:** In §9.1 (`Metrics -> Profiles -> Machines -> Recipes`), if a display metric has an error (e.g. `V-MET-1`: `split_sign: true` with `aggregate: "max"`) and the user clicks **"Load anyway"**, cascading `Metrics -> Profiles` drops every profile that references that metric, wiping out all machines and recipes in the package!
  4. **Unvalidated `resource_choice` and `slots` defaults (`V-PAR-1`, `V-PAR-3`, `V-REF-5`):** No rule in §9.2 checks that:
     - a `resource_choice` parameter specifies **exactly one** of `resources` or `tag`,
     - `resource_choice.default` (when non-null) is a valid resource ID belonging to the parameter's candidate `resources` list or carrying its `tag`,
     - `default: null` on `enum` or `resource_choice` is rejected when `allow_none` is `false`, or
     - `slots.default` (when provided) is an array of valid option keys of `option_set` (or `null` when `allow_empty: true`).
- **Suggested Fix:**
  1. In §9.1, run validation in stage order (`resource_kinds` & `attributes` $\to$ `resources` $\to$ `option_sets` & `tables` $\to$ `parameters` & `globals` $\to$ `metrics` $\to$ `profiles` $\to$ `machines` $\to$ `recipes`) and **suppress secondary reference errors** (`V-REF-*`, `V-EXPR-2`, `V-EXPR-5`, `V-ATTR-1`) when the referenced target entity already failed in an earlier stage; record cascade-dropped entities only in `LoadReport.dropped`.
  2. In §9.1, when a `Metric` is dropped under "Load anyway", remove only that metric entry from `profile.metrics` and `recipe.overrides.metrics` instead of dropping the profile.
  3. Extend `V-PAR-1`, `V-PAR-3`, and `V-REF-5` to check `resource_choice` (`exactly one of resources or tag`, and `default` must belong to the candidate set), `slots.default` elements, and `default: null` only when `allow_none: true`.

---

#### R2-08 · MEDIUM · Validating all string literals in `resource_expr` as resource IDs (§5.7, `V-REF-1`) rejects `""` (inactive sentinel) and string comparisons, while missing string attributes (`burnt_result`)
- **Where:** Spec §2.5 (line 147), §2.8 (line 196), §4.3 (line 380), §5.7 (line 541), §9.2 (`V-REF-1`, `V-W-2`).
- **What goes wrong:**
  1. **False-positive `V-REF-1` errors on `resource_expr` string literals:** §5.7 line 541 says compile-time checks reject an *"unknown option key or resource id in a comparison or `resource_expr` string literal"*. In a `resource_expr` like `"if(machine.fuel_cat == 'chemical', 'coal', '')"` or `"if(lookup('my_table', 'key') > 0, 'water', '')"` :
     - `'chemical'`, `'my_table'`, and `'key'` are string literals inside `resource_expr` that are **not** resource IDs, and
     - `''` (empty string) is the documented sentinel in §2.8 and §4.3 to mark the port inactive!
     Checking every string literal inside `resource_expr` against `resources` rejects valid expressions at load time.
  2. **Untyped resource-ID attributes (`param.fuel.burnt_result`, `machine.booster_res`):** When `resource_expr` reads a string attribute (as recommended in §4.3 line 380: `"param.fuel.burnt_result"` or `"machine.booster_res"`), the attribute is declared as `"type": "string"` in `/attributes`. Consequently, a typo like `"burnt_result": "ash_typo"` on a fuel resource is **not** caught at load time by `V-REF-1` (failing only at runtime when selected), and a resource like `"ash"` referenced only via `"burnt_result"` triggers a false `V-W-2` ("resource used by no recipe") warning.
- **Suggested Fix:**
  1. Specify in §5.7 and `V-REF-1` that in `resource_expr`, only **non-empty (`!= ""`) string literals in result position** (the root expression or the 2nd/3rd arguments of `if()`, recursively) and string literals compared via `==`/`!=` against a `resource`-typed operand are checked as resource IDs at load time.
  2. Add `"resource_id"` (or allow `V-REF-1` / `V-W-2` to inspect attributes referenced by `resource_expr`) in §2.5 so resource-reference attributes (`burnt_result`, `booster_res`) are validated at load time (allowing `""` or a valid resource ID) and counted by `V-W-2`.

---

#### R2-09 · MEDIUM · Expression Language (§5.2, §5.3, §5.5, §5.6): Ambiguities in floating-point `%`, `resource == resource` equality, `is_set(global.x)`, array `lookup()` index rounding, and `.id`/`.key` attribute shadowing
- **Where:** Spec §2.5 (line 148), §3.1 (line 260), §3.2 (line 268), §3.3 (line 297), §5.2 (line 444), §5.3 (lines 465–467), §5.5, §5.6 (lines 520, 523).
- **What goes wrong:**
  1. **Floating-point `%` semantics unspecified (§5.2 line 444):** The grammar defines `%` on `number` (`double`), but §5 never specifies whether `a % b` is truncating `std::fmod(a, b)` or floored modulo `a - b * std::floor(a / b)`.
  2. **Inconsistent `==` / `!=` rules for `resource` and `option<S>` (§5.3 lines 465–467):** In §5.3, the `string` row allows `== / != with string, option or resource`, whereas the `resource` row allows only `== / != with a string literal (a resource id, checked at load)`—forbidding `resource == resource` (e.g. `port.resource == param.alt_res`) and contradicting the `string` row when comparing against a non-literal `string` attribute.
  3. **`is_set()` forbids `global.x` (§5.6 line 523):** Globals (§3.2) use the same parameter types (`allow_none` `enum`/`resource_choice` and `slots`), but `is_set(param)` requires a bare `param.x` and forbids `is_set(global.x)`. Also, calling `is_set` on an `enum`/`resource_choice` with `allow_none: false` is unspecified.
  4. **Array `lookup()` index truncation trap (§5.6 line 520):** `lookup(table, key)` checks that `key` is *"integral within 1e-9"*. If `key` evaluates to `1.9999999995`, `static_cast<int>(key)` truncates to index `1` instead of `2` unless `std::llround(key)` is used.
  5. **`.id` on `resource` and `.key` on `option<S>` missing from §5.5 (`Built-in fields`):** §5.3 line 467 defines `.id` on `resource`, but §5.5 omits `resource.id`, so `V-ATTR-2` (which checks against §5.5) does not forbid declaring a resource attribute named `id`; and `option_sets.<set>.attrs` has no `V-ATTR-2` check preventing an option attribute named `id` or `key`.
  6. **Dynamic `min`/`max` on `int` parameters (§3.1 line 260):** If a dynamic `min` or `max` expression on an `int` parameter evaluates to non-integers (or `min > max`), clamping `param.x` to `[min, max]` produces a non-integral value on an `int` parameter.
- **Suggested Fix:**
  1. Define `a % b` in §5.2 as `std::fmod(a, b)`.
  2. In §5.3, allow `==` / `!=` between two `resource` values; clarify that comparing `option<S>` or `resource` with a **string literal** validates the option key or resource ID at load time (`V-EXPR-2`), while comparing with a non-literal `string` expression compares the option key / resource ID string at runtime.
  3. In §5.6, allow `is_set(param.x)` and `is_set(global.x)` (returning `true` when `allow_none: false` on `enum`/`resource_choice`), and specify `std::llround(key)` after checking `std::abs(key - std::round(key)) <= 1e-9` for array `lookup()`.
  4. List `resource.id` (and `option.key`) in §5.5, and extend `V-ATTR-1`/`V-ATTR-2` to cover `option_sets.<set>.attrs` and `game.attrs`.
  5. In §3.1 / §4.5 Step 1, for `int` parameters, require dynamic `min`/`max` to clamp to `[ceil(min - 1e-9), floor(max + 1e-9)]`, and set node status `ERROR` if `min > max` or non-finite.

---

#### R2-10 · MEDIUM · Overrides & Inheritance (§4.2, §4.4, §9.2): `overrides.let` can change a variable's type and break profile formulas at runtime, `overrides.metrics` scope is ambiguous, and `extra_ports` inheritance order is unspecified
- **Where:** Spec §4.2 (line 362), §4.4 (lines 399–401), §9.2 (`V-OVR-1`, `V-OVR-2`).
- **What goes wrong:**
  1. **`overrides.let` type mismatch:** In §4.4, a recipe can override an existing `let` entry of a profile. If `overrides.let.x` compiles to a `bool` or `string` while the profile's `let.x` was a `number`, checking only that the override expression compiles and is acyclic (`V-OVR-2`) allows the type change through, breaking the profile's `craft_time` or `output_amount` at runtime!
  2. **`overrides.metrics` vs. profile metrics:** §4.4 line 399 states: `let entries (by name; the name must already exist in the profile), metrics entries (by id)`. It is ambiguous whether `overrides.metrics.<id>` requires `<id>` to already exist in `profile.metrics`, or only in the top-level `"metrics"` registry (§6.1) so that recipes can attach recipe-specific metrics (e.g. Stationeers furnace pressure windows).
  3. **`extra_ports` merge order in §4.2:** `extra_ports` is an ordered array of objects, and its order determines Step 6 evaluation order (§4.5), reference port tie-breaking (§7.2), and UI pin order. §4.2 line 362 says `extra_ports` merges by `id` ("the child replaces entries with the same name and adds new ones") without stating whether a replaced entry keeps the parent's array position or moves to the child's position.
- **Suggested Fix:**
  1. In §4.4 and `V-OVR-2`, require `overrides.let.<name>` to have the **same result type** as `<name>` in the target profile.
  2. Clarify in §4.4 and `V-OVR-1` that `overrides.metrics.<id>` may reference any metric `<id>` declared in the top-level `"metrics"` registry (`V-PROF-3`), replacing the profile's entry if present or adding it for that recipe if absent.
  3. In §4.2, specify that merging `extra_ports` preserves the parent's position for replaced `id`s and appends new child `extra_ports` after them.

---

#### R2-11 · MEDIUM · Globals, Metrics Aggregation, Post-Solve Runtime Errors, and `node.params` on Machine/Recipe Change (§4.5, §6.2, §7.4, §7.6, §8.2)
- **Where:** Spec §3.2 (line 268), §4.5 Step 1 (line 409), §6.2 (line 591), §7.4 (lines 645–646), §7.6 (line 679), §8.2 (lines 722–723).
- **What goes wrong:**
  1. **Global warnings/errors infect unrelated nodes (§4.5 Step 1):** §4.5 Step 1 resolves `global.*` as part of every node's evaluation. If a global has an out-of-range clamped value (`WARNING`) or fails `allowed_if` (`BLOCKED`/`ERROR`), running Step 1 inside every node puts **every node in the graph** into `WARNING`/`BLOCKED`/`ERROR`—even nodes whose profile and recipe never reference that global! Additionally, §8.2 specifies `unresolved_params` on `Node` for unknown node parameters in a project file, but omits `unresolved_globals` at the project level.
  2. **`aggregate: "min"` and `"max"` are corrupted by `0` contributions (§6.2 line 591):** §6.2 states: *"Nodes in `ERROR`/`BLOCKED` (or where the metric's `enabled` is `false`) contribute 0."* For `sum`, `0` is neutral; for **`min`** (or `max` on negative values), contributing `0` forces the graph minimum to `0` whenever any node is `BLOCKED` or has `enabled == false`!
  3. **Post-solve `value` metric runtime errors (§7.6):** §5.7 defines runtime errors as locking all ports of the node to `0`. However, post-solve `value` metrics (§7.6) run *after* the LP has solved, so if a `value` metric expression divides by zero (e.g. when `node.machines == 0`), it cannot retroactively zero the LP ports.
  4. **`node.params` on `changeMachine` / `setNodeRecipe` (§7.4, §8.1):** §7.4 specifies how ports and wires behave when changing a node's machine or recipe, but omits how `node.params` is updated to satisfy R5 (§8.1: *"`params` stores every parameter of the node's profile"*).
- **Suggested Fix:**
  1. Resolve `globals` once per project before node evaluation; propagate a global's `WARNING`/`BLOCKED`/`ERROR` status only to nodes whose evaluated expressions actually reference that global, and keep unknown project globals in `unresolved_globals` (§8.2).
  2. In §6.2, state that nodes in `ERROR`/`BLOCKED` or where `enabled == false` are **excluded** from metric aggregation (so they do not inject `0` into `min`/`max`; if zero nodes contribute to a metric, `sum` is `0` and `min`/`max` are omitted).
  3. In §7.6, state that a runtime error in a post-solve `value` metric sets that metric's value on the node to `ERROR` (excluded from totals) and adds a message to the node without altering solved port/wire flows.
  4. In §7.4, specify that `changeMachine` and `setNodeRecipe` preserve `node.params` entries that exist in the new profile, initialize new profile parameters to their Step 1 defaults, and remove parameters not in the new profile (storing the previous `node.params` in the Undo command).

---

#### R2-12 · MEDIUM · 50k-Recipe Loader & Validator Details (§2.9, §9.2): Per-machine vs. per-profile `V-OVR-2` checks, `nlohmann::json` duplicate-key callback behavior (`V-ID-2`), nested `/attributes` shard merging, and `include` path deduplication
- **Where:** Spec §2.9 (lines 208–223), §9.2 (`V-ID-2`, `V-OVR-2`).
- **What goes wrong:**
  1. **Per-machine vs. per-profile recipe validation (`V-OVR-2`, `V-PROF-4`):** Because `attributes.machine` is declared globally (§2.5), the static types of all `machine.*` fields are identical across every machine in the package. Thus, compiling and validating recipe overrides and port formulas depends **only on the distinct `profile_id`s** of the recipe's compatible machines, not on individual machines. Pre-indexing `category -> unique_profile_ids` avoids repeating `V-OVR-2` and `V-PROF-4` checks across 50 machines that share the same profile.
  2. **`nlohmann::json` parser callback behavior on duplicate keys (`V-ID-2`):** §9.2 line 768 states that duplicate keys within a single JSON object are checked via a JSON parser callback and **"Drops: the second definition"**. In `nlohmann::json::parse(input, cb)`, `parser_callback_t` does not receive the parent `json::object_t`, and if the callback returns `true` on a duplicate `key` event, `nlohmann::json` **overwrites the first value with the second value**! To drop the second definition, the callback must maintain a stack of key sets per object depth and return `false` on duplicate `key` events.
  3. **Merging `/attributes` across shards (§2.9 line 213):** `/attributes` is a 2-level map (`/attributes/<entity_kind>/<attr>`). Merging top-level map keys across shards would treat `/attributes/machine` in two shards as a duplicate key (`V-ID-2`) unless `/attributes` is merged at the 2nd level (`<entity_kind>.<attr>`).
  4. **Directory detection and duplicate shard paths in `include` (§2.9 line 211):** Requiring a trailing `/` in JSON to identify a directory (instead of `std::filesystem::is_directory`) is error-prone for hand authors, and overlapping `include` entries (e.g. `["recipes/a.json", "recipes/"]` or `"."`) will load the same file twice (or re-load `game.json`) and emit thousands of `V-ID-2` errors unless canonical shard paths are deduplicated and `game.json` is skipped.
- **Suggested Fix:**
  Update §2.9 and `V-ID-2` to specify: (a) `category -> unique_profile_ids` indexing for `V-OVR-2`/`V-PROF-4`, (b) a per-depth key-set stack in the `nlohmann::json` parser callback that returns `false` on duplicate keys, (c) 2nd-level merging for `/attributes/<entity_kind>/<attr>`, and (d) `std::filesystem::is_directory` + canonical path deduplication (excluding `game.json`) for `include`.

---

#### R2-13 · LOW · Default Profile & Warnings (§4.1, §9.2, §10.2): `default.input_amount` ignores `port.chance`, `V-W-3` false positives on GTNH multi-slot outputs, `V-W-2` misses `setup`/`extra_ports` resources, and Example 10.2 allows negative `oc` when `time_raw < 1`
- **Where:** Spec §2.8 (line 187), §4.1 (line 352), §9.2 (`V-W-2`, `V-W-3`, lines 800–801), §10.2 (line 924).
- **What goes wrong:**
  1. **`default.input_amount` ignores `port.chance` (§4.1 line 352):** §2.8 allows `[resource, amount, chance]` on both `in` and `out` ports (used for probabilistic catalyst/fluid inputs in GTNH and Pyanodons), and `port.chance` defaults to `1`. However, the built-in `default` profile sets `"input_amount": "port.amount"` instead of `"port.amount * port.chance"`, silently ignoring `chance` on input ports.
  2. **`V-W-3` false positives on GTNH / Create multi-slot outputs (§9.2 line 801):** In GTNH (Macerator, Sifter, Centrifuge) and Create (Crushing Wheels), a single recipe often outputs the same resource in two slots with different `chance` or `min_tier` (e.g. `1x dust (100%)` + `1x dust (25% chance, min_tier: 3)`). `V-W-3` (*"Same resource twice in one recipe's `in` (or `out`)"*) will trigger thousands of false warnings unless restricted to ports with identical conditions/chances.
  3. **`V-W-2` false positives on setup/catalyst resources (§9.2 line 800):** GTNH Programmed Circuits (`1..24`), molds, and lenses are only referenced in `recipe.setup` (never in `recipe.in`/`out`). If `V-W-2` (*"resource used by no recipe"*) only checks `in`/`out`, all setup items and `extra_ports`-only resources will trigger false `V-W-2` warnings.
  4. **Negative `oc` when `recipe.time_raw < 1` in Example 10.2 (§10.2 line 924):** `"oc": "min(oc_v, floor(log(recipe.time_raw, 2) + 1e-9))"` omits `max(0, ...)` around the second argument; if `time_raw < 1`, `oc` becomes negative.
- **Suggested Fix:**
  1. In §4.1, change `default.input_amount` to `"port.amount * port.chance"`.
  2. In `V-W-2`, count resources referenced by `recipe.in`, `recipe.out`, `recipe.setup`, `extra_ports`, and `parameters`; in `V-W-3`, warn only when two ports in `in` (or `out`) share the same static `res` and identical `chance`, `catalyst`, `attrs`, `enabled`, and `amount_expr`.
  3. In §10.2 line 924, write `"oc": "min(oc_v, max(0, floor(log(recipe.time_raw, 2) + 1e-9)))"`.

---

## 2. Task 1: Expressing Every Must-Have Mechanic from the Matrix

Below, every must-have mechanic from [`docs/design/mechanics-matrix.md` §2.1–§2.13](file:///home/hagoel/Projects/factory-planner/docs/design/mechanics-matrix.md#L91-L167) is tested against the **current** v4 spec (incorporating the Iteration 1 changes: single-quoted strings, numeric/bool literals for bounds and flags, `input_enabled`/`output_enabled`, `resource_expr`, `resource_param` on recipe ports, and conditional `enabled` on metrics).

### 2.1 Satisfactory

| Mechanic | Status | Notes |
|---|---|---|
| **M1, M2: Clock Speed ($1\dots 250\%$) & Power Shards** | **Expressible** | Covered by Example 10.1. |
| **M3: Somersloop Production Amplification** | **Expressible** | Covered by Example 10.1 (`amp = 1 + param.somersloops / machine.somersloop_slots`). |
| **M4, M5: Non-Linear Power & Variable-Power Recipes** | **Expressible** | M4 is in Example 10.1. M5 (Particle Accelerator / Converter / Quantum Encoder) uses recipe attributes `power_min` and `power_max` plus a `peak_power` metric (snippet below). |
| **M6: Miner Resource Node Purity & 1200/min Belt Cap** | **Expressible** | Covered via an `enum` parameter `purity` and belt-rate clamping in `craft_time` (snippet below). |

**Snippet — M5 (Variable-Power Recipes) & M6 (Miner Purity + 1200/min Belt Cap):**
```json
{
  "game": { "id": "satisfactory", "name": "Satisfactory", "data_version": "1.1.0.4",
            "attrs": { "max_belt_rate_s": 20.0 } },
  "attributes": {
    "machine": { "power": { "type": "number", "default": 0 },
                 "somersloop_slots": { "type": "number", "default": 0 } },
    "recipe":  { "power_min": { "type": "number", "default": 0 },
                 "power_max": { "type": "number", "default": 0 } }
  },
  "option_sets": {
    "purities": {
      "attrs": { "mult": { "type": "number", "default": 1 } },
      "options": [
        { "key": "impure", "label": "Impure", "attrs": { "mult": 0.5 } },
        { "key": "normal", "label": "Normal", "attrs": { "mult": 1.0 } },
        { "key": "pure",   "label": "Pure",   "attrs": { "mult": 2.0 } }
      ]
    }
  },
  "parameters": {
    "purity": { "label": "Node purity", "type": "enum", "option_set": "purities", "default": "normal",
                "ui": { "widget": "combo", "placement": "body" } }
  },
  "metrics": {
    "peak_power": { "label": "Peak power", "unit": "MW", "display": ["tooltip", "totals"],
                    "aggregate": "sum", "format": "%.2f" }
  },
  "profiles": {
    "sf_variable_production": {
      "extends": "sf_production",
      "let": {
        "avg_w": "if(recipe.power_max > 0, (recipe.power_min + recipe.power_max) / 2, machine.power)",
        "peak_w": "if(recipe.power_max > 0, recipe.power_max, machine.power)"
      },
      "metrics": {
        "power":      { "per_machine": "avg_w * amp ^ 2 * c ^ 1.321928" },
        "peak_power": { "per_machine": "peak_w * amp ^ 2 * c ^ 1.321928" }
      }
    },
    "sf_miner": {
      "params": ["clock", "purity"],
      "let": { "c": "param.clock / 100" },
      "craft_time": "max(recipe.time / (machine.speed * param.purity.mult * c), recipe.output_items_total / game.max_belt_rate_s)",
      "metrics": {
        "power":  { "per_machine": "machine.power * c ^ 1.321928" },
        "shards": { "value": "ceil(node.machines) * clamp(ceil((param.clock - 100) / 50), 0, 3)" }
      }
    }
  }
}
```

---

### 2.2 Factorio (2.0 & Space Age)

| Mechanic | Status | Notes |
|---|---|---|
| **M7: Machine Module Slots** | **Expressible** | Covered by Example 10.5 (`slots` parameter + `sum(param.modules.speed)`). |
| **M8: 2.0 Beacons ($\sqrt{n}$ Diminishing Returns & Beacon Quality)** | **Expressible** | Covered by Example 10.5; beacon quality adds a `beacon_quality` `enum` parameter (`1.5..2.5`). |
| **M9, M10: Infinite Productivity Research, $+300\%$ Cap & Catalyst Exclusion** | **Expressible (Verbose `if` chain)** | `port.catalyst` and `clamp(p, 0, 3)` work cleanly; per-recipe research levels use a nested `if(recipe.prod_tech == 'steel', global.prod_steel, ...)` chain in `let` (§12.1). |
| **M11: Quality Tiers & Cascading Quality Splits** | **Partial (Non-goal)** | Machine/beacon/module quality multipliers work via `enum`/`slots` options. 5-tier probabilistic output splitting is a documented non-goal (§11). |
| **M12, M13: Burner / Fluid Fuels & Spoilage Approximation** | **Expressible (Fix R2-03, R2-05)** | Expressible via `extra_ports` with `resource_param: "fuel"`; if shared with electric machines via `visible_if: "machine.is_burner"`, requires R2-05 so `param.fuel` does not bind to `null` or emit a fallback `WARNING` on machines with different `fuel_cat`. |

**Snippet — M8 (Beacon Quality), M9 (Per-Recipe Research), M12 (Spoilage), & M13 (Burner Fuel):**
```json
{
  "attributes": {
    "machine":  { "burner_eff": { "type": "number", "default": 1 },
                  "fuel_cat":   { "type": "string", "default": "chemical" } },
    "recipe":   { "prod_tech":  { "type": "string", "default": "" } },
    "resource": { "fuel_mj":    { "type": "number", "default": 0 },
                  "fuel_cat":   { "type": "string", "default": "" } }
  },
  "globals": {
    "prod_steel":   { "label": "Steel prod. level",   "type": "int", "default": 0, "min": 0, "max": 30 },
    "prod_plastic": { "label": "Plastic prod. level", "type": "int", "default": 0, "min": 0, "max": 30 },
    "prod_lds":     { "label": "LDS prod. level",     "type": "int", "default": 0, "min": 0, "max": 30 }
  },
  "option_sets": {
    "qualities": {
      "attrs": { "mult": { "type": "number", "default": 1.0 },
                 "beacon_eff": { "type": "number", "default": 1.5 } },
      "options": [
        { "key": "normal",    "label": "Normal",    "attrs": { "mult": 1.0, "beacon_eff": 1.5 } },
        { "key": "uncommon",  "label": "Uncommon",  "attrs": { "mult": 1.3, "beacon_eff": 1.7 } },
        { "key": "rare",      "label": "Rare",      "attrs": { "mult": 1.6, "beacon_eff": 1.9 } },
        { "key": "epic",      "label": "Epic",      "attrs": { "mult": 1.9, "beacon_eff": 2.1 } },
        { "key": "legendary", "label": "Legendary", "attrs": { "mult": 2.5, "beacon_eff": 2.5 } }
      ]
    }
  },
  "parameters": {
    "machine_quality": { "label": "Machine quality", "type": "enum", "option_set": "qualities", "default": "normal" },
    "beacon_quality":  { "label": "Beacon quality",  "type": "enum", "option_set": "qualities", "default": "normal",
                         "visible_if": "param.beacon_count > 0" },
    "fuel":            { "label": "Fuel", "type": "resource_choice", "tag": "fuel",
                         "allowed_if": "option.fuel_cat == machine.fuel_cat" },
    "freshness_pct":   { "label": "Freshness", "type": "number", "default": 100, "min": 1, "max": 100, "unit": "%" }
  },
  "profiles": {
    "fa_burner_crafter": {
      "params": ["machine_quality", "fuel"],
      "craft_time": "recipe.time / (machine.speed * param.machine_quality.mult)",
      "extra_ports": [
        { "id": "fuel", "dir": "in", "resource_param": "fuel",
          "amount": "(machine.power * craft_time) / (machine.burner_eff * param.fuel.fuel_mj)" }
      ]
    },
    "fa_space_age_crafter": {
      "extends": "fa_crafter",
      "params": ["machine_quality", "beacon_quality"],
      "let": {
        "bmul": "if(n > 0, param.beacon_quality.beacon_eff * sqrt(n), 0)",
        "tech_lvl": "if(recipe.prod_tech == 'steel', global.prod_steel, if(recipe.prod_tech == 'plastic', global.prod_plastic, if(recipe.prod_tech == 'lds', global.prod_lds, 0)))",
        "p": "machine.base_prod + sum(param.modules.prod) + 0.1 * tech_lvl",
        "speed": "max(0.2, 1 + s) * param.machine_quality.mult"
      }
    }
  }
}
```

---

### 2.3 Factorio Overhauls: Pyanodons & Seablock

| Mechanic | Status | Notes |
|---|---|---|
| **M13, M14: Burner Fuels with Ash / Spent Fuel Container Outputs** | **Expressible (Fix R2-03, R2-08)** | Uses `resource_expr: "param.fuel.burnt_result"` on the `spent` extra output port! Requires R2-03 so `enabled` guards `resource_expr` when `param.fuel` is unset, and R2-08 so `"burnt_result"` is validated at load time. |
| **M15: Tiered Steam & Fluid Temperatures** | **Expressible** | Discrete resources (`steam-165`, `steam-500`, `steam-1000`). |
| **M16: Plant & Animal Living Modules (PyAL)** | **Expressible** | `slots` parameter (or `int` count + `enum` tier for 30+ slot buildings) scaling `craft_time` and `output_amount`. |
| **M17: T.U.R.D. Global Tech Upgrades** | **Expressible** | Global `enum` per biological line (`global.turd_moss == 'branch_2'`) referenced in port `enabled` / `amount_expr` and recipe `overrides`. |

**Snippet — M13, M14 (Burner Fuel + Dynamic `burnt_result` Output), M16 (PyAL Modules), & M17 (T.U.R.D.):**
```json
{
  "attributes": {
    "resource": { "fuel_mj":      { "type": "number", "default": 0 },
                  "burnt_result": { "type": "string", "default": "" },
                  "burnt_ratio":  { "type": "number", "default": 1 } }
  },
  "resources": [
    { "id": "raw-coal", "name": "Raw Coal", "tags": ["fuel"],
      "attrs": { "fuel_mj": 3, "burnt_result": "ash", "burnt_ratio": 1 } },
    { "id": "tar-canister", "name": "Tar Canister", "tags": ["fuel"],
      "attrs": { "fuel_mj": 12, "burnt_result": "empty-canister", "burnt_ratio": 1 } },
    { "id": "syngas", "name": "Syngas", "kind": "fluid", "tags": ["fuel"],
      "attrs": { "fuel_mj": 2.5, "burnt_result": "", "burnt_ratio": 0 } }
  ],
  "profiles": {
    "py_burner": {
      "params": ["fuel"],
      "let": {
        "fuel_amt": "(machine.power * craft_time) / (machine.burner_eff * param.fuel.fuel_mj)"
      },
      "craft_time": "recipe.time / machine.speed",
      "extra_ports": [
        { "id": "fuel",  "dir": "in",  "resource_param": "fuel", "amount": "fuel_amt" },
        { "id": "spent", "dir": "out", "resource_expr": "param.fuel.burnt_result",
          "amount": "fuel_amt * param.fuel.burnt_ratio" }
      ]
    }
  }
}
```
*(Note: When `param.fuel` is `"syngas"`, `param.fuel.burnt_result` evaluates to `""`, which marks `x:spent` inactive per §4.3 / §4.5 Step 6.)*

---

### 2.4 Dyson Sphere Program

| Mechanic | Status | Notes |
|---|---|---|
| **M18: Proliferator Tier & Mode (`Extra Products` vs. `Speedup`)** | **Expressible** | Covered by Example 10.4. (Note: in DSP, fluid inputs like `water`/`refined-oil` are also proliferated on belts, so `spray.amount` should use `(recipe.input_items_total + recipe.input_fluids_total) / param.prolif.charges`.) |
| **Assembler / Smelter / Chemical Plant Tier Speeds** | **Expressible** | Covered by `machine.speed`. |
| **M19: Ray Receiver Modes & Graviton Lens Proliferation** | **Expressible** | Snippet below. |
| **M20: Fractionator Belt-Throughput Scaling** | **Expressible** | Snippet below. |

**Snippet — M19 (Ray Receiver) & M20 (Fractionator):**
```json
{
  "parameters": {
    "recv_eff":    { "label": "Receiving efficiency", "type": "number", "default": 100, "min": 30, "max": 100, "unit": "%" },
    "use_lens":    { "label": "Graviton lens", "type": "bool", "default": false },
    "lens_prolif": { "label": "Lens proliferator", "type": "resource_choice", "tag": "proliferator",
                     "allow_none": true, "default": null, "visible_if": "param.use_lens" },
    "belt_rate":   { "label": "Belt flow rate", "type": "number", "default": 30, "min": 1, "max": 120, "unit": "/s" }
  },
  "profiles": {
    "dsp_ray_receiver": {
      "params": ["recv_eff", "use_lens", "lens_prolif"],
      "let": {
        "eta": "param.recv_eff / 100",
        "lens_mult": "if(param.use_lens, 2 * (1 + if(is_set(param.lens_prolif), param.lens_prolif.speedup, 0)), 1)",
        "rate_mult": "((1 + eta ^ 2) / 2) * lens_mult"
      },
      "craft_time": "recipe.time / rate_mult",
      "extra_ports": [
        { "id": "lens", "dir": "in", "resource": "graviton-lens",
          "amount": "(0.1 / 60) * craft_time", "enabled": "param.use_lens" },
        { "id": "spray", "dir": "in", "resource_param": "lens_prolif",
          "amount": "((0.1 / 60) * craft_time) / param.lens_prolif.charges",
          "enabled": "param.use_lens && is_set(param.lens_prolif)" }
      ]
    },
    "dsp_fractionator": {
      "params": ["belt_rate", "prolif"],
      "let": {
        "on": "is_set(param.prolif)",
        "p_conv": "0.01 * (1 + if(on, param.prolif.speedup, 0))"
      },
      "craft_time": "1 / (param.belt_rate * p_conv)",
      "extra_ports": [
        { "id": "spray", "dir": "in", "resource_param": "prolif",
          "amount": "1 / param.prolif.charges", "enabled": "on" }
      ],
      "metrics": {
        "power": { "per_machine": "0.72 * max(1, param.belt_rate / 30) * if(on, param.prolif.power_mult, 1)" }
      }
    }
  }
}
```

---

### 2.5 Mindustry (Serpulo & Erekir)

| Mechanic | Status | Notes |
|---|---|---|
| **M21: Tile Attribute Efficiency Sum** | **Expressible** | `tile_eff` parameter (`default: 100`, `visible_if: "machine.uses_tiles"`). |
| **M22: Optional Liquid/Item Boosters** | **Expressible (with `resource_expr`)** | With `resource_expr: "machine.booster_res"` and updated R1 (§7.4), a single drill profile covers all booster fluids (`water`, `cryofluid`, `nitrogen`, `cyanogen`, `hydrogen`). |
| **M23: Overdrive Projector / Dome Speed Aura** | **Expressible** | `overdrive` `enum` parameter (`1.0`, `1.5`, `2.25`, `2.5`). |
| **M24: Erekir Heat Scaling (`HeatCrafter`)** | **Expressible** | `supplied_heat` parameter scaling `craft_time` when `machine.heat_req > 0`. |

**Snippet — M21–M24 (Unified Mindustry Drill & HeatCrafter Profiles):**
```json
{
  "attributes": {
    "machine": { "uses_tiles":     { "type": "bool",   "default": false },
                 "boost_mult":     { "type": "number", "default": 1.0 },
                 "booster_res":    { "type": "string", "default": "" },
                 "booster_rate_s": { "type": "number", "default": 0.0 },
                 "heat_req":       { "type": "number", "default": 0.0 },
                 "max_heat_eff":   { "type": "number", "default": 4.0 } }
  },
  "parameters": {
    "tile_eff":      { "label": "Tile efficiency", "type": "number", "default": 100, "min": 1, "max": 900, "unit": "%",
                       "visible_if": "machine.uses_tiles" },
    "boosted":       { "label": "Liquid boost", "type": "bool", "default": false,
                       "visible_if": "machine.boost_mult > 1" },
    "overdrive":     { "label": "Overdrive", "type": "enum", "option_set": "overdrives", "default": "none" },
    "supplied_heat": { "label": "Supplied heat", "type": "number", "default": 0, "min": 0, "max": 100,
                       "visible_if": "machine.heat_req > 0" }
  },
  "profiles": {
    "mindustry_drill": {
      "params": ["tile_eff", "boosted", "overdrive"],
      "let": {
        "eff": "(if(machine.uses_tiles, param.tile_eff / 100, 1)) * (if(param.boosted, machine.boost_mult, 1)) * param.overdrive.mult"
      },
      "craft_time": "recipe.time / (machine.speed * eff)",
      "extra_ports": [
        { "id": "booster", "dir": "in", "resource_expr": "machine.booster_res",
          "amount": "machine.booster_rate_s * craft_time", "enabled": "param.boosted" }
      ]
    },
    "mindustry_heat_crafter": {
      "params": ["supplied_heat", "overdrive"],
      "let": {
        "h_eff": "if(param.supplied_heat <= machine.heat_req, param.supplied_heat / machine.heat_req, min(machine.max_heat_eff, 1 + (param.supplied_heat - machine.heat_req) / machine.heat_req))"
      },
      "requires": [ { "expr": "param.supplied_heat > 0", "message": "Requires heat input" } ],
      "craft_time": "recipe.time / (machine.speed * h_eff * param.overdrive.mult)"
    }
  }
}
```

---

### 2.6 GregTech: New Horizons (GTNH)

| Mechanic | Status | Notes |
|---|---|---|
| **M25, M26, M27: Voltage Tiers, Imperfect/Perfect OC, Sub-Tick Parallels, Multi-Amp Hatches** | **Expressible** | Covered by Example 10.2 and the `gt_ebf` snippet below. |
| **M28: Coil Heat Gating, 900K EU Discounts & 1800K Perfect OCs** | **Expressible** | Cleanly expressed in `gt_ebf` below. |
| **M29, M30: Tier-Gated Byproducts & Tier-Boosted Chance Outputs** | **Expressible** | Covered by Example 10.2 (`port.min_tier <= t` and `port.chance + port.chance_boost * oc`). |
| **M31: Non-Consumed Catalysts & Programmed Circuits** | **Expressible** | Covered by `recipe.setup` (§2.8). |
| **M32: Multiblock Parallels & Processing Array (PA)** | **Expressible** | Parallel count `total_par` in `let` multiplies `input_amount` and `output_amount`. |
| **M33, M34: CropsNH (2.9) & Forestry/Gendustry Bees** | **Expressible** | M33 is in Example 10.3; M34 is in `gt_bees` below. |
| **M45: OreDict Input Alternatives** | **Blocked / Awkward (R2-01, R2-06)** | Using `resource_param` on recipe inputs with per-slot OreDict tags (`port.oredict_tag`) is blocked by **R2-01** (`port.*` unbound in Step 1 on recipes with fewer OreDict slots, and missing `has_tag()`). |

**Snippet — M26, M27, M28, M32 (GTNH EBF with Multi-Amp Hatches, Coils, Discounts, Perfect OCs & Sub-Tick Parallels) & M34 (Bees):**
```json
{
  "parameters": {
    "hatch_amps": { "label": "Total hatch amps", "type": "int", "default": 2, "min": 2, "max": 65536 },
    "coil":       { "label": "Heating coil", "type": "enum", "option_set": "gt_coils", "default": "cupronickel" },
    "max_par":    { "label": "Max parallels", "type": "int", "default": 1, "min": 1, "max": 256 },
    "housing":    { "label": "Housing", "type": "enum", "option_set": "bee_housings", "default": "ind_apiary" },
    "frame_mod":  { "label": "Frame bonus", "type": "number", "default": 0, "min": 0, "max": 10 }
  },
  "profiles": {
    "gt_ebf": {
      "extends": "gt_single",
      "params": ["hatch_amps", "coil", "max_par"],
      "let": {
        "e_budget": "param.voltage_tier.v * (param.hatch_amps / 2)",
        "t_eff": "max(0, floor(log(e_budget / 8, 4) + 1e-9))",
        "h_mach": "param.coil.heat + 100 * max(0, t_eff - 2)",
        "delta_h": "h_mach - recipe.min_heat",
        "e_disc": "max(1, recipe.eut * (0.95 ^ max(0, floor(delta_h / 900 + 1e-9))))",
        "par": "clamp(floor(e_budget / e_disc + 1e-9), 1, param.max_par)",
        "e_batch": "e_disc * par",
        "oc_count": "max(0, floor(log(e_budget / max(8, e_batch), 4) + 1e-9))",
        "perf_ocs": "min(oc_count, max(0, floor(delta_h / 1800 + 1e-9)))",
        "imp_ocs": "oc_count - perf_ocs",
        "raw_ticks": "recipe.time_raw / ((4 ^ perf_ocs) * (2 ^ imp_ocs))",
        "sub_tick_par": "if(raw_ticks < 1, floor(1 / raw_ticks + 1e-9), 1)",
        "total_par": "par * sub_tick_par"
      },
      "requires": [
        { "expr": "e_budget >= e_disc", "message": "Insufficient hatch power for recipe" },
        { "expr": "h_mach >= recipe.min_heat", "message": "Coil heat too low for recipe" }
      ],
      "craft_time": "max(1, floor(raw_ticks + 1e-9)) / game.ticks_per_second",
      "input_amount": "port.amount * port.chance * total_par",
      "output_amount": "port.amount * port.chance * total_par",
      "metrics": { "eut": { "per_machine": "e_batch * (4 ^ oc_count)" } }
    },
    "gt_bees": {
      "params": ["housing", "frame_mod"],
      "let": {
        "h_term": "(param.housing.mod + param.frame_mod + param.housing.tier) ^ 0.52",
        "s_term": "recipe.bee_speed ^ 0.37"
      },
      "craft_time": "27.5",
      "output_amount": "port.amount * min(1, 0.028 * ((port.chance * 100) ^ 0.52) * h_term * s_term)"
    }
  }
}
```

---

### 2.7 Captain of Industry

| Mechanic | Status | Notes |
|---|---|---|
| **M35: Workers, Electricity (kW) & Computing (TFlops) Totals** | **Expressible** | Cleanly modeled via `metrics` with `enabled: "machine.tflops > 0"` where appropriate. |
| **M36: Maintenance I / II / III Consumption** | **Expressible (with `resource_expr`)** | Using `resource_expr: "machine.maint_res"` on a single `x:maint` extra input port cleanly handles Maintenance I, II, and III without duplicating ports! |
| **M37: Unity Speed Boosts** | **Expressible** | `enum` parameter scaling `craft_time`, `kw`, `maint`, and adding a `unity` metric. |

**Snippet — M35, M36, M37 (Captain of Industry Machine Profile):**
```json
{
  "attributes": {
    "machine": { "workers":      { "type": "number", "default": 0 },
                 "kw":           { "type": "number", "default": 0 },
                 "tflops":       { "type": "number", "default": 0 },
                 "maint_res":    { "type": "string", "default": "maintenance-1" },
                 "maint_rate_s": { "type": "number", "default": 0 } }
  },
  "profiles": {
    "coi_machine": {
      "params": ["unity_boost", "wire_maint"],
      "let": { "b": "param.unity_boost.mult" },
      "craft_time": "recipe.time / (machine.speed * b)",
      "extra_ports": [
        { "id": "maint", "dir": "in", "resource_expr": "machine.maint_res",
          "amount": "machine.maint_rate_s * b * craft_time",
          "enabled": "param.wire_maint && machine.maint_rate_s > 0" }
      ],
      "metrics": {
        "workers": { "value": "ceil(node.machines) * machine.workers", "enabled": "machine.workers > 0" },
        "kw":      { "per_machine": "machine.kw * b",                  "enabled": "machine.kw > 0" },
        "tflops":  { "value": "ceil(node.machines) * machine.tflops",  "enabled": "machine.tflops > 0" },
        "unity":   { "value": "ceil(node.machines) * param.unity_boost.unity_cost",
                     "enabled": "param.unity_boost.unity_cost > 0" }
      }
    }
  }
}
```

---

### 2.8 Oxygen Not Included

| Mechanic | Status | Notes |
|---|---|---|
| **M39: Geyser Eruption & Dormancy Time-Averaging** | **Expressible** | Node parameters compute average yield (`PRE-SOLVE`) and dormancy buffer mass (`POST-SOLVE`). |
| **M40: Wild vs. Domesticated Plants & Farmer's Touch** | **Expressible** | With `input_enabled: "param.domesticated"` (§4.1) and the updated R1 raw-source rule (§7.4), wild plants disable all recipe inputs while keeping harvest outputs active and acting as raw sources. |
| **M38: Parameterized Thermal Output & Net Heat (`kDTU/s`)** | **Expressible (Precomputed SHC sums)** | Uses recipe attributes `in_shc_g_k` and `out_shc_g_k` (sum of `amount * shc` over inputs/outputs) with parameter `in_temp_c` to compute `out_temp_c` and `net_kdtu_s`. |

**Snippet — M38 (Thermal Load), M39 (Geyser), & M40 (Wild vs. Domesticated Plants):**
```json
{
  "profiles": {
    "oni_geyser": {
      "params": ["peak_g_s", "erupt_s", "period_s", "active_cyc", "total_cyc"],
      "let": {
        "avg_g_s": "param.peak_g_s * (param.erupt_s / param.period_s) * (param.active_cyc / param.total_cyc)"
      },
      "requires": [
        { "expr": "param.erupt_s <= param.period_s && param.active_cyc <= param.total_cyc",
          "message": "Active duration cannot exceed period/cycle" }
      ],
      "craft_time": "1",
      "output_amount": "avg_g_s",
      "metrics": {
        "buffer_kg": { "value": "node.crafts * avg_g_s * (param.total_cyc - param.active_cyc) * 600 / 1000" }
      }
    },
    "oni_plant": {
      "params": ["domesticated", "farmers_touch"],
      "let": {
        "mult": "if(param.domesticated, if(param.farmers_touch, 0.5, 1.0), 4.0)"
      },
      "craft_time": "recipe.time * mult",
      "input_enabled": "param.domesticated",
      "extra_ports": [
        { "id": "micronutrient", "dir": "in", "resource": "micronutrient-fertilizer",
          "amount": "(5 / 600) * craft_time",
          "enabled": "param.domesticated && param.farmers_touch" }
      ]
    },
    "oni_thermal_machine": {
      "params": ["in_temp_c"],
      "let": {
        "out_temp_c": "max(param.in_temp_c, recipe.min_out_temp_c)"
      },
      "craft_time": "recipe.time",
      "metrics": {
        "out_temp": { "value": "out_temp_c", "enabled": "recipe.min_out_temp_c > 0" },
        "kdtu_s":   { "per_craft": "(recipe.out_shc_g_k * (out_temp_c + 273.15) - recipe.in_shc_g_k * (param.in_temp_c + 273.15)) / 1000" }
      }
    }
  }
}
```

---

### 2.9 Anno 1800

| Mechanic | Status | Notes |
|---|---|---|
| **M41: Additive Productivity (Electricity, Tractors, Silos, Working Conditions)** | **Expressible** | Cleanly expressed via `let` sum and `extra_ports` for Tractor fuel and Silo grain. |
| **M43: Extra Goods Every $N^{\text{th}}$ Cycle** | **Expressible** | Primary output boost or an `extra_port` (`dir: "out"`, `resource_param: "extra_good"`, `amount: "1 / param.extra_cycles"`). |
| **M42: Specialist Input Replacements** | **Awkward (R2-06)** | Possible via `"resource_expr": "if(is_set(param.input_0_override), param.input_0_override.id, 'steel')"` on the recipe port, **but** requires omitting `"res": "steel"` (losing the port's static base resource) and writing `.id` on `param.input_0_override` until **R2-06** is fixed. |
| **M35: Tiered Workforce & Maintenance Totals** | **Expressible** | Covered by `metrics` with `enabled: "machine.workforce > 0"`. |

**Snippet — M41, M42 (showing R2-06 workaround), & M43 (Anno 1800 Factory):**
```json
{
  "profiles": {
    "anno_factory": {
      "params": ["work_cond_pct", "electricity", "tu_prod_pct", "input_0_override", "extra_good", "extra_cycles"],
      "let": {
        "prod": "max(0.1, 1 + (param.work_cond_pct + param.tu_prod_pct) / 100 + if(param.electricity, 1.0, 0))"
      },
      "craft_time": "recipe.time / prod",
      "extra_ports": [
        { "id": "bonus", "dir": "out", "resource_param": "extra_good",
          "amount": "1 / param.extra_cycles", "enabled": "is_set(param.extra_good)" }
      ]
    }
  },
  "recipes": [
    {
      "id": "sewing-machines", "name": "Sewing Machines", "category": "anno_heavy", "time": 30,
      "in": [
        { "resource_expr": "if(is_set(param.input_0_override), param.input_0_override.id, 'steel')", "amount": 1 },
        ["wood", 1]
      ],
      "out": [["sewing-machines", 1]]
    }
  ]
}
```

---

### 2.10 Against the Storm

| Mechanic | Status | Notes |
|---|---|---|
| **M44: Recipe Star Ratings (☆ / ★ / ★★ / ★★★)** | **Expressible** | Distinct recipes per star tier restricted to the corresponding building categories. |
| **M45: Ingredient Category Slot Selection** | **Blocked (R2-01)** | **Cannot be expressed cleanly in the current spec:** (1) `port.*` in `slot_1.allowed_if` is unbound in §4.5 Step 1 whenever a recipe in the same building uses a fixed `"res"` on `in:1`, and `slot_1` cannot hide itself via `visible_if`; (2) the expression language has no `has_tag(option, port.tag)` function to filter resources by category tag. |
| **M46: Rainpunk Engines & Double Production (Crit) Chance** | **Expressible** | With `resource_expr: "machine.rainwater_res"`, a single `ats_workshop` profile handles Drizzle, Clearance, and Storm water across all buildings. |

**Snippet — M45 (Blocked without R2-01) & M46 (Rainpunk Engine with `resource_expr`):**
```json
{
  "attributes": {
    "machine": { "rainwater_res": { "type": "string", "default": "drizzle-water" } },
    "port":    { "slot_tag":      { "type": "string", "default": "" } }
  },
  "parameters": {
    "slot_0": {
      "label": "Ingredient 1", "type": "resource_choice", "tag": "ats_good",
      "allowed_if": "has_tag(option, port.slot_tag)",
      "notes": "BLOCKED BY R2-01: has_tag() does not exist in §5.6, and port.* is unbound in §4.5 Step 1 if a recipe uses fixed 'res' on a slot!"
    },
    "rain_level":    { "label": "Rain engine", "type": "int", "default": 0, "min": 0, "max": 3 },
    "base_crit_pct": { "label": "Crit chance", "type": "number", "default": 10, "min": 0, "max": 100, "unit": "%" }
  },
  "profiles": {
    "ats_workshop": {
      "params": ["rain_level", "base_crit_pct"],
      "let": {
        "rain_spd": "if(param.rain_level >= 3, 1.0, if(param.rain_level >= 1, 0.5, 0.0))",
        "crit": "min(1.0, param.base_crit_pct / 100 + if(param.rain_level >= 2, 0.25, 0.0))"
      },
      "craft_time": "recipe.time / (1 + rain_spd)",
      "output_amount": "port.amount * port.chance * (1 + crit)",
      "extra_ports": [
        { "id": "rainwater", "dir": "in", "resource_expr": "machine.rainwater_res",
          "amount": "param.rain_level * (2 / 60) * craft_time",
          "enabled": "param.rain_level > 0" }
      ]
    }
  }
}
```

---

### 2.11 Create (Minecraft Mod)

| Mechanic | Status | Notes |
|---|---|---|
| **M47: Shaft Rotational Speed (`RPM` $\in [1, 256]$) & Stress (`SU`)** | **Expressible** | `rpm` parameter scales `craft_time` and `su` metric. |
| **M48: Blaze Burner Heat State & Fuel Port (`Kindled` vs. `Seething`)** | **Expressible** | With `resource_expr: "if(recipe.heat_level == 2, 'blaze-cake', 'coal')"`, a single `x:burner_fuel` extra port covers both Kindled and Seething recipes! |
| **M49: Sequenced Assembly with Salvage Loops** | **Expressible** | Composite recipe with fractional `chance` on primary and salvage outputs. |

**Snippet — M47 & M48 (Create Basin with Single `resource_expr` Blaze Burner Fuel Port):**
```json
{
  "profiles": {
    "create_basin": {
      "params": ["rpm"],
      "craft_time": "recipe.time / param.rpm",
      "extra_ports": [
        { "id": "burner_fuel", "dir": "in",
          "resource_expr": "if(recipe.heat_level == 2, 'blaze-cake', if(recipe.heat_level == 1, 'coal', ''))",
          "amount": "craft_time / if(recipe.heat_level == 2, 160, 80)",
          "enabled": "recipe.heat_level >= 1" }
      ],
      "metrics": {
        "su": { "value": "ceil(node.machines) * machine.su_per_rpm * param.rpm" }
      }
    }
  }
}
```

---

### 2.12 Timberborn

| Mechanic | Status | Notes |
|---|---|---|
| **M50: Workday Hours & Beaver vs. Bot Worker Speed + Bot Consumables** | **Expressible** | Global `work_hours` and per-node `is_bot` parameter with conditional `extra_ports` and `enabled` metrics. |
| **M51: Seasonal Cycle Averaging (Temperate / Drought / Badtide)** | **Expressible** | Globals `wet_days`, `drought_days`, `badtide_days` scale seasonal extractors (`PRE-SOLVE`) and compute storage buffer (`POST-SOLVE`). |
| **M35: Mechanical Power (`hp`) & Worker Count** | **Expressible** | Covered by `metrics` with `enabled`. |

**Snippet — M50 & M51 (Timberborn Workplace & Seasonal Extractor):**
```json
{
  "globals": {
    "work_hours":   { "label": "Workday hours",  "type": "int",    "default": 16, "min": 1, "max": 24 },
    "wet_days":     { "label": "Temperate days", "type": "number", "default": 15, "min": 1, "max": 100 },
    "drought_days": { "label": "Drought days",   "type": "number", "default": 10, "min": 0, "max": 100 },
    "badtide_days": { "label": "Badtide days",   "type": "number", "default": 5,  "min": 0, "max": 100 }
  },
  "profiles": {
    "tb_workplace": {
      "params": ["is_bot", "wellbeing_spd_pct"],
      "let": {
        "uptime": "if(param.is_bot, 1.0, (global.work_hours / 24) * 0.85) * (1 + param.wellbeing_spd_pct / 100)",
        "season_mult": "if(machine.is_seasonal, global.wet_days / (global.wet_days + global.drought_days + global.badtide_days), 1.0)"
      },
      "craft_time": "recipe.time / (machine.speed * uptime * season_mult)",
      "extra_ports": [
        { "id": "bot_fuel", "dir": "in", "resource_expr": "machine.bot_consumable_res",
          "amount": "machine.workers * (1 / 86400) * craft_time", "enabled": "param.is_bot" }
      ],
      "metrics": {
        "hp":      { "per_machine": "machine.hp", "enabled": "machine.hp > 0" },
        "beavers": { "value": "ceil(node.machines) * machine.workers", "enabled": "!param.is_bot && machine.workers > 0" },
        "bots":    { "value": "ceil(node.machines) * machine.workers", "enabled": "param.is_bot && machine.workers > 0" }
      }
    }
  }
}
```

---

### 2.13 Stationeers

| Mechanic | Status | Notes |
|---|---|---|
| **M52a: Linear Ore Degassing, Centrifuge & Alloy Stoichiometry** | **Expressible** | Standard multi-input, multi-output recipes. |
| **M52b: Furnace Pressure $[P_{\min}, P_{\max}]$ & Temperature $[T_{\min}, T_{\max}]$ Readouts** | **Expressible** | With metric `"enabled"` (§6.2), pressure/temperature window readouts are shown on alloy recipes and hidden on un-windowed recipes! |
| **M35: Electrical Power (`W`)** | **Expressible** | Standard `power` metric. |

**Snippet — M52 (Stationeers Furnace Profile with Conditional Window Metrics):**
```json
{
  "metrics": {
    "p_min": { "label": "Min P", "unit": "kPa", "display": ["body"], "aggregate": "none", "format": "%.0f" },
    "p_max": { "label": "Max P", "unit": "kPa", "display": ["body"], "aggregate": "none", "format": "%.0f" },
    "t_min": { "label": "Min T", "unit": "K",   "display": ["body"], "aggregate": "none", "format": "%.0f" },
    "t_max": { "label": "Max T", "unit": "K",   "display": ["body"], "aggregate": "none", "format": "%.0f" }
  },
  "profiles": {
    "stationeers_furnace": {
      "metrics": {
        "p_min": { "value": "recipe.p_min_kpa", "enabled": "recipe.p_max_kpa > 0" },
        "p_max": { "value": "recipe.p_max_kpa", "enabled": "recipe.p_max_kpa > 0" },
        "t_min": { "value": "recipe.t_min_k",   "enabled": "recipe.t_max_k > 0" },
        "t_max": { "value": "recipe.t_max_k",   "enabled": "recipe.t_max_k > 0" }
      }
    }
  }
}
```

---

## 3. Task 2: Expression Language & Validation Rules Audit Summary

1. **Recipe-port `resource_param` vs. §4.5 Step 1 evaluation order (R2-01):** Allowing `port.*` inside `allowed_if` of a `resource_choice` parameter (§5.4 line 479) conflicts with §4.5 Step 1 (which resolves all `profile.params` at the node level before Step 5 visits recipe ports). When a recipe uses a fixed `"res"` on slot 1 instead of `"resource_param": "slot_1"`, `port.*` is unbound during Step 1, and `slot_1` cannot hide itself because `port.*` is forbidden in `visible_if`. In addition, `has_tag(resource, string)` is missing from §5.6.
2. **Pre-solve port evaluation order: `resource_expr` before `enabled` (R2-03):** In §4.5 Steps 5 and 6, `resource_expr` is evaluated before `enabled`. Any `extra_port` whose `enabled` expression (`is_set(param.fuel)`) guards an `allow_none` parameter attribute access in `resource_expr` (`param.fuel.burnt_result`) crashes into runtime `ERROR` when `enabled` is `false`.
3. **Hidden `allow_none: false` parameters without a literal `default` evaluate to `null` (R2-05):** §3.1 line 257 and §4.5 Step 1 bind a hidden parameter (`visible_if == false`) with no literal `default` to `null` even when `allow_none` is `false`, causing subsequent `let` formulas (`param.coil.heat`) to fail with a runtime `ERROR`.
4. **Validation cascade & rule gaps (R2-07, R2-08, R2-10):**
   - Secondary reference errors (`V-REF-3`, `V-REF-4`) are not suppressed when an upstream entity (`profile`, `parameter`, `resource`) has an error, so 1 profile error generates 40,000+ recipe errors in `LoadReport.errors`.
   - `attributes` and `resource_kinds` are missing from the §9.1 cascade, and dropping a `Metric` (`V-MET-1`) in §9.1 drops every `Profile` that references it instead of just removing that metric from the profile.
   - `resource_choice.default`, `slots.default`, and `default: null` (when `allow_none: false`) are not validated by `V-PAR-1` / `V-PAR-3` / `V-REF-5`.
   - `V-REF-1` / §5.7 ("unknown resource id in `resource_expr` string literal") does not restrict the check to non-empty (`!= ""`) string literals in result position of `resource_expr`.
   - `V-OVR-2` does not verify that `overrides.let.<name>` preserves the static type of `<name>` in the profile.
5. **Expression operator & built-in edge cases (R2-09):** Floating-point `%` (`std::fmod` vs. floored) is undefined; `resource == resource` is omitted from §5.3; `is_set(global.x)` is forbidden by §5.6; array `lookup()` needs `std::llround` rather than truncation; and `resource.id` / `option.key` are omitted from §5.5.

---

## 4. Task 3: Solver Linearity, Topology & 50k-Recipe Load Performance Summary

1. **LP Linearity Remains Strictly Guaranteed:** `V-EXPR-3` rejects `node.machines` and `node.crafts` in all `static` and `pre-solve` contexts at compile time. All stoichiometry coefficients $a_p$, craft times $t'_n$, and `per_machine`/`per_craft` metric coefficients are pure constants before `FactorySolver::solve()` runs.
2. **Solver Topology Contradiction on Sinks with Unwired Extra Outputs (R2-02):**
   - §7.4 line 649 classifies a node with `recipe.out = []` and an unwired active extra output port (`x:ash`) as a **sink** ($n \in \mathcal{S}_K$), while §7.2 line 620 / `analyzeComponent` places that unwired active `x:ash` port into **Exports ($\mathcal{X}_K$)**.
   - In Step 2 of `AUTO` resolution, owning an export port in $\mathcal{X}_K$ puts $n \in B_{\text{seeds}}$, which **strips $n$ and its upstream subgraph out of the Sink-Dedicated Subgraph ($\mathcal{S}^+_K$)**!
   - A sink node must have **at least one active input port** and **zero active output ports of any kind** (`out:*` or `x:*`).
3. **50k-Recipe Loader & Validator Bottlenecks / Traps (R2-12):**
   - `V-OVR-2` and `V-PROF-4` depend only on the unique `profile_id`s of compatible machines (since `attributes.machine` is global), so indexing `category -> unique_profile_ids` avoids $50\times$ redundant checks per recipe across machines in the same category.
   - `V-ID-2` via `nlohmann::json::parser_callback_t` will overwrite the *first* key with the *second* key unless the callback tracks keys per object depth and returns `false` on duplicate `key` events.
   - Merging `/attributes` across shards must merge at the 2nd level (`/attributes/<entity_kind>/<attr>`), and `include` must use `std::filesystem::is_directory` and deduplicate canonical file paths.

---

## 5. Task 4: Hand-Authoring UX Judgment

Iteration 1 resolved the biggest hand-authoring pain points (`params` inheritance appending, single-quoted strings `'...'` inside expressions, JSON number/bool literals for `min`/`max`/`count`/`enabled`/`visible_if`/`allowed_if`, `.attrs.x` error suggestions, and ready-to-paste `V-ATTR-1` snippets). The remaining hand-authoring UX friction points are:

1. **Port slot replacement boilerplate (R2-06):** Because a recipe port cannot keep `"res": "steel"` as its fallback when adding `"resource_param"` or `"resource_expr"`, and because `if(is_set(param.x), param.x, 'steel')` is a type error (`resource` vs. `string`, requiring `param.x.id`), authoring optional input replacements by hand is unexpectedly tricky. Allowing `"res"` alongside `"resource_param"` / `"resource_expr"` (as `port.base_resource`) eliminates both traps.
2. **Replacing a single field of an inherited `extra_port` (§4.2):** When a child profile extends a parent profile and only wants to tweak `enabled` or `amount` on an inherited `extra_port`, `V-PROF-4` requires repeating the entire `{ "id", "dir", "resource", "amount", "enabled" }` object in the child profile.
3. **Directory trailing-slash requirement in `include` (R2-12):** Requiring `"include": ["recipes/"]` with a mandatory trailing `/` (instead of checking `is_directory` on disk) is an easy mistake when writing `game.json` by hand.
4. **60-option flat dropdowns for multi-dimensional `slots` (Factorio 2.0 Quality Modules):** Representing `4 module types × 3 tiers × 5 qualities` as 60 flat entries in an `option_set` works in the schema, though an optional `"group"` field on `option_sets.<set>.options[i]` (mirroring `parameters.<id>.ui.group`) would let the UI group options into sub-menus or headers without changing any expression semantics.
