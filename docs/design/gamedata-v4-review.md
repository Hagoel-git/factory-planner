# GameData v4 Specification — Adversarial Review

**Reviewed document:** [`docs/design/gamedata-v4-spec.md`](file:///home/hagoel/Projects/factory-planner/docs/design/gamedata-v4-spec.md)  
**Reference inputs:** [`docs/ROADMAP.md`](file:///home/hagoel/Projects/factory-planner/docs/ROADMAP.md), [`docs/design/mechanics-matrix.md`](file:///home/hagoel/Projects/factory-planner/docs/design/mechanics-matrix.md), [`docs/solver.md`](file:///home/hagoel/Projects/factory-planner/docs/solver.md), [`src/core/graph/FactorySolver.cpp`](file:///home/hagoel/Projects/factory-planner/src/core/graph/FactorySolver.cpp)

---

## 1. Findings Ranked by Severity (with Concrete Fixes)

### Summary Index

| ID | Severity | Category | Title |
|---|---|---|---|
| **R-01** | **CRITICAL** | Solver / Topology | Fixing $R_p = 0$ and $F_c = 0$ on `INACTIVE`/`INVALID` ports and wires breaks `FactorySolver` conservation, no-overflow rules, and component analysis |
| **R-02** | **CRITICAL** | Solver / `AUTO` Modes | Rule R1 ("Extra ports do not affect raw/sink classification") breaks `AUTO` target resolution when an extra port is wired (e.g. Mindustry drills, GTNH Crop Manager) |
| **R-03** | **CRITICAL** | Expression / Spec Bug | Contradiction between `allowed_if`, `default` fallback, and `requires` causes runtime failure when no option is allowed (reproducible in Worked Example 10.2) |
| **R-04** | **HIGH** | Mechanics / Ports | Recipe ports (`recipe.in`/`out`) cannot use `resource_param`, blocking M42 (Anno input replacements), M45 (AtS/GTNH tag slots), and generic Sink/Source nodes |
| **R-05** | **HIGH** | Mechanics / Extra Ports | `extra_ports` only accept a fixed `resource` or `resource_param`, blocking derived resources like `param.fuel.burnt_result` (M14) and `machine.booster_res` (M22, M36, M46, M48) |
| **R-06** | **HIGH** | Mechanics / Expressions | `port_enabled` cannot distinguish input vs. output ports (`port.dir` is missing), blocking M40 (ONI Wild Plants disabling inputs while keeping harvest outputs active) |
| **R-07** | **HIGH** | Solver / `AUTO` Modes | Recipe with all inputs disabled at runtime (`port_enabled == false`, e.g. ONI Wild Plant) is still treated as a non-raw node under R1 |
| **R-08** | **HIGH** | Expression / Validation | Static-context expressions (`min`, `max`, `count`, `visible_if`, `allowed_if`) lack runtime evaluation order, hidden-parameter short-circuiting, and per-profile `V-EXPR-5` ownership rules |
| **R-09** | **HIGH** | Expression / Runtime | Unset `allow_none` parameters (`null`) and empty `slots` lists in `min(list)` / `max(list)` have under-specified or hostile runtime semantics |
| **R-10** | **HIGH** | Validation / Overrides | Recipe `overrides.let` can introduce `let` cycles (`V-EXPR-6`), and `let` names can shadow reserved namespaces (`machine`, `param`, `port`, `craft_time`) and built-ins |
| **R-11** | **HIGH** | Performance (50k) | Unbounded Levenshtein "did you mean" searches (`V-REF-1`), un-interned port/override AST compilation (`V-OVR-2`), and eager JSON pointer formatting threaten the 2 s GTNH load budget |
| **R-12** | **MEDIUM** | Hand-Authoring UX | `extends` replaces `params` instead of inheriting/merging them, breaking inherited `let` formulas in child profiles |
| **R-13** | **MEDIUM** | Hand-Authoring UX | Requiring string literals for numeric `min`/`max`/`count` (`"min": "1"`) and double-escaped quotes (`\"...\"`) inside expressions creates high hand-editing friction |
| **R-14** | **MEDIUM** | Validation / Schema | Missing validation rules for `recipe.setup`, `globals` static-expression cycles, `tables` element types, empty `option_sets`, and single-file duplicate JSON keys |
| **R-15** | **MEDIUM** | Expression / Slots | Dynamic `slots.count` changes (e.g. switching from a 4-slot to a 2-slot machine) and per-slot `allowed_if` fallback are under-specified |
| **R-16** | **MEDIUM** | Worked Example 10.2 | GTNH overclocking formula in Example 10.2 (`floor(log(v / recipe.eut, 4))`) over-overclocks sub-8 EU/t recipes by 1–2 extra tiers and divides by zero if `eut == 0` |
| **R-17** | **LOW** | Solver / Conditioning | Reference port selection (`a > 0`) and negative port amount checks need a `kZeroEpsilon` (`1e-9`) guard to prevent ill-conditioned LP matrices and floating-point noise errors |
| **R-18** | **LOW** | Metrics / Units | Canonical unit conventions for `is_rate: true` across `per_machine`, `per_craft`, and `value` metrics, plus conditional metric visibility, are under-specified |

---

### Detailed Findings & Suggested Fixes

#### R-01 · CRITICAL · Fixing $R_p = 0$ and $F_c = 0$ on `INACTIVE`/`INVALID` ports and wires breaks `FactorySolver`
- **Where:** Spec §7.2 (lines 608–611), §7.4 (lines 628–632) vs. [`FactorySolver.cpp`](file:///home/hagoel/Projects/factory-planner/src/core/graph/FactorySolver.cpp#L63-L484) (`partitionComponents`, `analyzeComponent`, `addConnectionConstraints`).
- **What goes wrong:**
  Spec §7.4 keeps wires attached to inactive ports (`INACTIVE`) and mismatched `resource_choice` ports (`INVALID`), and §7.2 says only: *"Ports with `active = false` or `amount_per_craft = 0` are fixed to 0"* and *"Wires whose state is not OK (§7.4) have their flow variable fixed to 0."*
  In `FactorySolver`, keeping non-`OK` wires in `comp.port_incoming` / `comp.port_outgoing` and inactive ports in `comp.output_ports` causes four solver failures:
  1. **Upstream shutdown via No-Overflow Rules:** Suppose a Water Extractor (`out:0`) is wired to a Drill's booster port `x:water` (`boosted = true`). The user then unchecks `boosted`. Wire $c$ becomes `INACTIVE` ($F_c = 0$), but `out:0` still has $W^+(\text{out:0}) = \{c\} \neq \emptyset$. `addConnectionConstraints` enforces $R_{\text{out:0}} - E_{\text{out:0}} - F_c = 0$. Whenever the Water Extractor is push-driven ($p \in Z_K$, Rule 1) or outside $\mathcal{T}_K$ (Rule 2), `excess->SetUB(0.0)` forces $E_{\text{out:0}} = 0$, which forces $R_{\text{out:0}} = 0$ and **shuts down the upstream node** (or makes a rated `PUSH` goal `INFEASIBLE`).
  2. **Downstream shutdown on `INACTIVE`/`INVALID` incoming wire:** If an active input port $p$ has only an `INACTIVE` or `INVALID` incoming wire $c$ ($F_c = 0$), $W^-(p) \neq \emptyset$ excludes $p$ from unwired imports ($\mathcal{B}^{\text{imp}}_K$) and adds $\sum_{c \in W^-(p)} F_c - R_p = 0 \implies R_p = 0$, clamping the downstream node to 0 instead of treating $p$ as an unwired import.
  3. **Broken Sink-Dedicated Subgraph ($\mathcal{S}^+_K$):** Every node holds its full port superset (Q2). On an LV Macerator (Example 10.2), `out:1` (`dust_gold`, `min_tier: 3`) is `active = false` and unwired. Because `analyzeComponent` puts every unwired output port into `comp.export_ports` ($\mathcal{X}_K$), the inactive `out:1` port marks the Macerator as having an active export (`B_seeds`), disqualifying the Macerator and everything upstream of it from $\mathcal{S}^+_K$.
  4. **False component merging & broken splitter fairness:** `partitionComponents` merges two subgraphs linked only by an `INACTIVE`/`INVALID` wire into one component $K$ (so a `TARGET` in one subgraph switches the other from `PUSH` to `LIMIT`). And `addBalance` (Stages 2c.2 and 4) includes $F_c = 0$ wires in `conn_ids`, pinning $lo_p = 0$ on splitters/mergers.
- **Suggested Fix:**
  Specify in §7.2 and §7.4 that **all solver graph-topology and LP steps (`partitionComponents`, `analyzeComponent`, `addConnectionConstraints`, `addBalance`) operate exclusively on active ports (`active == true && amount_per_craft > 0`) and `OK` wires**. Inactive ports and non-`OK` wires are excluded from $W^+(p)$, $W^-(p)$, $\mathcal{B}^{\text{imp}}_K$, $\mathcal{X}_K$, and component BFS, and their solved `rate` / `excess_rate` are simply written as `0.0`.

---

#### R-02 · CRITICAL · Rule R1 ("Extra ports do not affect raw/sink classification") breaks `AUTO` target resolution when an extra port is wired
- **Where:** Spec §0 R1, §4.3 (line 376), §7.4 (line 636), §10.3 (line 1007), §12.7 vs. [`FactorySolver::analyzeComponent`](file:///home/hagoel/Projects/factory-planner/src/core/graph/FactorySolver.cpp#L215-L265) and [`FactorySolver::classifyConstraint`](file:///home/hagoel/Projects/factory-planner/src/core/graph/FactorySolver.cpp#L499-L525).
- **What goes wrong:**
  Spec §4.3 and §7.4 state: *"raw source ⇔ the recipe's `in` is empty; sink ⇔ the recipe's `out` is empty. Extra ports do not affect classification."*
  Consider Worked Example 10.3 (`gt_crop_manager`, `recipe.in = []`, `out = [sugar_cane, sticky_resin]`, `extra_ports = [x:water, x:fertilizer]`) with `hydrated = true`, where the user wires a Water source into `x:water` and sets a rate constraint of `10` (`AUTO` mode) on the Crop Manager's `sugar_cane` output wanting 10 sugar cane/s:
  1. Because `recipe.in` is empty, the Crop Manager is unconditionally classified as a **raw source**, so its `sugar_cane` output port is placed in `comp.raw_ports` ($\mathcal{B}^{\text{raw}}_K$).
  2. In Step 1 of `AUTO` resolution (`analyzeComponent` line 262: `else if (comp.output_ports.count(pid) && !comp.raw_ports.count(pid))`), `sugar_cane` is **not** resolved to `TARGET`.
  3. In Step 3 of `AUTO` resolution, `has_effective_targets` is **`false`**! The entire component is treated as **Supply-Driven** instead of **Demand-Driven**: `sugar_cane` resolves to `PUSH`, the upstream Water producer also resolves to `PUSH` (instead of `LIMIT`), and `target_serving_nodes` ($\mathcal{T}_K$) is **empty** (so Rule 2 forbids byproduct excess shedding on any upstream supplier).
  4. Symmetrically, if a sink recipe (`recipe.out = []`) has an active wired extra output port (e.g. `x:ash` wired to an ash processor), classifying it as a sink puts it in $\mathcal{S}_K$.
- **Suggested Fix:**
  Replace R1 in §4.3 and §7.4 with an activity/wiring-aware rule:
  - A node is a **raw source** in component $K$ iff it has **no active recipe input ports** (`in:*`) **and no active extra input ports (`x:*`) with an `OK` incoming wire**. (When an extra input port like `x:water` is unwired, the node remains a raw source and `x:water` is an unwired import in $\mathcal{B}^{\text{imp}}_K$; as soon as `x:water` is wired to an upstream node, the node ceases to be a raw source so its outputs resolve to `TARGET` in `AUTO` mode.)
  - A node is a **sink** in component $K$ iff it has **no active recipe output ports** (`out:*`) **and no active extra output ports (`x:*`) with an `OK` outgoing wire**.

---

#### R-03 · CRITICAL · Contradiction between `allowed_if`, `default` fallback, and `requires` causes runtime failure when no option is allowed (Worked Example 10.2)
- **Where:** Spec §3.1 (lines 240, 247, 257), §4.5 (lines 401–403), §9.2 (`V-PAR-3`), and Worked Example 10.2 (lines 894–912, 933).
- **What goes wrong:**
  In Worked Example 10.2 (`gt_single`):
  - `voltage_tier` has no explicit `"default"` (relying on §3.1: *"first allowed option"*) and defines:
    `"allowed_if": "option.t >= machine.min_tier && option.t <= machine.max_tier && option.v >= recipe.eut"`
  - `let` (Step 2 of §4.5) evaluates `"t": "param.voltage_tier.t"` and `"v": "param.voltage_tier.v"`.
  - `requires` (Step 3 of §4.5) evaluates `"v >= recipe.eut"` with message `"Machine tier too low for this recipe"`.
  Two contradictions occur:
  1. **Empty allowed set crash/error before `requires`:** If a recipe has `eut: 2048` (EV) on a machine with `max_tier: 2` (MV), `allowed_if` is `false` for **every** option in `gt_tiers`. In Step 1 of §4.5, there is no "first allowed option" to fall back to! Then Step 2 (`let`) executes *before* Step 3 (`requires`) and tries to read `param.voltage_tier.t` on a null/empty option, failing with a runtime `ERROR` instead of reaching Step 3's `BLOCKED` message (`"Machine tier too low for this recipe"`).
  2. **Dead `requires` check & conflicting fallback rules:** Whenever at least one tier *is* allowed (e.g. `max_tier: 8`, `recipe.eut: 512`), `allowed_if` already filters out ULV–MV. If the user had LV selected, Step 1 auto-switches `param.voltage_tier` to HV (`v = 512`), so `v >= recipe.eut` in `requires` is **unreachable dead code**! Furthermore, §3.1 line 257 says a disallowed value *"falls back to the default"*, whereas §10.2 line 933 says it *"falls back to the first allowed tier"*—which contradict if an explicit `default` is itself disallowed by `allowed_if`.
- **Suggested Fix:**
  1. Define the fallback chain in §3.1 / §4.5 Step 1 unambiguously: if a stored `enum` or `resource_choice` value fails `allowed_if`, try the parameter's declared `default` (if it passes `allowed_if`), then the **first option/resource passing `allowed_if`** (or `none` if `allow_none: true`). If **zero** options pass `allowed_if` and `allow_none` is `false`, immediately terminate evaluation in Step 1 with status `BLOCKED` and message `"No allowed value for parameter '<label>'"`.
  2. In Worked Example 10.2, remove `&& option.v >= recipe.eut` from `voltage_tier.allowed_if` (keep `option.t >= machine.min_tier && option.t <= machine.max_tier` in `allowed_if` so the combo box shows the machine's valid tiers, and let `requires: v >= recipe.eut` put the node into `BLOCKED` with `"Machine tier too low for this recipe"` when the chosen tier is below `recipe.eut`).

---

#### R-04 · HIGH · Recipe ports (`recipe.in`/`out`) cannot use `resource_param`, blocking M42, M45, and parameterized Sink/Source recipes
- **Where:** Spec §0 Q3, §2.8 (lines 188–202), §4.3, §9.2 (`V-NUM-4`).
- **What goes wrong:**
  Decision Q3 states *"a port can take its resource from a `resource_choice` parameter"*, and §7.1/§7.4 already build full support for dynamic port resources and `INVALID` wires in `NodeEval`. However, §4.3 only allows `resource_param` on profile `extra_ports` (which cannot be overridden per recipe per §4.4), while §2.8 restricts recipe ports (`recipe.in` / `recipe.out`) to a fixed string `"res"`. This blocks three major use cases:
  1. **M45 (Against the Storm Ingredient Category Slots & GTNH OreDict inputs):** Different recipes in the same building take different resource tags and base amounts per input slot (e.g. *Skewers* takes `tag: "skewer_meat"` on `in:0` and `tag: "skewer_veg"` on `in:1`, while *Biscuits* in the same building takes `flour` and `tag: "biscuit_filling"`). Profile-level `extra_ports` cannot express recipe-specific input slots.
  2. **M42 (Anno 1800 Specialist Input Replacements):** Replacing `steel` with `iron_ore` on `in:0` of a Sewing Machine factory cannot be done without adding a second port `in:1` (which breaks existing wires on `in:0`).
  3. **Generic Sink / Void / External Source nodes:** Because `V-NUM-4` rejects a recipe with empty `in` and empty `out`, you cannot build a sink node using an `extra_port`. Without `resource_param` on recipe ports, sinking any item in GTNH (Trash Can / Void) or Satisfactory (AWESOME Sink) requires generating **20,000+ separate `sink-<resource>` recipes** in the game data!
- **Suggested Fix:**
  Allow long-form recipe ports (§2.8) to specify `"resource_param": "<param_id>"` (or `"res_expr"`, see R-05) as an alternative to `"res"`, and allow a recipe-level `resource_choice` tag filter via a port attribute (e.g. `"allowed_if": "has_tag(option, port.tag)"` or `"res_expr"`). This also enables a single generic `"sink"` recipe (`"in": [{ "resource_param": "item", "amount": 1 }], "out": []`) and a single `"source"` recipe.

---

#### R-05 · HIGH · `extra_ports` cannot derive their resource from attributes or expressions, blocking M14, M22, M36, M46, and M48
- **Where:** Spec §4.3 (lines 365–375), §5.3, §9.2 (`V-PROF-4`).
- **What goes wrong:**
  In §4.3, an `extra_port` must specify either a hardcoded literal `"resource": "water"` or `"resource_param": "<param_id>"`. It **cannot** specify an expression such as `param.fuel.burnt_result` or `machine.booster_resource`. This blocks or severely complicates:
  - **M14 (Pyanodons / Seablock / Factorio Ash & Spent Fuel Containers):** When `param.fuel` is `"coal"`, the extra output port must emit `"ash"`; when `"tar-canister"`, `"empty-canister"`; when `"uranium-fuel-cell"`, `"depleted-uranium-fuel-cell"`; when `"syngas"`, the port must be disabled (`""`). Using `"resource_param": "fuel"` on the output port would output unburnt `"coal"`!
  - **M22 (Mindustry Drills & Erekir Extractors):** Different drills sharing the drill profile use different booster fluids (`water`, `cryofluid`, `nitrogen`, `cyanogen`, `hydrogen`).
  - **M36 (Captain of Industry Maintenance I / II / III):** Different machines consume `maintenance_1`, `maintenance_2`, or `maintenance_3`.
  - **M46 (Against the Storm Rainpunk Engines):** Craft, Industry, and Food buildings consume `drizzle_water`, `clearance_water`, or `storm_water`.
  - **M48 (Create Blaze Burner):** A Basin consumes `coal` when `recipe.heat_level == 1` (Kindled) and `blaze_cake` when `recipe.heat_level == 2` (Seething).
- **Suggested Fix:**
  1. Allow `"resource"` on `extra_ports` (and long-form recipe ports) to be either a fixed resource ID or a `"resource_expr"` returning a `string` (resource ID) or `resource`. If `resource_expr` evaluates to `""` (empty string), the port is automatically marked `active = false`.
  2. When validating `resource_expr` at load time, verify that every string literal or resource attribute value that can flow into it is either `""` or a valid resource ID (`V-REF-1`).

---

#### R-06 · HIGH · `port_enabled` cannot distinguish input vs. output ports (`port.dir` is missing from §5.5)
- **Where:** Spec §4.1 (lines 317–319), §5.5 (lines 489–492).
- **What goes wrong:**
  In §4.1, profiles have separate `input_amount` and `output_amount` formulas, but only a **single** `port_enabled` formula that runs for both input and output recipe ports. Meanwhile, §5.5 (`port.*` built-in fields) exposes `port.amount`, `port.chance`, `port.catalyst`, `port.index`, `port.resource`, and `port.<attr>`, **but not `port.dir`**!
  As a result, a profile cannot write a `port_enabled` expression that disables only input ports (such as **M40 ONI Wild Plants**, where `param.domesticated == false` disables all irrigation/fertilizer recipe inputs while keeping the crop harvest output enabled) or only output ports without requiring every recipe port in JSON to carry a manual boolean attribute.
- **Suggested Fix:**
  Add `port.dir` (`string`: `"in"` or `"out"`) to §5.5 built-in fields, and/or split `port_enabled` into `input_enabled` and `output_enabled` in §4.1 (defaulting both to `"true"`).

---

#### R-07 · HIGH · Recipe with all inputs disabled at runtime (`port_enabled == false`) is still treated as a non-raw node under R1
- **Where:** Spec §7.4 (line 636) vs. M40 (ONI Wild Plants).
- **What goes wrong:**
  In **M40 (Oxygen Not Included)**, a Sleet Wheat or Bristle Blossom plant has recipe inputs (`water`, `dirt`). When the user sets `param.domesticated = false` (Wild mode), all input ports on the plant are disabled (`active = false`), so the plant acts as a self-sustaining raw source (`4×` growth time, zero inputs).
  However, §7.4 states: *"raw source ⇔ the recipe's `in` is empty"*. Because `recipe.in` in JSON is non-empty, `FactorySolver::analyzeComponent` does **not** put the wild plant's output into `comp.raw_ports` ($\mathcal{B}^{\text{raw}}_K$). In a supply-driven component (no downstream `TARGET`), the wild plant has no ports in $\mathcal{B}^{\text{raw}}_K \cup \mathcal{B}^{\text{imp}}_K$, so `AUTO` resolution resolves its output to an unconstrained `TARGET` instead of `PUSH`, and Stage 3a drives the wild plant's production to **0**!
- **Suggested Fix:**
  Combined with R-02, define a node as a **raw source** in `NodeEval` / `analyzeComponent` when it has **zero active recipe input ports** (`in:*` with `active == true && amount_per_craft > 0`) and **zero wired active extra input ports**, rather than checking whether the static `recipe.in` JSON array is empty.

---

#### R-08 · HIGH · Static-context expressions (`min`, `max`, `count`, `visible_if`, `allowed_if`) lack runtime evaluation order, hidden-parameter short-circuiting, and correct cascade ownership
- **Where:** Spec §3.1 (lines 242–252), §4.5 (line 401), §5.4 (line 467), §9.1 (lines 724–732), §9.2 (`V-EXPR-5`).
- **What goes wrong:**
  1. **Cascade ownership bug (`V-EXPR-5`):** Parameters are defined globally in `"parameters"` (§3.1), while parameter ordering (`params: ["p1", "p2"]`) is defined per **profile** (§4.1). If `p2.visible_if` references `param.p1`, `V-EXPR-5` (*"a static expression using a later parameter | Drops: the owner"*) is ambiguous: if Profile A has `["p1", "p2"]` (valid) and Profile B has `["p2", "p1"]` (invalid), dropping parameter `p2` would break Profile A! Profile B is the violator and must be dropped.
  2. **Parameter-to-parameter dependency in cascade (§9.1):** The cascade diagram in §9.1 shows a single box `Parameters / globals`. If `p1` is dropped due to an error and `p2.visible_if` references `param.p1`, `p2` must also be dropped before cascading to `Profiles`.
  3. **Hidden parameter evaluation order in §4.5 Step 1:** In Example 10.1, `somersloops` has `"min": "0", "max": "machine.somersloop_slots", "visible_if": "machine.somersloop_slots > 0"`. What if a hidden parameter has `"min": "1", "max": "machine.slots"` where `machine.slots == 0` (`min > max`)? If `min`/`max` (or `allowed_if` / `count`) are evaluated when `visible_if` is `false`, `clamp(x, 1, 0)` triggers a runtime `ERROR` (`lo > hi`, §5.6) on a hidden parameter!
- **Suggested Fix:**
  1. Specify in §4.5 Step 1 that parameters are resolved sequentially in `profile.params` order: for each parameter, evaluate `visible_if` **first**; if `false`, bind the parameter directly to its declared `default` (or `null`/empty if no literal default) **without** evaluating `min`, `max`, `count`, or `allowed_if` and without emitting range warnings.
  2. In `V-EXPR-5` (§9.2), specify that ordering checks (`param.x` appearing earlier in `profile.params`) are checked per profile and drop **the profile**, while unknown `param.x` in a parameter's static expression drops **the parameter** (followed by transitive closure over `Parameters` and `Globals` in §9.1).

---

#### R-09 · HIGH · Unset `allow_none` parameters (`null`) and empty `slots` lists in `min(list)` / `max(list)` have under-specified or hostile runtime semantics
- **Where:** Spec §3.1 (lines 249–251), §5.2 (line 446), §5.3 (lines 456–459), §5.6 (lines 500, 507–510), §5.7 (line 538).
- **What goes wrong:**
  1. **Unset `allow_none` parameters in `==` / `!=` and `.attr`:** When an `enum` or `resource_choice` parameter with `allow_none: true` is set to `none` (`null`), what happens if a formula evaluates `param.prolif == "proliferator-mk1"` without an `is_set(param.prolif)` guard? If `==` throws a runtime error on `null`, simple conditions become landmines; if `==` returns `false` (and `!=` returns `true`), it is safe and intuitive. Meanwhile, reading `param.prolif.attr`, `param.prolif.id`, or `lookup("t", param.prolif)` when `param.prolif` is `none` *must* be listed as a runtime error in §5.7 (currently omitted from line 538).
  2. **`min(list)` and `max(list)` fail on default empty `slots`:** By §3.1, the default value of a `slots` parameter is **all slots empty**. By §5.6 (line 500), `sum(list)` returns `0` when all slots are empty, but `min(list)` and `max(list)` trigger a **runtime `ERROR`** on an empty list! Consequently, any profile that uses `max(param.modules.attr)` or `min(param.modules.attr)` will put every newly placed node into `ERROR` status until the user fills at least one slot!
- **Suggested Fix:**
  1. In §5.3 & §5.7: specify that `param.x == "key"` evaluates to `false` (and `!=` to `true`) when `param.x` is `none`, whereas accessing `.attr`, `.id`, or passing an unset `param.x` to `lookup()` is a runtime error (guarded by short-circuiting `is_set(param.x)`).
  2. In §5.6: add an optional default argument to list `min` and `max`: `min(list, default)` and `max(list, default)` (or have `min(list)` / `max(list)` return `0` on empty slots, matching `sum(list)`).

---

#### R-10 · HIGH · Recipe `overrides.let` can introduce `let` cycles (`V-EXPR-6`), and `let` names can shadow reserved namespaces and built-ins
- **Where:** Spec §4.1 (line 314), §4.4 (lines 384–395), §5.2, §9.2 (`V-EXPR-6`, `V-OVR-1`, `V-OVR-2`).
- **What goes wrong:**
  1. **Override-induced `let` cycles:** Suppose profile `P` defines `"let": { "a": "1", "b": "a + 1" }` (acyclic). A recipe with `"overrides": { "let": { "a": "b + 1" } }` creates the cycle `a -> b -> a` only when combined with `P`. Currently, `V-EXPR-6` says `"let dependency cycle | Drops: the profile"`, which would wrongly drop the valid profile `P` instead of the offending recipe!
  2. **Identifier shadowing in `let`:** No rule in §4.1, §5.1, or §9.2 forbids a `let` variable from being named `machine`, `recipe`, `param`, `port`, `node`, `game`, `global`, `option`, `craft_time`, `min`, `max`, `sum`, `count`, `lookup`, `if`, or `is_set`. Because the grammar (§5.2) parses `machine.speed` as `postfix = primary { "." IDENT }` where `primary` can be `IDENT`, a `let` variable named `machine` or `craft_time` collides with built-in namespaces and values.
- **Suggested Fix:**
  1. Extend `V-OVR-2` / `V-EXPR-6` so that `let` cycle detection re-runs for each `(recipe, profile)` pair that overrides `let`, dropping **the recipe** if the override introduces a cycle.
  2. Add a check to `V-EXPR-1`/`V-PROF-*` rejecting any `let` variable name (or parameter/attribute name) that matches a reserved root identifier (`game`, `global`, `machine`, `recipe`, `param`, `port`, `node`, `option`, `craft_time`, `true`, `false`) or any built-in function name.

---

#### R-11 · HIGH · 50k-Recipe Load Performance Bottlenecks (`V-REF-1` fuzzy matching, `V-OVR-2` AST compilation, `nlohmann::json` DOM overhead)
- **Where:** Spec §2.9 (lines 216–220), §5.7 (line 528), §9.2 (`V-REF-1`, `V-OVR-2`, `V-W-2`).
- **What goes wrong:**
  Testing the spec against GTNH scale (50,000+ recipes, 20,000+ resources, ~200,000 ports) reveals four load-time traps that can blow the 2-second budget:
  1. **Quadratic Levenshtein search on broken shards (`V-REF-1`, §5.7):** If a shard has 5,000 ports referencing missing resources (e.g. a missing dependency shard), computing edit distance $\le 2$ against 20,000 resource IDs performs $5{,}000 \times 20{,}000 = 10^8$ Levenshtein comparisons, freezing load for 10+ seconds.
  2. **Repeated compilation of identical port/override expressions (`V-OVR-2`):** If an importer emits `"enabled": "param.voltage_tier.t >= 3"` on 30,000 long-form ports across 50,000 recipes, compiling and type-checking each port's AST separately per compatible machine will parse and allocate 100,000+ identical ASTs.
  3. **`nlohmann::json` `std::map` object overhead & eager JSON pointer formatting:** By default, `nlohmann::json` represents every JSON object as `std::map<std::string, json>`, doing ~1.5 million individual red-black tree node allocations for 50k recipes. Building `/recipes/49999/out/2` string pointers eagerly during traversal adds another ~500k string allocations.
  4. **Naive $O(R \cdot N)$ unused-entity scan (`V-W-2`):** Checking *"machine that no recipe can use; resource used by no recipe"* by scanning recipes per resource/machine takes $20{,}000 \times 50{,}000 = 10^9$ iterations unless done via a single linear pass with bitsets/hashsets.
- **Suggested Fix:**
  Add explicit implementation constraints to §2.9 / §5.7:
  1. **Cap fuzzy "did you mean" searches** to at most the first 25 errors per load, pre-filtering candidates by `abs(len(a) - len(b)) <= 2`.
  2. **Intern and cache compiled expressions** by `(profile_id, context, expr_string)` so identical port/override expressions across 50k recipes are parsed and type-checked once.
  3. **Construct JSON pointers lazily** (only when pushing an error/warning to `LoadReport`, passing integer/string path frames on a stack during validation), and compute `V-W-2` via a single $O(N + R + M)$ marking pass.

---

#### R-12 · MEDIUM · Profile inheritance (`extends`) replaces `params` instead of merging/appending them
- **Where:** Spec §4.2 (lines 356–360).
- **What goes wrong:**
  In §4.2, `let`, `metrics`, and `extra_ports` merge by key, and `requires` appends, but **"`params` is replaced when the child defines it"**.
  If `gt_single` defines `"params": ["voltage_tier"]` and `"let": { "t": "param.voltage_tier.t" }`, and a child profile `gt_ebf` writes `"extends": "gt_single", "params": ["coil"]`, `gt_ebf` loses `"voltage_tier"` and immediately fails `V-EXPR-5` on the inherited `let.t` formula unless the author manually copy-pastes every parent parameter into every child profile.
- **Suggested Fix:**
  Change §4.2 so that `params` in a child profile **appends** new parameters to the parent's `params` list (deduplicating any parameter ID already present, or allowing `"params"` to list additional parameters in UI order).

---

#### R-13 · MEDIUM · Hand-Authoring UX: Stringified numbers in `min`/`max`/`count` and mandatory `\"` escaping in expressions
- **Where:** Spec §3.1 (lines 232, 242, 250), §5.1 (line 422), §9.2 (`V-STRUCT-2`).
- **What goes wrong:**
  1. In §3.1, `"default": 100` and `"step": 0.0001` are JSON numbers, but `"min": "1"`, `"max": "250"`, and `"count": "2"` **must** be JSON strings! Writing `"min": 1, "max": 250` by hand triggers `V-STRUCT-2` (*"expected string, got number"*) and drops the parameter.
  2. In §5.1, strings inside expressions must be double-quoted (`"extra"`). Because expressions are themselves JSON strings, every `lookup`, option comparison, and `count` call requires backslash-escaped quotes: `"if(on && param.prolif_mode == \"extra\", lookup(\"crop_tier_cap\", t), 0)"`.
- **Suggested Fix:**
  1. Allow `min`, `max`, `count`, `enabled`, `visible_if`, and `allowed_if` to accept **either** a JSON primitive (`number` / `bool`) **or** an expression `string`.
  2. Allow **single-quoted string literals** (`'extra'`, `'crop_tier_cap'`) in the expression lexer (§5.1) alongside double-quoted strings.

---

#### R-14 · MEDIUM · Missing validation rules (`recipe.setup`, `globals` cycles, `tables` types, empty `option_sets`, single-file duplicate keys)
- **Where:** Spec §2.8 (line 183), §3.2 (line 262), §3.3, §3.4, §9.2.
- **What goes wrong:**
  1. **`recipe.setup` is unvalidated:** §2.8 introduces `"setup": [["programmed_circuit_1", 1]]`, but no rule in §9.2 checks that `setup` entries are 2-element tuples referencing a valid resource ID (`V-REF-1`) with a finite positive amount (`V-NUM-2`).
  2. **Cycles between `globals` static expressions:** §3.2 allows a global's `min`, `max`, `count`, `visible_if`, and `allowed_if` to reference other `globals`. Unlike `profile.params` (ordered array) and `profile.let` (`V-EXPR-6`), `globals` is an unordered JSON map, so `global.a.max = "global.b"` and `global.b.max = "global.a"` forms an uncaught cycle!
  3. **`tables` validation missing:** §3.4 defines array and object lookup tables of numbers, but no rule in §9.2 rejects a table containing non-finite numbers, strings, nested objects, or an empty table.
  4. **Empty `option_sets` / zero allowed resources:** `V-REF-5` checks `"resource_choice tag matching no resource"`, but fails to check an explicit `"resources": []` list on `resource_choice` or an empty `"options": []` array in `option_sets`.
  5. **Duplicate keys within a single JSON object (`V-ID-2`):** Standard `nlohmann::json::parse()` silently overwrites duplicate keys inside the same JSON object (`"profiles": { "a": {}, "a": {} }`) unless a custom SAX/parser callback is used.
- **Suggested Fix:**
  Extend `V-REF-1`/`V-NUM-2` to cover `recipe.setup`; extend `V-EXPR-6` to topological-sort `globals` static dependencies and reject cycles; add `V-TBL-1` for table values; extend `V-PAR-3`/`V-REF-5` to reject empty `options`/`resources` lists; and note in §9.2 that `V-ID-2` checks duplicate object keys during JSON parsing (or via a SAX check).

---

#### R-15 · MEDIUM · Dynamic `slots.count` changes and per-slot `allowed_if` fallback are under-specified
- **Where:** Spec §3.1 (lines 250–258), §5.6 (`sum`, `count`), §8.2.
- **What goes wrong:**
  1. In Example 10.5, `"modules"` has `"count": "machine.module_slots"`. If a node is switched from `assembling-machine-3` (4 slots) to `assembling-machine-2` (2 slots), the stored array in `node.params["modules"]` has 4 elements. The spec does not state whether `sum(param.modules.speed)` and `count(param.modules)` evaluate only over the first `count` (`2`) slots or all 4 stored slots, nor whether slots 3–4 are preserved in `node.params` (like hidden parameters in §3.1) if the user switches back.
  2. What if `count` on a `slots` parameter evaluates at runtime to `< 0`, a non-integer, or `> 256`?
  3. When a recipe changes (e.g. `allows_productivity` becomes `false`), §3.1 says a disallowed slot value falls back to default (`null`). It should explicitly confirm that fallback is **per individual slot** (clearing only the disallowed productivity modules while keeping valid speed modules in other slots).
- **Suggested Fix:**
  Specify in §3.1 / §4.5 Step 1 that for `slots`:
  - `count` must evaluate to an integer in `[0, 64]` (otherwise runtime `ERROR`).
  - Evaluation considers only the first `count` slots (padding with `null` if the stored array is shorter, and keeping any extra stored elements past `count` untouched in `node.params` with a `WARNING` if non-empty).
  - `allowed_if` is checked per individual slot in `0 .. count-1`; any disallowed slot evaluates as `null` (`empty`) with a `WARNING`.

---

#### R-16 · MEDIUM · Worked Example 10.2 has a GTNH overclocking math bug for recipes with `eut < 8` (and `eut == 0`)
- **Where:** Spec §10.2 (lines 908–949).
- **What goes wrong:**
  In Example 10.2, `gt_single` writes:
  `"oc_v": "max(0, floor(log(v / recipe.eut, 4) + 1e-9))"`
  and tests it on `macerate-crushed-copper` with `"eut": 2` at MV (`v = 128`) and HV (`v = 512`), getting `oc = 3` at MV (`50 ticks`) and `oc = 4` at HV (`25 ticks`).
  1. In GT5u / GTNH, voltage tiers start at Tier 0 (ULV, $\le 8\text{ EU/t}$) or Tier 1 (LV, $\le 32\text{ EU/t}$). A recipe with `eut = 2` is ULV ($T_{\text{rec}} = 0$, or LV $T_{\text{rec}} = 1$ in GTNH macerator recipes): at LV ($T=1, v=32$) it overclocks at most **1 time** ($T_{\text{mach}} - T_{\text{rec}} = 1$), at MV ($T=2$) **2 times**, and at HV ($T=3$) **3 times**. Dividing `v` by `recipe.eut` (`128 / 2 = 64 = 4^3`) treats `2 EU/t` as "two tiers below ULV", giving **1 extra overclock** at every tier (`oc = 2` at LV, `oc = 3` at MV, `oc = 4` at HV) and computing `2 * 4^4 = 512 EU/t` instead of `2 * 4^3 = 128 EU/t` at HV!
  2. If any recipe has `eut: 0` (the default declared on line 873!), `v / recipe.eut` divides by zero.
- **Suggested Fix:**
  In Example 10.2, compute recipe tier or clamp the denominator to ULV voltage (`8`):
  `"oc_v": "max(0, floor(log(v / max(8, recipe.eut), 4) + 1e-9))"` (and update the hand-calculated numbers in lines 937–949 accordingly: at MV, `oc = 2`, `craft_time = 5.0 s`, `eut = 32`; at HV, `oc = 3`, `craft_time = 2.5 s`, `eut = 128`).

---

#### R-17 · LOW · Reference port selection and negative port amount checks need a `kZeroEpsilon` (`1e-9`) guard
- **Where:** Spec §5.7 (line 538), §7.2 (lines 608–610).
- **What goes wrong:**
  1. If a formula like `port.amount - boost` evaluates to `-1e-17` due to IEEE-754 rounding, §5.7 (*"negative port amount"*) puts the node into `ERROR`.
  2. If a port amount evaluates to `1e-16` (`> 0`), §7.2 can pick it as the reference port $R_{\text{ref}}$, yielding $w_n = t'_n / a_{\text{ref}} \approx 10^{16}$ and destroying GLOP numerical stability.
- **Suggested Fix:**
  In §5.7 and §7.2: treat `amount_per_craft < -1e-9` as a runtime `ERROR`, clamp `amount_per_craft` in `[-1e-9, 1e-9]` to `0.0`, and require `amount_per_craft > 1e-9` for active/reference ports.

---

#### R-18 · LOW · Canonical unit conventions for `is_rate: true` metrics and conditional metric visibility are under-specified
- **Where:** Spec §2.3 (line 119), §6.1, §6.2.
- **What goes wrong:**
  1. §6.1 defines `is_rate: true` (*"the value is per second and gets converted to the project's rate unit for display"*). For `per_craft` metrics (`coeff * node.crafts`), `node.crafts` is already in crafts/s, so `coeff` must be **amount per craft**; whereas for `per_machine` (`coeff * node.machines`) and `value`, `coeff` / `value` must be **amount per second**. This distinction is not stated in §6.2.
  2. Metrics on a profile have no `visible_if` or `enabled` field, so recipe-specific readouts (e.g. Stationeers furnace $[P_{\min}, P_{\max}]$ windows or Satisfactory variable peak power on machines that share a profile) are shown as `0` on recipes where they do not apply.
- **Suggested Fix:**
  Clarify the canonical units of `per_machine` (units/s/machine), `per_craft` (units/craft), and `value` (units/s) when `is_rate: true` in §6.2, and allow an optional `"enabled"` bool expression on `profiles.<id>.metrics.<metric>` (when `false`, the metric is hidden on that node and contributes `0` to totals).

---

## 2. Task 1: Expressing Every Must-Have Mechanic from the Matrix

Below, every must-have mechanic from [`docs/design/mechanics-matrix.md` §2.1–§2.13](file:///home/hagoel/Projects/factory-planner/docs/design/mechanics-matrix.md#L91-L167) is tested against the v4 spec.

### 2.1 Satisfactory

| Mechanic | Status | Notes |
|---|---|---|
| **M1, M2: Clock Speed ($1\dots 250\%$) & Power Shards** | **Expressible** | Covered by Example 10.1. Dynamic clock ceiling by shard count (`"max": "100 + 50 * param.shards"`) also works if `shards` precedes `clock` in `params`. |
| **M3: Somersloop Amplification** | **Expressible** | Covered by Example 10.1 (`amp = 1 + param.somersloops / machine.somersloop_slots`). |
| **M4, M5: Non-Linear Power & Variable-Power Recipes** | **Expressible** | M4 is in Example 10.1. M5 (Particle Accelerator / Converter / Quantum Encoder) works by adding recipe attributes `power_min` and `power_max` (snippet below). |
| **M6: Miner Resource Node Purity & Belt Cap** | **Expressible** | Covered via an `enum` parameter `purity` and belt-rate clamping in `craft_time` (snippet below). |

**Snippet — M5 (Variable-Power Recipes) & M6 (Miner Purity + 1200/min Belt Cap):**
```json
{
  "game": { "id": "satisfactory", "name": "Satisfactory", "data_version": "1.0",
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
      "craft_time": "max(recipe.time / (machine.speed * param.purity.mult * c), 1 / game.max_belt_rate_s)",
      "metrics": { "power": { "per_machine": "machine.power * c ^ 1.321928" } }
    }
  }
}
```

---

### 2.2 Factorio (2.0 & Space Age)

| Mechanic | Status | Notes |
|---|---|---|
| **M7: Machine Module Slots** | **Expressible** | Covered by Example 10.5 (`slots` parameter + `sum(param.modules.speed)`). |
| **M8: 2.0 Beacons ($\sqrt{n}$ Diminishing Returns & Beacon Quality)** | **Expressible** | Covered by Example 10.5; beacon quality adds a `beacon_quality` `enum` parameter replacing constant `game.beacon_efficiency`. |
| **M9, M10: Infinite Productivity Research, $+300\%$ Cap & Catalyst Exclusion** | **Awkward** | `port.catalyst` and `clamp(p, 0, 3)` work cleanly, **but** without `lookup` over `globals` (§12.1), mapping ~12 per-recipe productivity researches (`steel`, `lds`, `blue_circuit`, `plastic`, `rocket_part`, `scrap`, …) requires a 12-level nested `if(...)` chain in `let`! |
| **M11: Quality Tiers & Cascading Quality Splits** | **Partial (Non-goal)** | Machine/module/beacon quality multipliers work via `enum`/`slots` options. Probabilistic 5-tier output splitting is explicitly deferred (§11). |
| **M12, M13: Burner / Fluid Fuels & Spoilage Approximation** | **Expressible (M13 without ash) / Awkward** | M13 fuel input port works via `extra_ports` with `resource_param: "fuel"` and `allowed_if: "option.fuel_cat == machine.fuel_cat"`. M12 spoilage parameter works via a `freshness_pct` parameter and a conditional `spoilage` output port on Gleba recipes. |

**Snippet — M9 (Nested `if` for Per-Recipe Research), M12 (Spoilage), & M13 (Burner Fuel Input):**
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
    "prod_steel":   { "label": "Steel prod. level",   "type": "int", "default": 0, "min": "0", "max": "30" },
    "prod_plastic": { "label": "Plastic prod. level", "type": "int", "default": 0, "min": "0", "max": "30" },
    "prod_lds":     { "label": "LDS prod. level",     "type": "int", "default": 0, "min": "0", "max": "30" }
  },
  "parameters": {
    "fuel": { "label": "Fuel", "type": "resource_choice", "tag": "fuel",
              "allowed_if": "option.fuel_cat == machine.fuel_cat" },
    "spoil_pct": { "label": "Transit spoilage", "type": "number", "default": 0,
                   "min": "0", "max": "99", "unit": "%" }
  },
  "profiles": {
    "fa_burner_crafter": {
      "params": ["fuel"],
      "craft_time": "recipe.time / machine.speed",
      "extra_ports": [
        { "id": "fuel", "dir": "in", "resource_param": "fuel",
          "amount": "(machine.power * craft_time) / (machine.burner_eff * param.fuel.fuel_mj)" }
      ]
    },
    "fa_space_age_crafter": {
      "extends": "fa_crafter",
      "let": {
        "tech_lvl": "if(recipe.prod_tech == \"steel\", global.prod_steel, if(recipe.prod_tech == \"plastic\", global.prod_plastic, if(recipe.prod_tech == \"lds\", global.prod_lds, 0)))",
        "p": "machine.base_prod + sum(param.modules.prod) + 0.1 * tech_lvl"
      }
    }
  }
}
```

---

### 2.3 Factorio Overhauls: Pyanodons & Seablock

| Mechanic | Status | Notes |
|---|---|---|
| **M13, M14: Burner Fuels with Ash / Spent Fuel Container Outputs** | **Blocked (R-05)** | **Cannot be expressed cleanly:** `extra_ports` cannot set `"resource": "param.fuel.burnt_result"`! Setting `"resource_param": "fuel"` on an output extra port would output unburnt fuel (`coal`) instead of `ash` or `empty-canister`. |
| **M15: Tiered Steam & Fluid Temperatures** | **Expressible** | Discrete resources (`steam-165`, `steam-500`, `steam-1000`) work cleanly. |
| **M16: Plant & Animal Living Modules (PyAL)** | **Expressible** | `slots` parameter (or `int` count + `enum` tier) scales `craft_time` and `output_amount`; `value` metric reports `ceil(node.machines) * count(param.creatures)`. |
| **M17: T.U.R.D. Global Tech Upgrades** | **Expressible** | Global `enum` per biological line (`global.turd_ulric == "branch_1"`) referenced in recipe long-form port `enabled` / `amount_expr` and recipe `overrides`. |

**Snippet — M16 (PyAL Living Modules) & M17 (T.U.R.D. Global Switch), plus Why M14 Fails:**
```json
{
  "globals": {
    "turd_moss": { "label": "Moss T.U.R.D.", "type": "enum", "option_set": "turd_branches", "default": "none" }
  },
  "parameters": {
    "creatures": { "label": "Caged creatures", "type": "slots", "option_set": "py_creatures",
                   "count": "machine.module_slots", "allow_empty": false }
  },
  "profiles": {
    "py_husbandry": {
      "params": ["creatures"],
      "let": { "bonus": "sum(param.creatures.speed_bonus)" },
      "requires": [ { "expr": "count(param.creatures) > 0", "message": "Requires at least 1 creature" } ],
      "craft_time": "recipe.time / (machine.speed * bonus)",
      "metrics": {
        "caged_animals": { "value": "ceil(node.machines) * count(param.creatures)" }
      }
    },
    "py_burner_broken_m14": {
      "params": ["fuel"],
      "extra_ports": [
        { "id": "fuel", "dir": "in", "resource_param": "fuel",
          "amount": "machine.power * craft_time / param.fuel.fuel_mj" },
        { "id": "spent", "dir": "out",
          "resource": "ash",
          "notes": "BLOCKED BY R-05: Cannot write param.fuel.burnt_result here! Hardcoding 'ash' breaks canister/nuclear fuels.",
          "amount": "machine.power * craft_time / param.fuel.fuel_mj * param.fuel.ash_ratio",
          "enabled": "param.fuel.ash_ratio > 0" }
      ]
    }
  }
}
```

---

### 2.4 Dyson Sphere Program

| Mechanic | Status | Notes |
|---|---|---|
| **M18: Proliferator Tier & Mode (`Extra Products` vs. `Speedup`)** | **Expressible** | Covered by Worked Example 10.4. Self-sprayed proliferator (`charges * 1.25`) can be added via a `bool` parameter `self_sprayed`. |
| **Assembler / Smelter / Chemical Plant Tier Speeds** | **Expressible** | Covered by `machine.speed`. |
| **M19: Ray Receiver Modes & Graviton Lens Proliferation** | **Expressible** | Snippet below. |
| **M20: Fractionator Belt-Throughput Scaling** | **Expressible** | Snippet below. |

**Snippet — M19 (Ray Receiver) & M20 (Fractionator):**
```json
{
  "parameters": {
    "recv_eff":      { "label": "Receiving efficiency", "type": "number", "default": 100, "min": "30", "max": "100", "unit": "%" },
    "use_lens":      { "label": "Graviton lens", "type": "bool", "default": false },
    "lens_prolif":   { "label": "Lens proliferator", "type": "resource_choice", "tag": "proliferator",
                       "allow_none": true, "default": null, "visible_if": "param.use_lens" },
    "belt_rate":     { "label": "Belt flow rate", "type": "number", "default": 30, "min": "1", "max": "120", "unit": "/s" }
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
| **M21: Tile Attribute Efficiency Sum** | **Expressible** | `tile_efficiency` parameter (`default: 100`, `visible_if: "machine.uses_tiles"`). |
| **M22: Optional Liquid/Item Boosters** | **Awkward (R-02, R-05)** | 1. Because `extra_port.resource` cannot read `machine.booster_res` (R-05), drills using different booster fluids (`water`, `cryofluid`, `nitrogen`, `cyanogen`, `hydrogen`) need separate profiles or multiple `extra_ports`.<br>2. When `x:booster` is wired to a pump, R1 prevents the drill's output from resolving to `TARGET` (R-02). |
| **M23: Overdrive Projector / Dome Speed Aura** | **Expressible** | `overdrive` `enum` parameter (`1.0`, `1.5`, `2.25`, `2.5`). |
| **M24: Erekir Heat Scaling (`HeatCrafter`)** | **Expressible** | `supplied_heat` parameter scaling `craft_time` when `machine.heat_req > 0`. |

**Snippet — M21–M24 (Mindustry Crafter & Drill Profiles):**
```json
{
  "attributes": {
    "machine": { "uses_tiles":   { "type": "bool",   "default": false },
                 "boost_mult":   { "type": "number", "default": 1.0 },
                 "water_rate_s": { "type": "number", "default": 0.0 },
                 "heat_req":     { "type": "number", "default": 0.0 },
                 "max_heat_eff": { "type": "number", "default": 4.0 } }
  },
  "option_sets": {
    "overdrives": {
      "attrs": { "mult": { "type": "number", "default": 1.0 } },
      "options": [
        { "key": "none",      "label": "None",              "attrs": { "mult": 1.0 } },
        { "key": "projector", "label": "Projector (+50%)",  "attrs": { "mult": 1.5 } },
        { "key": "phase",     "label": "Projector + Phase", "attrs": { "mult": 2.25 } },
        { "key": "dome",      "label": "Overdrive Dome",    "attrs": { "mult": 2.5 } }
      ]
    }
  },
  "parameters": {
    "tile_eff":      { "label": "Tile efficiency", "type": "number", "default": 100, "min": "1", "max": "900", "unit": "%",
                       "visible_if": "machine.uses_tiles" },
    "boosted":       { "label": "Liquid boost", "type": "bool", "default": false,
                       "visible_if": "machine.boost_mult > 1" },
    "overdrive":     { "label": "Overdrive", "type": "enum", "option_set": "overdrives", "default": "none" },
    "supplied_heat": { "label": "Supplied heat", "type": "number", "default": 0, "min": "0", "max": "100",
                       "visible_if": "machine.heat_req > 0" }
  },
  "profiles": {
    "mindustry_drill_water": {
      "params": ["tile_eff", "boosted", "overdrive"],
      "let": {
        "eff": "(if(machine.uses_tiles, param.tile_eff / 100, 1)) * (if(param.boosted, machine.boost_mult, 1)) * param.overdrive.mult"
      },
      "craft_time": "recipe.time / (machine.speed * eff)",
      "extra_ports": [
        { "id": "water", "dir": "in", "resource": "water",
          "amount": "machine.water_rate_s * craft_time", "enabled": "param.boosted" }
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
| **M25, M26, M27: Voltage Tiers, Imperfect/Perfect OC, Sub-Tick Parallels, Multi-Amp Hatches** | **Expressible (Fix R-03, R-16)** | Expressible once `allowed_if` / `oc_v` in Example 10.2 are fixed (snippet below). |
| **M28: Coil Heat Gating, 900K EU Discounts & 1800K Perfect OCs** | **Expressible** | Cleanly expressed in `gt_ebf` profile (snippet below). |
| **M29, M30: Tier-Gated Byproducts & Tier-Boosted Chance Outputs** | **Expressible** | Covered by Example 10.2 (`port.min_tier <= t` and `port.chance + port.chance_boost * ...`). |
| **M31: Non-Consumed Catalysts & Programmed Circuits** | **Expressible** | Covered by `recipe.setup` (§2.8). |
| **M32: Multiblock Parallels & Processing Array (PA)** | **Expressible** | Parallel count `par` in `let` multiplies `input_amount` and `output_amount` (snippet below). |
| **M33, M34: CropsNH (2.9) & Forestry/Gendustry Bees** | **Expressible** | M33 is in Example 10.3 (subject to R-02 when wired). M34 bee formula is shown below. |
| **M45: OreDict Input Alternatives** | **Blocked (R-04)** | Without `resource_param` on recipe inputs, OreDict slots must either be expanded into duplicate recipes or canonicalized to a single representative item by the importer. |

**Snippet — M26, M27, M28, M32 (GTNH EBF with Multi-Amp Hatches, Coils, Discounts, Perfect OCs & Sub-Tick Parallels) & M34 (Bees):**
```json
{
  "option_sets": {
    "gt_coils": {
      "attrs": { "heat": { "type": "number", "default": 1801 } },
      "options": [
        { "key": "cupronickel", "label": "Cupronickel (1801K)", "attrs": { "heat": 1801 } },
        { "key": "kanthal",     "label": "Kanthal (2701K)",     "attrs": { "heat": 2701 } },
        { "key": "nichrome",    "label": "Nichrome (3601K)",    "attrs": { "heat": 3601 } }
      ]
    },
    "bee_housings": {
      "attrs": { "mod": { "type": "number", "default": 0 }, "tier": { "type": "number", "default": 1 } },
      "options": [
        { "key": "apiary",     "label": "Apiary",            "attrs": { "mod": -0.9, "tier": 1 } },
        { "key": "alveary",    "label": "Alveary",           "attrs": { "mod": 0.0,  "tier": 1 } },
        { "key": "ind_apiary", "label": "Industrial Apiary", "attrs": { "mod": 2.0,  "tier": 8 } }
      ]
    }
  },
  "parameters": {
    "hatch_amps": { "label": "Total hatch amps", "type": "int", "default": 2, "min": "2", "max": "65536" },
    "coil":       { "label": "Heating coil", "type": "enum", "option_set": "gt_coils", "default": "cupronickel" },
    "max_par":    { "label": "Max parallels", "type": "int", "default": 1, "min": "1", "max": "256" },
    "housing":    { "label": "Housing", "type": "enum", "option_set": "bee_housings", "default": "ind_apiary" },
    "frame_mod":  { "label": "Frame bonus", "type": "number", "default": 0, "min": "0", "max": "10" }
  },
  "profiles": {
    "gt_ebf": {
      "params": ["voltage_tier", "hatch_amps", "coil", "max_par"],
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
      "input_amount": "port.amount * total_par",
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
| **M35: Workers, Electricity (kW) & Computing (TFlops) Totals** | **Expressible** | Cleanly modeled via `metrics` (`per_machine` for kW, `value` with `ceil(node.machines)` for Workers and TFlops). |
| **M36: Maintenance I / II / III Consumption** | **Awkward (R-05)** | Post-solve totals work via 3 metrics (`maint_1`, `maint_2`, `maint_3`). Wiring Maintenance Depots via an `extra_port` requires 3 separate `extra_ports` on the profile (`x:maint_1`, `x:maint_2`, `x:maint_3`) gated by `machine.maint_tier == 1/2/3` because `extra_port.resource` cannot read `machine.maint_res` (R-05). |
| **M37: Unity Speed Boosts** | **Expressible** | `enum` parameter scaling `craft_time`, `electricity`, `maint`, and adding a `unity` metric. |

**Snippet — M35, M36, M37 (Captain of Industry Machine Profile):**
```json
{
  "profiles": {
    "coi_machine": {
      "params": ["unity_boost", "wire_maint"],
      "let": { "b": "param.unity_boost.mult" },
      "craft_time": "recipe.time / (machine.speed * b)",
      "extra_ports": [
        { "id": "maint_1", "dir": "in", "resource": "maintenance-1",
          "amount": "machine.maint_rate_s * b * craft_time",
          "enabled": "param.wire_maint && machine.maint_tier == 1" },
        { "id": "maint_2", "dir": "in", "resource": "maintenance-2",
          "amount": "machine.maint_rate_s * b * craft_time",
          "enabled": "param.wire_maint && machine.maint_tier == 2" }
      ],
      "metrics": {
        "workers": { "value": "ceil(node.machines) * machine.workers" },
        "kw":      { "per_machine": "machine.kw * b" },
        "tflops":  { "value": "ceil(node.machines) * machine.tflops" },
        "unity":   { "value": "ceil(node.machines) * param.unity_boost.unity_cost" }
      }
    }
  }
}
```

---

### 2.8 Oxygen Not Included

| Mechanic | Status | Notes |
|---|---|---|
| **M39: Geyser Eruption & Dormancy Time-Averaging** | **Expressible** | Node parameters compute average yield (`PRE-SOLVE`) and dormancy buffer storage mass (`POST-SOLVE`). |
| **M40: Wild vs. Domesticated Plants & Farmer's Touch** | **Blocked / Awkward (R-06, R-07)** | 1. Because `port_enabled` has no `port.dir` (R-06), writing `"port_enabled": "param.domesticated"` in the profile disables the **harvest output port** too! Every plant recipe must tag its inputs with a custom port attribute `is_irrigation: true`.<br>2. When `domesticated = false` disables all inputs, R1 still treats the plant as a non-raw node (R-07). |
| **M38: Parameterized Thermal Output & Net Heat (`kDTU/s`)** | **Awkward** | Because `metrics` cannot iterate over `port.*` (`port.*` is only in scope inside port formulas, §5.4), `Q_net` cannot sum $\sum R_o c_o T_o - \sum R_i c_i T_i$ dynamically from `port.resource.shc`; the importer must precompute per-recipe heat capacity attributes `recipe.in_shc_sum` and `recipe.out_shc_sum`. |

**Snippet — M39 (Geyser) & M40 (Plants, showing the `port.is_irrigation` workaround for R-06):**
```json
{
  "attributes": {
    "port": { "is_irrigation": { "type": "bool", "default": false } }
  },
  "profiles": {
    "oni_geyser": {
      "params": ["peak_g_s", "erupt_s", "period_s", "active_cyc", "total_cyc"],
      "let": {
        "avg_g_s": "param.peak_g_s * (param.erupt_s / param.period_s) * (param.active_cyc / param.total_cyc)"
      },
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
      "port_enabled": "!port.is_irrigation || param.domesticated",
      "extra_ports": [
        { "id": "micronutrient", "dir": "in", "resource": "micronutrient-fertilizer",
          "amount": "(5 / 600) * craft_time",
          "enabled": "param.domesticated && param.farmers_touch" }
      ]
    }
  }
}
```

---

### 2.9 Anno 1800

| Mechanic | Status | Notes |
|---|---|---|
| **M41: Additive Productivity (Electricity, Tractors, Silos, Working Conditions)** | **Expressible** | Cleanly expressed via `let` sum and `extra_ports` for Tractor fuel and Silo grain. |
| **M43: Extra Goods Every $N^{\text{th}}$ Cycle** | **Expressible** | Primary output boost or an `extra_port` (`dir: "out"`, `resource_param: "extra_good"`, `amount: "1 / param.extra_every_n"`). |
| **M42: Specialist Input Replacements** | **Blocked (R-04)** | Recipe input ports (`recipe.in`) cannot change their `res` dynamically via a parameter or expression. |
| **M35: Tiered Workforce & Maintenance Totals** | **Expressible** | Covered by `metrics`. |

**Snippet — M41 & M43 (Anno 1800 Farm / Factory Profile):**
```json
{
  "profiles": {
    "anno_farm": {
      "params": ["work_cond_pct", "tractor", "silo", "tu_prod_pct", "extra_good", "extra_cycles"],
      "let": {
        "prod": "max(0.1, 1 + (param.work_cond_pct + param.tu_prod_pct) / 100 + if(param.tractor, 2.0, 0) + if(param.silo, 1.0, 0))"
      },
      "craft_time": "recipe.time / prod",
      "extra_ports": [
        { "id": "fuel",  "dir": "in",  "resource": "tractor-fuel",
          "amount": "(1 / 180) * craft_time", "enabled": "param.tractor" },
        { "id": "grain", "dir": "in",  "resource": "grain",
          "amount": "(1 / 300) * craft_time", "enabled": "param.silo" },
        { "id": "bonus", "dir": "out", "resource_param": "extra_good",
          "amount": "1 / param.extra_cycles", "enabled": "is_set(param.extra_good)" }
      ]
    }
  }
}
```

---

### 2.10 Against the Storm

| Mechanic | Status | Notes |
|---|---|---|
| **M44: Recipe Star Ratings (☆ / ★ / ★★ / ★★★)** | **Expressible** | Distinct recipes per star tier restricted to the corresponding building categories. |
| **M45: Ingredient Category Slot Selection** | **Blocked (R-04)** | Core mechanic of Against the Storm: recipe input slots accept interchangeable goods from a recipe-slot-specific category (often with different amounts per good). Blocked because `recipe.in` ports only take a fixed `res` string. |
| **M46: Rainpunk Engines & Double Production (Crit) Chance** | **Expressible (Awkward water type, R-05)** | Crit chance (`1 + p_crit`) and speed scale `output_amount` and `craft_time`; rainwater requires 3 profiles (`ats_craft`, `ats_industry`, `ats_food`) or 3 `extra_ports` unless R-05 is fixed. |

**Snippet — M46 (Against the Storm Rainpunk Engine & Crit Yield):**
```json
{
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
        { "id": "drizzle", "dir": "in", "resource": "drizzle-water",
          "amount": "param.rain_level * (2 / 60) * craft_time",
          "enabled": "param.rain_level > 0 && machine.water_type == 1" }
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
| **M48: Blaze Burner Heat State & Fuel Port (`Kindled` vs. `Seething`)** | **Expressible (via 2 extra ports)** | Uses two conditional `extra_ports` (`x:coal` for `recipe.heat_level == 1` and `x:blaze_cake` for `recipe.heat_level == 2`), or 1 port if R-05 is fixed. |
| **M49: Sequenced Assembly with Salvage Loops** | **Expressible** | Composite recipe with fractional `chance` on primary and salvage outputs. |

**Snippet — M47 & M48 (Create Kinetic Basin with Blaze Burner):**
```json
{
  "profiles": {
    "create_basin": {
      "params": ["rpm"],
      "craft_time": "recipe.time / param.rpm",
      "extra_ports": [
        { "id": "kindled_fuel",  "dir": "in", "resource": "coal",
          "amount": "craft_time / 80",  "enabled": "recipe.heat_level == 1" },
        { "id": "seething_fuel", "dir": "in", "resource": "blaze-cake",
          "amount": "craft_time / 160", "enabled": "recipe.heat_level == 2" }
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
| **M50: Workday Hours & Beaver vs. Bot Worker Speed + Bot Consumables** | **Expressible** | Global `work_hours` and per-node `worker_type` parameter with conditional `extra_ports` for bot consumables. |
| **M51: Seasonal Cycle Averaging (Temperate / Drought / Badtide)** | **Expressible** | Globals `wet_days`, `drought_days`, `badtide_days` scale seasonal extractors (`PRE-SOLVE`) and compute storage buffer (`POST-SOLVE`). |
| **M35: Mechanical Power (`hp`) & Worker Count** | **Expressible** | Covered by `metrics`. |

**Snippet — M50 & M51 (Timberborn Workplace & Seasonal Extractor):**
```json
{
  "globals": {
    "work_hours":   { "label": "Workday hours", "type": "int", "default": 16, "min": "1", "max": "24" },
    "wet_days":     { "label": "Temperate days", "type": "number", "default": 15, "min": "1", "max": "100" },
    "drought_days": { "label": "Drought days",   "type": "number", "default": 10, "min": "0", "max": "100" },
    "badtide_days": { "label": "Badtide days",   "type": "number", "default": 5,  "min": "0", "max": "100" }
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
        { "id": "biofuel", "dir": "in", "resource": "biofuel",
          "amount": "machine.workers * (1 / 86400) * craft_time", "enabled": "param.is_bot" }
      ],
      "metrics": {
        "hp": { "per_machine": "machine.hp" },
        "beavers": { "value": "if(param.is_bot, 0, ceil(node.machines) * machine.workers)" },
        "bots":    { "value": "if(param.is_bot, ceil(node.machines) * machine.workers, 0)" }
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
| **M52b: Furnace Pressure $[P_{\min}, P_{\max}]$ & Temperature $[T_{\min}, T_{\max}]$ Readouts** | **Awkward (R-18)** | Requires 4 separate `aggregate: "none"` metrics (`p_min`, `p_max`, `t_min`, `t_max`), which display `0` on un-windowed recipes unless conditional metric visibility (R-18) is added. |
| **M35: Electrical Power (`W`)** | **Expressible** | Standard `power` metric. |

**Snippet — M52 (Stationeers Furnace Profile):**
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
        "p_min": { "value": "recipe.p_min_kpa" },
        "p_max": { "value": "recipe.p_max_kpa" },
        "t_min": { "value": "recipe.t_min_k" },
        "t_max": { "value": "recipe.t_max_k" }
      }
    }
  }
}
```

---

## 3. Task 2: Expression Language & Validation Rules Audit Summary

1. **Contradiction in `allowed_if` fallback vs. `requires` (R-03):** Worked Example 10.2 filters `voltage_tier` by `option.v >= recipe.eut` in `allowed_if` AND checks `v >= recipe.eut` in `requires`. When a recipe's EU/t exceeds the machine's `max_tier`, `allowed_if` rejects all options, leaving `param.voltage_tier` undefined in Step 2 (`let`) before Step 3 (`requires`) ever runs.
2. **Static-context evaluation & cascade ownership (R-08):** Parameter definitions are global, whereas `params` ordering is per profile. `V-EXPR-5` must drop the offending **profile** (not the parameter) when a profile lists parameters out of dependency order, and Step 1 of §4.5 must short-circuit hidden parameters (`visible_if == false`) before evaluating `min`/`max`/`count`/`allowed_if`.
3. **Unset `allow_none` and empty `slots` runtime traps (R-09):** `min(list)` and `max(list)` throw a runtime error on empty lists (§5.6), which puts every default node with `slots` into `ERROR` status; reading `.attr` on an unset `allow_none` parameter is omitted from §5.7's runtime error list.
4. **Missing `port.dir` in `port_enabled` (R-06):** `port_enabled` runs on both input and output ports, but `port.*` lacks `port.dir`, making it impossible for a profile formula to disable only inputs or only outputs.
5. **Override-induced `let` cycles & identifier shadowing (R-10):** Recipe `overrides.let` can create cycles with profile `let` variables (`V-EXPR-6` wrongly drops the profile instead of the recipe), and `let` variable names are not prevented from shadowing reserved root identifiers (`machine`, `param`, `port`, `craft_time`) or built-in functions.
6. **Validation gaps (R-14, R-15):** `recipe.setup` is unvalidated; `globals` static-expression cycles are unvalidated; `tables` value types are unvalidated; dynamic `slots.count` truncation/bounds are unspecified; and duplicate JSON keys within a single file require a parser callback in `nlohmann::json`.

---

## 4. Task 3: Solver Linearity, Correctness & 50k-Recipe Load Performance Summary

1. **LP Linearity is preserved:** Because `V-EXPR-3` bans `node.machines`, `node.crafts`, and solved port/wire rates from `static` and `pre-solve` contexts at compile time, all stoichiometry ratios ($a_p / a_{\text{ref}}$), cycle times ($t'_n$), and linear metric coefficients (`per_machine`, `per_craft`) are pure constants during `FactorySolver::solve()`.
2. **Solver Topology & Conservation Bugs (R-01, R-02, R-07, R-17):**
   - **R-01 (`CRITICAL`):** Keeping `INACTIVE`/`INVALID` wires in $W^+(p), W^-(p)$ and inactive ports in $\mathcal{X}_K$ while merely fixing $R_p = 0, F_c = 0$ breaks `FactorySolver` conservation equations, No-Overflow Rules 1 & 2 ($E_p = 0$), Sink-Dedicated Subgraph ($\mathcal{S}^+_K$) detection, component partitioning, and Stage 2c/4 splitter fairness.
   - **R-02 & R-07 (`CRITICAL`/`HIGH`):** Defining raw sources and sinks solely by static `recipe.in.empty()` / `recipe.out.empty()` (ignoring wired active `extra_ports` and runtime-disabled recipe ports) breaks `AUTO` constraint mode resolution in `FactorySolver::analyzeComponent`.
   - **R-17 (`LOW`):** Reference port selection needs an `amount_per_craft > 1e-9` threshold so near-zero port amounts do not create $10^{16}$ LP coefficients.
3. **50k-Recipe Loading Bottlenecks (R-11):**
   - Unbounded Levenshtein "did you mean" scans on broken shards ($O(\text{errors} \times \text{resources})$),
   - Un-interned AST compilation across thousands of identical port/override expressions (`V-OVR-2`),
   - Eager formatting of JSON pointer strings during DOM traversal, and
   - Naive $O(R \cdot N)$ scans for `V-W-2` unused-entity warnings.

---

## 5. Task 4: Hand-Authoring UX Judgment

With the in-app GameData editor removed (Decision D2), authors creating or editing packages by hand face four main sources of friction:

1. **Broken `params` inheritance in `extends` (R-12):** Replacing `params` instead of appending/merging when extending a profile forces authors to duplicate parent parameter lists in every child profile or suffer immediate `V-EXPR-5` errors on inherited `let` formulas.
2. **Stringified numbers and quote-escaping noise (R-13):** Forcing `"min": "1", "max": "250"` to be strings (right next to `"default": 100` as a number) causes constant `V-STRUCT-2` errors, and forbidding single-quoted strings (`'extra'`) inside expressions forces ugly `\"` escaping throughout every JSON formula.
3. **JSON path vs. expression path mismatch (§2.5–§2.8):** In JSON, custom fields live inside `"attrs": { "power": 4 }`, whereas in expressions they are accessed directly at the root as `machine.power` (not `machine.attrs.power`). While concise in expressions, the error message for `V-EXPR-2` should explicitly detect `machine.attrs.x` / `recipe.attrs.x` and suggest `machine.x` / `recipe.x`.
4. **Mandatory upfront attribute declarations (`V-ATTR-1`):** Requiring every machine, recipe, port, and resource attribute to be pre-declared in top-level `"attributes"` catches typos at 50k scale (good for importers), but adds boilerplate for small hand-written files. Allowing `V-ATTR-1`'s error message to show the exact snippet to add to `"attributes"` keeps strictness without frustrating hand authors.

