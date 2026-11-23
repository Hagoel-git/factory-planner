# Factory Planner — GameData v4 Author Reference

This is the author-facing reference for creating, converting, validating, and packaging **GameData v4** game packs for Factory Planner. It covers the schema, the expression language, validation rules with fixes, five worked examples, a cross-game mechanic cookbook, data extraction sources, converter rules, and the `gamedata_tool` CLI.

The formal design specification lives in `docs/design/gamedata-v4-spec.md`.

---

## 1. Quick Start & Package Layout

### 1.1 How to add a game

There are two ways to author a game pack:

1. **AI-assisted converter workflow (recommended for real games):** Place your raw game dump in `raw/<game>/` (inside a standalone workspace or a checkout of `factory-planner-packs`), paste the bootstrap prompt below into an AI coding agent, and co-design a deterministic converter + template + regression cases verified by `gamedata_tool`:
   ```text
   You are helping me build a Factory Planner game-data pack in this folder. My raw game data
   is in raw/. Run `gamedata_tool guide prompt` and follow it exactly. Then run
   `gamedata_tool guide reference` and read it fully before proposing anything.
   ```
2. **Manual / small pack:** Scaffold a valid starting package with `gamedata_tool init <dir>`, edit `game.json`, and iterate with `gamedata_tool validate <dir> --werror` and `gamedata_tool eval`.

### 1.2 Directory layout

Factory Planner discovers packages at `<game_data_dir>/<game>/<package>/game.json`.

```text
<game>/<package>/
  game.json                     manifest (small games can put everything here)
  resources.json                optional shard(s) listed in "include"
  machines.json                 optional shard(s)
  waivers.json                  optional warning waivers shard (§4.1)
  recipes/*.json                optional recipe shards (split by category / recipe map)
  icons.pak                     packed icons for releases (checked first)
  icons/                        loose PNGs for local authoring (gitignored in packs repos)
    resources/<id>.png
    machines/<id>.png
    options/<set>/<key>.png
```

- **Sharding (`"include"`):** Each entry in `"include"` is a relative file or directory path inside the package root (`V-INC-3` rejects paths escaping the package). Directories contribute every `*.json` recursively in deterministic byte-sorted relative path order (`game.json` is skipped; duplicate canonical paths are deduplicated). Shards may contain any manifest top-level key **except** `schema_version`, `game`, `units`, `include`, and `icon_root`. Maps merge by key (`/attributes` merges at `/attributes/<entity_kind>/<attr>`); duplicate keys or entity IDs across files or inside a single JSON object are errors (`V-ID-2`).
- **Icons & lookup order:** Icons must be **square PNGs of at most 64×64** (`V-ICON-1`, checked by `gamedata_tool`). Lookup order is:
  1. `<package_root>/icons.pak`
  2. `<icon_root>/icons.pak` (when `"icon_root"` points elsewhere inside `<game>/`, e.g. `"../icons"`)
  3. Loose PNGs under `<icon_root>/` (default `"icons"`).
  Option icons (`options/<set>/<key>.png`) render in `enum` dropdowns, closed combo previews, node-body selectors, Project Globals combos, and `slots` rows. Missing/waived option icons omit placeholder boxes so text-only option sets render cleanly.
- **"No icons" mode:** If neither `icons.pak` nor `<icon_root>/` exists, the package is in valid "no icons" mode: the tool emits a single info note (no `V-W-1` warnings) and passes `--werror`. The app uses fallback icons (or cross-pack icon fallback if enabled in Settings).
- **Strict JSON:** No comments or trailing commas. Any object may carry a `"notes"` string (ignored by the loader). Unknown keys are errors (`V-STRUCT-1`).
- **Identifiers:**
  - Entity `id`, option `key`, `category` / `categories`, and `tag` / `tags`: lowercase slug matching `[a-z0-9_-]+` (matching `slugify()`, §8.2).
  - `parameters`, `globals`, `let` names, and attribute names: `[a-z_][a-z0-9_]*`.
  - Names must not shadow reserved roots (`game`, `global`, `machine`, `recipe`, `param`, `port`, `node`, `option`, `craft_time`, `true`, `false`), built-in fields (§3.4), or built-in functions (§3.5).

---

## 2. Schema Cheat Sheet

### 2.1 Top-level manifest (`game.json`)

| Key | Req | Type | Purpose |
|---|---|---|---|
| `schema_version` | yes | `4` | Must be `4` (`F-3`). |
| `game` | yes | object | `{ "id", "name", "data_version", "attrs" }`. `id` is stored in `.fpp` project files; `game.attrs` are typed JSON constants (finite number, bool, or string) readable as `game.<attr>`. |
| `units` | yes | object | Recipe time base and default project rate unit (§2.2). |
| `resource_kinds` | no | object | Display units & scale factors (§2.2). Default: single kind `"item"`. |
| `attributes` | no | object | Typed declarations for `machine`, `recipe`, `port`, `resource` (§2.3). |
| `tables` | no | object | Named numeric lookup tables: non-empty array or string→number map (§2.3). |
| `option_sets` | no | object | Named option lists with typed attributes for `enum` and `slots` (§2.3). |
| `parameters` | no | object | Per-node parameter definitions (§2.4). |
| `globals` | no | object | Per-project parameter definitions (§2.4). |
| `metrics` | no | object | Post-solve metric registry (§2.5). |
| `profiles` | no | object | Reusable formula sets attached to machines (§2.6). |
| `resources`, `machines`, `recipes` | no | arrays | Entity lists (§2.7). |
| `waive_warnings` | no | array | Scoped warning waivers; recommended in a separate `waivers.json` shard (§4.1). |
| `include` | no | string[] | Shard files or directories (§1.2). |
| `icon_root` | no | string | Relative icon directory inside `<game>/` (default `"icons"`). |
| `notes` | no | string | Free-text author note (allowed on any entity/section). |

### 2.2 Units & resource kinds

```json
"units": { "recipe_time": "tick", "ticks_per_second": 20, "default_rate": "s" },
"resource_kinds": {
  "item":  { "label": "Item",  "display_unit": "",   "display_scale": 1 },
  "fluid": { "label": "Fluid", "display_unit": "m³", "display_scale": 1 }
}
```

- `recipe_time`: `"s"` or `"tick"`. The loader converts `recipe.time` to seconds (`recipe.time_raw` preserves the raw value).
- `ticks_per_second`: number `> 0`. Required whenever `recipe_time` is `"tick"` or `"tick"` is offered as a rate unit. Available as `game.ticks_per_second`.
- `default_rate`: `"tick"`, `"s"`, `"min"`, or `"h"`. Initial display unit for new projects. **Internally the solver and `.fpp` constraints are always per second**; changing rate unit only converts display/input.
- `resource_kinds.<kind>`: recipes use the game's native amount; `display_scale` multiplies values for display (e.g. store `L`, show `kL` with `0.001`).

### 2.3 Attributes, tables & option sets

```json
"attributes": {
  "machine":  { "power": { "type": "number", "default": 0 } },
  "recipe":   { "eut":   { "type": "number", "default": 0 } },
  "port":     { "min_tier": { "type": "number", "default": 0 } },
  "resource": { "fuel_value": { "type": "number", "default": 0 } }
},
"tables": {
  "gt_voltage": [8, 32, 128, 512, 2048],
  "coil_heat":  { "cupronickel": 1801, "kanthal": 2701 }
},
"option_sets": {
  "gt_tiers": {
    "attrs": { "t": { "type": "number", "default": 0 }, "v": { "type": "number", "default": 0 } },
    "options": [
      { "key": "lv", "label": "LV", "attrs": { "t": 1, "v": 32 } },
      { "key": "mv", "label": "MV", "attrs": { "t": 2, "v": 128 } }
    ]
  }
}
```

- Attribute types: `"number"`, `"bool"`, `"string"`. Every declaration requires a `"default"` of matching type.
- Tables must be non-empty and contain only finite numbers (`V-TBL-1`). Array tables are 0-based.
- Option sets must have ≥ 1 option with unique keys (`V-PAR-3`). Option attributes must be declared in the set's `"attrs"` with defaults and cannot be named `key` or `id`.

### 2.4 Parameters & globals

Parameters live under `"parameters"` (per node, opted into via `profile.params`) and `"globals"` (per project, read as `global.<id>`).

| Type | Required / Key Fields | Default Widget | Value in JSON / `eval` |
|---|---|---|---|
| `number` | `default`, optional `min`/`max` (number or static expr), `step`, `unit` | `drag` | number |
| `int` | `default` (integer), optional `min`/`max` (number or static expr), integer `step` (def `1`), `unit` | `stepper` | integer |
| `bool` | `default` (bool) | `checkbox` | `true` / `false` |
| `enum` | `option_set`, optional `default`, `allowed_if`, `allow_none` | `combo` | option key string (or `null` if `allow_none`) |
| `resource_choice` | exactly one of `resources` (non-empty id[]) or `tag` (matches ≥ 1 res), optional `default`, `allowed_if`, `allow_none` | `combo` | resource id string (or `null` if `allow_none`) |
| `slots` | `option_set`, `count` (number or static expr, runtime int in `[0, 64]`), optional `default`, `allowed_if`, `allow_empty` (def `true`) | `slot_row` | array of option keys / `null` |

Common optional fields on all parameters: `label`, `tooltip`, `visible_if` (bool literal or static expr), `ui`:
- `ui.widget`: `"slider"`, `"drag"`, `"input"`, `"stepper"`, `"checkbox"`, `"combo"`, `"slot_row"`.
- `ui.format`: printf-style format (e.g. `"%.4g%%"`).
- `ui.group`: section label in Node Properties (for parameters) and Project Globals (for globals).
- `ui.placement`: `"body"` (shown directly on node card) or `"panel"` (Node Properties panel only, **default**). Applies only to node parameters — globals are always displayed in the **Project Globals** section of Summary & Inspector.

**UX defaults for parameters & globals:**
- **Keep node cards clean:** default node parameters to `"placement": "panel"` so graph nodes stay uncluttered; reserve `"body"` only for a small number of controls users change frequently directly on-canvas (e.g. a module `slot_row`).
- **Use `enum` for discrete game presets:** whenever the game exposes a fixed discrete set of options or non-uniform presets (such as difficulty multipliers `0.25, 0.5, 1, 2, 5, 10, 100`, belt tiers, or voltage tiers), model it as `type: "enum"` + `option_set` (with numeric `attrs` like `mult`) rather than a free-form `number` input. Reserve `number`/`int` for genuinely continuous or arbitrary-step ranges.
- **Option icons are optional:** `enum` and `slots` render `options/<set>/<key>.png` whenever present; for text-only option sets without game artwork, waive `V-W-1` in `waivers.json` (§4.1) and the UI omits placeholder icons cleanly.

**Resolution & fallback rules (pre-solve step 1):**
1. Parameters resolve sequentially in `profile.params` order (so later params may read earlier `param.*` in `visible_if`, `allowed_if`, `min`, `max`, `count`).
2. **Port-bound `resource_choice`:** If referenced by a recipe port's `resource_param` (or reading `port.*` in `visible_if`/`allowed_if`), it auto-hides when the current recipe has no port using it; otherwise `port.*` (except `port.resource`) is bound to that port.
3. **`visible_if` short-circuits:** When `false`, the parameter binds directly to its declared `default` (or first candidate if `allow_none: false`, or `null` / empty slots) without evaluating `min`/`max`/`count`/`allowed_if` or warning. Stored values are preserved in `.fpp` files.
4. **Clamping & fallbacks:** Out-of-range `number`/`int` values clamp during evaluation with a `WARNING` (`int` clamps to `[ceil(min - 1e-9), floor(max + 1e-9)]`; `min > max` → `ERROR`). Disallowed `enum`/`resource_choice` values fall back to `default` (if allowed) → first allowed candidate → `null` (if `allow_none`), with a `WARNING`; zero allowed choices with `allow_none: false` → `BLOCKED`. `slots` checks `allowed_if` per slot in `0..count-1`; disallowed slots become `null` (`WARNING`, or `BLOCKED` if `allow_empty: false`).
5. **Globals** may reference only `game.*` and other globals (resolved in topological order; cycles → `V-EXPR-6`).

### 2.5 Metrics

Registry entry under `"metrics"`:
```json
"power": { "label": "Power", "unit": "MW", "format": "%.2f",
           "display": ["body", "tooltip", "totals"],
           "aggregate": "sum", "split_sign": true, "is_rate": false }
```
- `display`: subset of `["body", "tooltip", "totals"]` (default `["tooltip"]`). **UX default:** use `["tooltip", "totals"]` for routine metrics (power, energy, shards) to keep node cards uncluttered; reserve `"body"` for rare high-signal, node-specific readouts that need to be seen at a glance (e.g. sink points rate or phase completion time).
- `aggregate`: `"sum"` (default), `"max"`, `"min"`, or `"none"`.
- `split_sign`: `true` (only with `"sum"`, `V-MET-1`) shows `+` consumption, `-` generation, and net in totals. Convention: consumption positive, generation negative.
- `is_rate`: `true` converts per-second values to the project's active rate unit and appends the rate suffix (`/tick`, `/s`, `/min`, `/h`) automatically — set `unit` to the **base unit** without `/s` or `/min` (e.g. `"pts"`, not `"pts/min"`). Note that `gamedata_tool eval` and `--cases` always output and compare the canonical **per-second** value.

Profile (or recipe override) metric entry: optional `"enabled"` (bool/pre-solve expr, default `true`) plus **exactly one** form (`V-PROF-3`):
- `"per_machine"`: pre-solve expr → `coeff × node.machines` (LP-linear; best for time-averaged / utilization-proportional metrics).
- `"per_craft"`: pre-solve expr → `coeff × node.crafts` (LP-linear; `node.crafts` is crafts/s).
- `"value"`: post-solve expr (display only; may use `node.machines`, `node.crafts`, `ceil`, etc.). Use `"value"` with `ceil(max(0, node.machines - 1e-6))` whenever a metric counts **whole physical buildings** (peak breaker load, slotted shards/modules, or fixed-draw sinks whose capacity parameter varies `node.machines` without reducing per-building active draw).

Only `OK`/`WARNING` nodes with `enabled == true` and non-error evaluation contribute to totals.

### 2.6 Profiles & extra ports

Built-in `"default"` profile (implicitly inherited by every profile unless `extends` specifies another parent):
```json
"default": {
  "params": [], "let": {}, "requires": [],
  "craft_time": "recipe.time / machine.speed",
  "input_amount": "port.amount * port.chance",
  "output_amount": "port.amount * port.chance",
  "port_enabled": "true", "extra_ports": [], "metrics": {}
}
```

- **Inheritance (`extends`):** `params` appends child entries (deduplicated, keeping first position); `let`, `metrics`, and `extra_ports` merge by key/`id` (preserving parent order in `extra_ports`); `requires` appends; `craft_time`, `input_amount`, `output_amount`, `port_enabled`, `input_enabled`, `output_enabled` replace.
- **Port enable precedence:** port-level `enabled` → `input_enabled` / `output_enabled` → `port_enabled` → `true`.
- **Port amount precedence:** port-level `amount_expr` → `input_amount` / `output_amount`.
- **Extra ports:** Machine/profile-level ports (fuel, spray, water booster, ash):
  ```json
  { "id": "spray", "dir": "in", "resource_param": "prolif",
    "amount": "recipe.input_items_total / param.prolif.charges", "enabled": "is_set(param.prolif)" }
  ```
  - Slot key: **`xin:<id>`** (`dir: "in"`) or **`xout:<id>`** (`dir: "out"`).
  - Resource: **exactly one** of `"resource"` (fixed id), `"resource_param"` (a `resource_choice` param of this profile; `null` makes port inactive), or `"resource_expr"` (pre-solve expr returning resource id string, `""` for inactive, or `resource`).
  - `craft_time` is in scope for extra port `amount` (e.g. `machine.water_per_s * craft_time`).

### 2.7 Resources, machines & recipes

- **Resource:** `{ "id", "name", "kind": "item", "tags": [], "attrs": {}, "notes" }`.
- **Machine:** `{ "id", "name", "profile": "default", "categories": [], "speed": 1.0, "attrs": {}, "notes" }`. `speed > 0`.
- **Recipe:**
  ```json
  { "id": "iron-plate", "name": "Iron Plate", "category": "constructor", "time": 6,
    "in": [["iron-ingot", 3]], "out": [["iron-plate", 2]],
    "setup": [["circuit-1", 1]], "attrs": {}, "overrides": {} }
  ```
  - Must specify **exactly one** of `"category"` or `"machines"` (`V-REF-2`), `time > 0` (`V-NUM-1`), and at least one port in `in` or `out` (`V-NUM-4`).
  - **Short tuple port:** `[res_id, amount]` or `[res_id, amount, chance]`. Prefer short form for 50k-scale packs.
  - **Long object port:** `{ "res", "resource_param", "resource_expr", "amount", "chance": 1, "catalyst": 0, "attrs": {}, "enabled", "amount_expr" }`.
    - `amount > 0`, `chance ∈ (0, 1]`, `catalyst ∈ [0, amount]`.
    - `resource_param` and `resource_expr` are mutually exclusive; at most one recipe port may bind a given `resource_param` (`V-PROF-4`). Optional `"res"` alongside them serves as `port.base_resource` and fallback when `allow_none` is `null`.
  - **Port slot keys:** `in:0`, `in:1`, … and `out:0`, `out:1`, …. **Port order is identity** in saved `.fpp` files — never reorder recipe ports arbitrarily across versions!
  - **`setup`:** Non-consumed build/configuration items (e.g. GTNH programmed circuits, molds, lenses) as `[res_id, amount]` (`amount > 0`). Displayed on the node; excluded from the LP.
  - **`overrides`:** May override `craft_time`, `input_amount`, `output_amount`, `port_enabled`, `input_enabled`, `output_enabled`, existing `let` entries (must keep result type and avoid cycles), any registry `metrics`, and append `requires`. Cannot override `params` or `extra_ports` (`V-OVR-1`). Type-checked against **every** compatible machine profile (`V-OVR-2`).
  - **Raw sources & sinks (no `"nothing"` resource):** A node is a raw source when it has ≥ 1 active output, 0 active recipe inputs (`in:*`), and 0 wired active extra inputs (`xin:*`). It is a sink when it has ≥ 1 active input and 0 active outputs (`out:*` or `xout:*`).

### 2.8 Pre-solve evaluation order & linearity

For every node, `NodeEvaluator` runs before solving:
1. Resolve `param.*` in `profile.params` order (with `visible_if` short-circuiting and fallback rules).
2. Evaluate `let` bindings in topological order.
3. Evaluate `requires` in order; first `false` → status `BLOCKED`, all ports locked to 0.
4. Evaluate `craft_time`; `≤ 0` or non-finite → status `ERROR`.
5. Evaluate recipe ports (`in:*`, `out:*`); `amount < -1e-9` or non-finite → `ERROR`; `|amount| ≤ 1e-9` clamps to `0` and sets `active = false`.
6. Evaluate `extra_ports` (`xin:*`, `xout:*`): `enabled` first, then resource, then `amount` (same epsilon rules).
7. Evaluate metric `enabled` flags and linear `per_machine` / `per_craft` coefficients.

Because every pre-solve formula depends only on fixed parameters, globals, and attributes (never solved flows), `craft_time` and port amounts are constants inside the LP, keeping `FactorySolver` strictly linear.

---

## 3. Expression Language

### 3.1 Syntax & precedence

- **Literals:** decimal/scientific numbers (`12`, `0.25`, `1e-9`, no leading `.`), single- or double-quoted strings (`'extra'` or `"extra"`, escapes `\'`, `\"`, `\\` — **prefer single quotes inside JSON** to avoid `\"`), booleans (`true`, `false`).
- **Precedence (lowest → highest):**
  1. `||` (short-circuit OR)
  2. `&&` (short-circuit AND)
  3. `!` (logical NOT; binds looser than comparisons, so `!a == b` parses as `!(a == b)`)
  4. `==`, `!=`, `<`, `<=`, `>`, `>=` (**non-chaining**: `a < b < c` is a syntax error)
  5. `+`, `-`
  6. `*`, `/`, `%` (`%` is `std::fmod`; zero divisor → runtime error)
  7. Unary `-`
  8. `^` (right-associative power, tighter than unary `-`: `-2^2 == -4`, `2^3^2 == 512`, `2^-1 == 0.5`)
  9. Postfix `.attr`, function call `fn(...)`, parentheses `(...)`
- **No ternary `?:`** — use lazy `if(cond, then_expr, else_expr)`.

### 3.2 Types & unset (`allow_none`) semantics

| Type | Operations |
|---|---|
| `number` | arithmetic, comparisons, numeric built-ins (IEEE-754 double, `-0` normalized to `0`) |
| `bool` | `!`, `&&`, `||`, `==`, `!=`, `if()` condition (no implicit bool↔number conversion) |
| `string` | `==`, `!=` with string, `option<S>`, or `resource` |
| `option<S>` | `.attr`, `.key`, `==`/`!=` with `option<S>`, string literal (key checked at load!), or string expr |
| `resource` | `.attr`, `.id`, `==`/`!=` with `resource`, string literal (id checked at load!), or string expr; `has_tag()` |
| `slots<S>` | `.attr` → `list<number>`; `count()`, `is_set()` |
| `list<number>` | `sum()`, `min()`, `max()` over active non-empty slots in `0..count-1` |

When an `allow_none` parameter/global is `null`: `param.x == 'k'` is `false` and `param.x != 'k'` is `true` (safe). Accessing `.attr`, `.id`, `.key`, `has_tag()`, or `lookup()` on `null` is a runtime `ERROR` — guard with `is_set(param.x)` via short-circuiting `&&`, `||`, or `if()`.

### 3.3 Contexts

| Context | Used In | Available Names |
|---|---|---|
| **globals static** | `globals.*` (`min`, `max`, `count`, `visible_if`, `allowed_if`) | `game.*`, other `global.*`, `option` (in `allowed_if`) |
| **static** | `parameters.*` (`min`, `max`, `count`, `visible_if`, `allowed_if`) | `game.*`, `global.*`, `machine.*`, `recipe.*`, earlier `param.*` in profile, `option` (`allowed_if`), `port.*` excl. `port.resource` (port-bound `resource_choice` only) |
| **pre-solve** | `let`, `requires`, `craft_time`, port/extra-port formulas, metric `enabled`/`per_machine`/`per_craft` | static + all profile `param.*` + `let` names + `craft_time` (after step 4) + `port.*` (recipe port formulas only; `port.resource` only in `enabled`/`amount_expr`) |
| **post-solve** | metric `value` | pre-solve (without `port.*`) + `node.machines`, `node.crafts` |

### 3.4 Built-in fields

- `game.ticks_per_second`, `game.<attr>`
- `machine.id`, `machine.speed`, `machine.<attr>`
- `recipe.id`, `recipe.time` (seconds), `recipe.time_raw` (raw file value), `recipe.input_items_total`, `recipe.input_fluids_total`, `recipe.output_items_total`, `recipe.output_fluids_total` (sums over static-`res` ports of kind `item`/`fluid`), `recipe.<attr>`
- `option.key`, `option.<attr>` (on `option<S>`)
- `resource.id`, `resource.<attr>` (on `resource`, e.g. `param.fuel.id`, `port.resource.fuel_value`)
- `port.dir` (`"in"`/`"out"`), `port.amount`, `port.chance`, `port.catalyst`, `port.index`, `port.base_resource` (if static `res` declared), `port.resource`, `port.<attr>`
- `craft_time` (seconds, available in port, extra-port, and metric formulas)
- `node.machines`, `node.crafts` (crafts/s across all machines; post-solve `value` metrics only)

### 3.5 Built-in functions

| Function | Signature | Behaviour |
|---|---|---|
| `min`, `max` | `(num, …)` \| `(list)` \| `(list, default)` | Scalar (≥ 1 arg) or over non-empty active slots (empty → `default` or `0`). |
| `clamp` | `(x, lo, hi)` | Clamps `x` to `[lo, hi]`; runtime error if `lo > hi`. |
| `abs`, `floor`, `ceil`, `round`, `sqrt` | `(num)` | `round` is half away from zero; `sqrt(x < 0)` → runtime error. |
| `pow`, `exp`, `ln`, `log` | `(a, b)` / `(x)` / `(x, base)` | `pow` matches `^`; `ln(x ≤ 0)`, `log` with `x ≤ 0` or `base ≤ 0` or `base == 1`, non-finite results → runtime error. |
| `if` | `(bool, T, T)` | Lazy: evaluates only the selected branch; branches must have identical types. |
| `lookup` | `(table_lit, key [, default])` | `table_lit` must be a string literal. Array table: integral key (`\|k - round(k)\| ≤ 1e-9`), 0-based. Object table: string, option (`.key`), or resource (`.id`). Missing key without `default` → runtime error. |
| `has_tag` | `(resource, string)` | `true` iff resource's `tags` contains the string. |
| `sum` | `(list)` | Sum over non-empty active slots (`0` if all empty). |
| `count` | `(slots [, key_lit])` | Count of non-empty active slots, or active slots equal to `key_lit`. |
| `is_set` | `(param.x \| global.x)` | `true` if `enum`/`resource_choice` is non-null, or `slots` has ≥ 1 non-empty active slot. |

### 3.6 Determinism, pitfalls & v4 non-goals

- **No `.attrs.` in expressions:** Write `machine.power`, `recipe.eut`, `port.min_tier` — **never** `machine.attrs.power`.
- **Integer boundaries on `log`/`pow`:** Add `+ 1e-9` before `floor` (e.g. `floor(log(v / 8, 4) + 1e-9)`) or precompute integer attributes (like recipe tier) in the converter to avoid cross-platform MSVC/glibc 1-ULP drift.
- **LP epsilon guard on `ceil(node.machines)`:** When counting whole physical buildings in post-solve `value` metrics (shards, modules, peak power, fixed-draw sinks), write `ceil(max(0, node.machines - 1e-6))` (not raw `ceil(node.machines)`) so LP solver floating-point residuals near `0` or integers (`1e-12`, `1.00000001`) never bump `0 → 1` or `1 → 2`.
- **v4 non-goals:** No user-defined functions, loops, list literals, string concatenation, or dynamic global indexing `global[recipe.key]` (branch with `if(...)` or split profiles); no default per-node/per-port flow or machine constraints in game data (constraints live strictly in `.fpp` project files — throughput parameters like belt tier scale `node.machines`, not hard solver caps); no MIP/integer solver rounding; no solver-optimized parameters; no continuous thermodynamic mixing, belt-transit spoilage, or Factorio 5-tier quality cascades.

---

## 4. Validation Rules & How to Fix Them

Severity: **F** = fatal (cannot bypass), **E** = error (blocks clean load; dropped in "Load anyway" cascade), **W** = warning (fails `--werror`).
Cascade order: broken shards → resource kinds/attributes → resources → option sets/tables → parameters/globals → metrics → profiles → machines → recipes.

| Rule | Sev | Meaning | How to fix |
|---|---|---|---|
| `F-1` | F | Manifest missing or unreadable | Pass the package directory or its `game.json`; check permissions. |
| `F-2` | F | Manifest invalid JSON | Fix JSON syntax (remove comments/trailing commas; check encoding UTF-8). |
| `F-3` | F | `schema_version` missing or ≠ 4 | Set `"schema_version": 4` at top level of `game.json`. |
| `F-4` | F | `game.id` missing or invalid slug | Set `"game": { "id": "<slug>", ... }` matching `[a-z0-9_-]+`. |
| `F-5` | F | Invalid `units` | Use `recipe_time` in `s\|tick`, `default_rate` in `tick\|s\|min\|h`, and `ticks_per_second > 0` whenever `tick` is used. |
| `V-INC-1` | E | `include` path not found | Fix the relative path in `"include"` or remove stale entries. |
| `V-INC-2` | E | Shard file invalid JSON | Fix JSON syntax in the reported shard (emit with `allow_nan=False`). |
| `V-INC-3` | E | Shard escapes package dir or uses manifest-only key | Keep paths inside `<package_root>`; move `schema_version`/`game`/`units`/`include`/`icon_root` to `game.json`. |
| `V-STRUCT-1` | E | Unknown JSON key | Fix typo (see suggestion) or move free text into `"notes"`. |
| `V-STRUCT-2` | E | Wrong JSON value type | Match the schema type (e.g. number vs string; array vs object). |
| `V-ID-1` | E | Invalid id/key/tag/category/identifier, or reserved name | Slugify entity/tag/category/option ids (`[a-z0-9_-]+`); use `[a-z_][a-z0-9_]*` for params/globals/`let`/attrs; rename reserved words. |
| `V-ID-2` | E | Duplicate id or map key | Deduplicate or disambiguate deterministically in the converter; check overlapping shards. |
| `V-REF-1` | E | Unknown resource id in port, `setup`, `extra_ports`, `resource_choice`, or `resource_expr` literal | Emit the missing resource in `resources.json` or skip/remap the broken record in the converter. |
| `V-REF-2` | E | Recipe has both/neither `category`/`machines`, or unknown machine id | Provide exactly one of `category` or `machines`, referencing valid machine ids. |
| `V-REF-3` | E | No machine can run recipe | Add the recipe's category to a machine's `categories`, or skip uncraftable recipes. |
| `V-REF-4` | E | Machine references unknown profile | Define the profile under `"profiles"` (or fix typo / use `"default"`). |
| `V-REF-5` | E | Unknown resource kind, unknown `option_set`, invalid `resource_choice` (`resources` vs `tag` empty/both), or `default` outside candidates | Declare the kind/option set; supply exactly one non-empty `resources` or matching `tag`; align `default`. |
| `V-ATTR-1` | E | Undeclared attribute on entity or option | Declare the attribute with `type` and `default` under `/attributes/<kind>` or `/option_sets/<set>/attrs`. |
| `V-ATTR-2` | E | Wrong attribute value type, missing default, or shadowed built-in name | Match declared type; add `default`; don't shadow `id`, `key`, `speed`, `time`, `ticks_per_second`, etc. |
| `V-TBL-1` | E | Empty table or non-number/non-finite entry | Ensure ≥ 1 finite numeric entry per table. |
| `V-NUM-1` | E | `recipe.time ≤ 0`, `machine.speed ≤ 0`, or non-finite | Filter or clamp zero-time dummy records in the converter (log in `report.json`). |
| `V-NUM-2` | E | Port or `setup` `amount ≤ 0` or non-finite | Move 0-consumption items (circuits, molds) from `in` to `setup`; skip 0-amount junk. |
| `V-NUM-3` | E | `chance ∉ (0, 1]` or `catalyst ∉ [0, amount]` | Normalize percentages (`10000` → `1.0`, `10%` → `0.1`); clamp `catalyst ≤ amount`. |
| `V-NUM-4` | E | Recipe has neither `in` nor `out` | Skip empty placeholder recipes in the converter and log them in `report.json`. |
| `V-PAR-1` | E | Unknown param type, missing required default, `null` without `allow_none`, or wrong default type | Fix `type` / `default`, or add `"allow_none": true`. |
| `V-PAR-2` | E | Literal `default` outside `[min, max]`, `min > max`, non-integral `int` default/step, or `slots.count ∉ [0, 64]` | Align literal bounds, defaults, and integer steps. |
| `V-PAR-3` | E | Empty `option_set`, duplicate option key, unknown default key, or `null` slot with `allow_empty: false` | Populate ≥ 1 unique option; fix default key(s) or enable `allow_empty`. |
| `V-PAR-4` | E | Field invalid for parameter type (e.g. `count` on `number`) | Remove fields belonging to other parameter types (§2.4). |
| `V-PAR-5` | W | `ui.widget` unsuitable for parameter type | Pick a compatible widget (`drag`/`slider`/`input` for numbers, `stepper`/`slider`/`input` for ints, `checkbox` for bool, `combo` for enum/resource_choice, `slot_row` for slots). |
| `V-EXPR-1` | E | Expression syntax error | Fix parentheses, quotes, operators (no `?:`, no chained `a < b < c`). |
| `V-EXPR-2` | E | Unknown identifier, function, table, option key, or resource id | Fix typo; drop `.attrs.` (`machine.power`, not `machine.attrs.power`); check table/option/resource ids. |
| `V-EXPR-3` | E | Identifier used outside allowed context | Never use `node.machines`/`node.crafts` in pre-solve or `port.*` outside recipe ports (§3.3). |
| `V-EXPR-4` | E | Type mismatch, bad arity, or wrong result type | Use explicit `if(b, 1, 0)`; match required bool/number/resource return type. |
| `V-EXPR-5` | E | `param.x` unknown, unlisted in `profile.params`, or listed later than dependent param | Add `x` to `profile.params` before any parameter whose static expression reads `param.x`. |
| `V-EXPR-6` | E | Cycle in `let`, `globals`, or `overrides.let` | Break circular dependencies between `let` bindings or globals. |
| `V-PROF-1` | E | `extends` names unknown profile | Fix parent profile id or omit `extends` to inherit `default`. |
| `V-PROF-2` | E | Profile `extends` cycle | Break the inheritance loop. |
| `V-PROF-3` | E | Metric entry has 0 or > 1 forms, or unknown metric id | Declare metric in top-level `"metrics"`; specify exactly one of `per_machine`, `per_craft`, `value`. |
| `V-PROF-4` | E | Bad extra/recipe port spec: duplicate id, bad `dir`, invalid resource spec, duplicate `resource_param`, or non-`resource_choice` param | Give unique extra-port `id`s, valid `dir`, exactly one resource specifier on extra ports, and ensure `resource_param` is a `resource_choice` on every compatible profile. |
| `V-OVR-1` | E | Non-overridable key in `overrides` or unknown `let` override name | Only override allowed keys (§2.7); declare the `let` variable in the profile first. |
| `V-OVR-2` | E | Recipe override or port expr fails to compile, changes `let` type, or adds cycle for some compatible machine profile | Ensure every compatible machine's profile supports the override, or narrow recipe `category`/`machines`. |
| `V-MET-1` | E | Invalid `aggregate`/`display`, or `split_sign: true` when `aggregate != "sum"` | Use valid enum strings and `"aggregate": "sum"` with `split_sign`. |
| `V-ICON-1` | E | Icon not a valid PNG, not square, or > 64×64 (`gamedata_tool` only) | Convert/pad transparently and downscale to square PNG ≤ 64×64; re-run `check-icons --from`. |
| `V-W-1` | W | Referenced icon missing (not emitted in "no icons" mode) | Add the missing `<kind>/<id>.png`, waive intentionally icon-less keys in `waivers.json` (§4.1), or omit icons entirely ("no icons" mode). |
| `V-W-2` | W | Unused resource, machine, parameter, option set, table, or metric | Prune unreferenced entities in the converter so `--werror` passes cleanly (or waive in `waivers.json` if intentionally kept). |
| `V-W-3` | W | Identical duplicate static resource port in one recipe's `in` or `out` | Sum amounts of identical duplicate ports in the converter before writing (or waive in `waivers.json` if intentional). |

### 4.1 Waiving intentional warnings (`waive_warnings`)

To pass `--werror` when specific warnings are intentional (such as text-only `option_sets` without icons), add a dedicated waiver shard file (e.g. `waivers.json`) and list it in `game.json`'s `"include"` array:

```json
{
  "waive_warnings": [
    {
      "rule": "V-W-1",
      "match": "options/*_mults/*",
      "reason": "Project global multiplier presets are text-only"
    }
  ]
}
```

- Only warning rules (`V-W-1`, `V-W-2`, `V-W-3`, `V-PAR-5`) may be waived; attempting to waive errors (`E`/`F`) fails validation (`V-STRUCT-2`).
- `"match"` is a glob (`*` matches any characters, `?` matches one character) against the canonical warning target:
  - `V-W-1`: `resources/<id>`, `machines/<id>`, `options/<set>/<key>`
  - `V-W-2`: `resources/<id>`, `machines/<id>`, `parameters/<id>`, `option_sets/<id>`, `tables/<id>`, `metrics/<id>`
  - `V-W-3`: `recipes/<recipe_id>/<dir>/<resource_id>` (`dir` is `in` or `out`)
  - `V-PAR-5`: `parameters/<id>`, `globals/<id>`
- Waived `V-W-1` icons are **optional**: missing PNGs emit no warning, while any PNG that *does* exist is still validated (`V-ICON-1`), packed by `pack-icons`, and never flagged as unreferenced by `check-icons` or deleted by `prune-icons`. Prefer waiving text-only `option_sets` (such as numeric multiplier presets or abstract labels lacking game artwork) over generating dummy PNGs; the UI omits placeholder icons for waived options.
- Waived warnings are summarized as a single info note (e.g. `I 24 warnings waived (V-W-1)`) in `validate` and `check-icons` output and `--json` `notes[]`, which never fails `--werror`.

---

## 5. Worked Examples

Every example below is valid GameData v4 JSON with hand-checked NodeEval results. Examples follow clean-canvas UX defaults (`"placement": "panel"` and `display: ["tooltip", "totals"]` for routine power/energy), reserving `"body"` for genuinely high-frequency on-canvas controls such as Factorio's `modules` slot row (§5.5).

### 5.1 Satisfactory: Constructor with clock speed, somersloops & power

```json
{
  "units": { "recipe_time": "s", "default_rate": "min" },
  "attributes": {
    "machine": { "power": { "type": "number", "default": 0 },
                 "somersloop_slots": { "type": "number", "default": 0 } }
  },
  "parameters": {
    "clock": { "label": "Clock speed", "type": "number", "default": 100,
               "min": "1", "max": "250", "step": 0.0001, "unit": "%",
               "ui": { "widget": "drag", "format": "%.4g%%", "placement": "panel" } },
    "somersloops": { "label": "Somersloops", "type": "int", "default": 0,
                     "min": "0", "max": "machine.somersloop_slots",
                     "visible_if": "machine.somersloop_slots > 0",
                     "ui": { "widget": "stepper", "placement": "panel" } }
  },
  "metrics": {
    "power":  { "label": "Power", "unit": "MW", "display": ["tooltip", "totals"],
                "aggregate": "sum", "split_sign": true, "format": "%.2f" },
    "shards": { "label": "Power shards", "unit": "", "display": ["tooltip", "totals"],
                "aggregate": "sum", "format": "%.0f" },
    "sloops": { "label": "Somersloops", "unit": "", "display": ["tooltip", "totals"],
                "aggregate": "sum", "format": "%.0f" }
  },
  "profiles": {
    "sf_production": {
      "params": ["clock", "somersloops"],
      "let": {
        "c": "param.clock / 100",
        "amp": "if(machine.somersloop_slots > 0, 1 + param.somersloops / machine.somersloop_slots, 1)",
        "shards_per_machine": "clamp(ceil((param.clock - 100) / 50), 0, 3)"
      },
      "craft_time": "recipe.time / (machine.speed * c)",
      "output_amount": "port.amount * port.chance * amp",
      "metrics": {
        "power":  { "per_machine": "machine.power * amp ^ 2 * c ^ 1.321928" },
        "shards": { "value": "ceil(max(0, node.machines - 1e-6)) * shards_per_machine" },
        "sloops": { "value": "ceil(max(0, node.machines - 1e-6)) * param.somersloops" }
      }
    },
    "sf_generator": {
      "params": ["clock"],
      "let": { "c": "param.clock / 100" },
      "craft_time": "recipe.time / (machine.speed * c)",
      "metrics": { "power": { "per_machine": "-machine.power * c" } }
    }
  },
  "machines": [
    { "id": "constructor", "name": "Constructor", "profile": "sf_production",
      "categories": ["constructor"], "speed": 1, "attrs": { "power": 4, "somersloop_slots": 1 } }
  ],
  "recipes": [
    { "id": "iron-plate", "name": "Iron Plate", "category": "constructor", "time": 6,
      "in": [["iron-ingot", 3]], "out": [["iron-plate", 2]] }
  ]
}
```

**Check (`clock=150, somersloops=1`, target 90 plates/min = 1.5/s):** `c = 1.5`, `amp = 2`, `craft_time = 4 s`, `in:0 = 3`, `out:0 = 4`, `power coeff = 4 · 4 · 1.5^1.321928 = 27.35 MW/machine`. At 1.5 machines (`crafts = 0.375/s`): `power = 41.02 MW`, `shards = ceil(1.5) · 1 = 2`, `sloops = 2`.

### 5.2 GTNH: Macerator with voltage tiers & HV+ byproducts

```json
{
  "units": { "recipe_time": "tick", "ticks_per_second": 20, "default_rate": "s" },
  "attributes": {
    "machine": { "min_tier": { "type": "number", "default": 0 },
                 "max_tier": { "type": "number", "default": 14 } },
    "recipe":  { "eut": { "type": "number", "default": 0 } },
    "port":    { "min_tier": { "type": "number", "default": 0 },
                 "chance_boost": { "type": "number", "default": 0 } }
  },
  "option_sets": {
    "gt_tiers": {
      "attrs": { "t": { "type": "number", "default": 0 }, "v": { "type": "number", "default": 0 } },
      "options": [
        { "key": "ulv", "label": "ULV", "attrs": { "t": 0, "v": 8 } },
        { "key": "lv",  "label": "LV",  "attrs": { "t": 1, "v": 32 } },
        { "key": "mv",  "label": "MV",  "attrs": { "t": 2, "v": 128 } },
        { "key": "hv",  "label": "HV",  "attrs": { "t": 3, "v": 512 } },
        { "key": "ev",  "label": "EV",  "attrs": { "t": 4, "v": 2048 } },
        { "key": "iv",  "label": "IV",  "attrs": { "t": 5, "v": 8192 } },
        { "key": "luv", "label": "LuV", "attrs": { "t": 6, "v": 32768 } },
        { "key": "zpm", "label": "ZPM", "attrs": { "t": 7, "v": 131072 } },
        { "key": "uv",  "label": "UV",  "attrs": { "t": 8, "v": 524288 } }
      ]
    }
  },
  "parameters": {
    "voltage_tier": { "label": "Tier", "type": "enum", "option_set": "gt_tiers",
                      "allowed_if": "option.t >= machine.min_tier && option.t <= machine.max_tier",
                      "ui": { "widget": "combo", "placement": "panel" } }
  },
  "metrics": {
    "eut": { "label": "Energy", "unit": "EU/t", "display": ["tooltip", "totals"],
             "aggregate": "sum", "split_sign": true, "format": "%.0f" }
  },
  "profiles": {
    "gt_single": {
      "params": ["voltage_tier"],
      "let": {
        "t": "param.voltage_tier.t",
        "v": "param.voltage_tier.v",
        "oc_v": "max(0, floor(log(v / max(8, recipe.eut), 4) + 1e-9))",
        "oc": "min(oc_v, max(0, floor(log(recipe.time_raw, 2) + 1e-9)))"
      },
      "requires": [ { "expr": "v >= recipe.eut", "message": "Machine tier too low for this recipe" } ],
      "craft_time": "max(1, floor(recipe.time_raw / 2 ^ oc)) / game.ticks_per_second",
      "port_enabled": "port.min_tier <= t",
      "output_amount": "port.amount * min(1, port.chance + port.chance_boost * oc)",
      "metrics": { "eut": { "per_machine": "recipe.eut * 4 ^ oc" } }
    }
  },
  "machines": [
    { "id": "macerator", "name": "Macerator", "profile": "gt_single",
      "categories": ["gt-macerator"], "attrs": { "min_tier": 1, "max_tier": 8 } }
  ],
  "recipes": [
    { "id": "macerate-crushed-copper", "category": "gt-macerator", "time": 400,
      "attrs": { "eut": 2 },
      "in":  [["crushed_copper", 1]],
      "out": [["impure_dust_copper", 1],
              { "res": "dust_gold", "amount": 1, "chance": 0.1, "attrs": { "min_tier": 3 } }] }
  ]
}
```

**Check:** At **MV** (`t=2, v=128`): `oc=2`, `craft_time = 5.0 s`, `out:1 dust_gold` inactive (`min_tier 3 > 2`), `eut = 32`. At **HV** (`t=3, v=512`): `oc=3`, `craft_time = 2.5 s`, `out:1` active at `0.1`, `eut = 128`.

### 5.3 GTNH: Crop Manager with conditional extra ports (constants illustrative)

Reuses `voltage_tier`, `gt_tiers`, and `eut` from §5.2:

```json
{
  "game": { "id": "gtnh", "name": "GregTech: New Horizons", "data_version": "2.9.0",
            "attrs": { "crop_tick_s": 12.8 } },
  "attributes": {
    "machine": { "min_tier": { "type": "number", "default": 0 },
                 "max_tier": { "type": "number", "default": 14 },
                 "water_per_s": { "type": "number", "default": 0 },
                 "fertilizer_per_s": { "type": "number", "default": 0 } },
    "recipe":  { "crop_tier": { "type": "number", "default": 1 },
                 "growth_points": { "type": "number", "default": 0 } }
  },
  "tables": { "crop_tier_cap": [0, 4, 7, 10, 13, 16], "crop_manager_eut": [0, 32, 128, 512, 2048, 8192] },
  "parameters": {
    "growth":     { "label": "Growth",     "type": "int",  "default": 1, "min": "1", "max": "31", "ui": { "widget": "slider", "group": "Crop stats" } },
    "gain":       { "label": "Gain",       "type": "int",  "default": 1, "min": "1", "max": "31", "ui": { "widget": "slider", "group": "Crop stats" } },
    "resistance": { "label": "Resistance", "type": "int",  "default": 1, "min": "1", "max": "31", "ui": { "widget": "slider", "group": "Crop stats" } },
    "hydrated":   { "label": "Hydrated",   "type": "bool", "default": false, "ui": { "widget": "checkbox", "placement": "panel" } },
    "fertilized": { "label": "Fertilized", "type": "bool", "default": false, "ui": { "widget": "checkbox", "placement": "panel" } }
  },
  "profiles": {
    "gt_crop_manager": {
      "params": ["voltage_tier", "growth", "gain", "resistance", "hydrated", "fertilized"],
      "let": {
        "t": "param.voltage_tier.t",
        "points_per_tick": "3 + param.growth + if(param.hydrated, 2, 0) + if(param.fertilized, 2, 0)",
        "yield": "1.03 ^ param.gain"
      },
      "requires": [ { "expr": "recipe.crop_tier <= lookup('crop_tier_cap', t)",
                      "message": "Crop tier too high for this Crop Manager tier" } ],
      "craft_time": "recipe.growth_points / points_per_tick * game.crop_tick_s",
      "output_amount": "port.amount * port.chance * yield",
      "extra_ports": [
        { "id": "water", "dir": "in", "resource": "water",
          "amount": "machine.water_per_s * craft_time", "enabled": "param.hydrated" },
        { "id": "fertilizer", "dir": "in", "resource": "fertilizer",
          "amount": "machine.fertilizer_per_s * craft_time", "enabled": "param.fertilized" }
      ],
      "metrics": { "eut": { "per_machine": "lookup('crop_manager_eut', t)" } }
    }
  },
  "machines": [
    { "id": "crop_manager", "name": "Crop Manager", "profile": "gt_crop_manager",
      "categories": ["crops"],
      "attrs": { "min_tier": 1, "max_tier": 5, "water_per_s": 0.5, "fertilizer_per_s": 0.01 } }
  ],
  "recipes": [
    { "id": "crop-stickreed", "name": "Stickreed", "category": "crops", "time": 1,
      "attrs": { "crop_tier": 4, "growth_points": 3200 },
      "out": [["sugar_cane", 4], ["sticky_resin", 1, 0.5]] }
  ]
}
```

**Check (LV `t=1`, growth=31, gain=31, hydrated & fertilized):** `points_per_tick = 38`, `craft_time = 1077.89 s`, `yield = 2.50008`, `out:0 = 10.0003`, `out:1 = 1.25004`, `xin:water = 538.95`, `xin:fertilizer = 10.779`, `eut = 32`.

### 5.4 Dyson Sphere Program: Proliferator (`resource_choice` + `extra_ports`)

```json
{
  "attributes": {
    "machine":  { "power": { "type": "number", "default": 0 } },
    "recipe":   { "allows_extra": { "type": "bool", "default": false } },
    "resource": { "extra": { "type": "number", "default": 0 },
                  "speedup": { "type": "number", "default": 0 },
                  "power_mult": { "type": "number", "default": 1 },
                  "charges": { "type": "number", "default": 1 } }
  },
  "resources": [
    { "id": "proliferator-mk1", "name": "Proliferator Mk.I",   "tags": ["proliferator"],
      "attrs": { "extra": 0.125, "speedup": 0.25, "power_mult": 1.3, "charges": 12 } },
    { "id": "proliferator-mk2", "name": "Proliferator Mk.II",  "tags": ["proliferator"],
      "attrs": { "extra": 0.2,   "speedup": 0.5,  "power_mult": 1.7, "charges": 24 } },
    { "id": "proliferator-mk3", "name": "Proliferator Mk.III", "tags": ["proliferator"],
      "attrs": { "extra": 0.25,  "speedup": 1.0,  "power_mult": 2.5, "charges": 60 } }
  ],
  "option_sets": {
    "prolif_modes": { "attrs": {}, "options": [ { "key": "extra", "label": "Extra products" },
                                                { "key": "speed", "label": "Production speedup" } ] }
  },
  "parameters": {
    "prolif":      { "label": "Proliferator", "type": "resource_choice", "tag": "proliferator",
                     "allow_none": true, "default": null, "ui": { "widget": "combo", "placement": "panel" } },
    "prolif_mode": { "label": "Mode", "type": "enum", "option_set": "prolif_modes", "default": "speed",
                     "allowed_if": "option != 'extra' || recipe.allows_extra",
                     "visible_if": "is_set(param.prolif)", "ui": { "widget": "combo", "placement": "panel" } }
  },
  "metrics": {
    "power": { "label": "Power", "unit": "MW", "display": ["tooltip", "totals"], "aggregate": "sum", "format": "%.2f" }
  },
  "profiles": {
    "dsp_crafter": {
      "params": ["prolif", "prolif_mode"],
      "let": {
        "on": "is_set(param.prolif)",
        "extra": "if(on && param.prolif_mode == 'extra', param.prolif.extra, 0)",
        "spd": "if(on && param.prolif_mode == 'speed', param.prolif.speedup, 0)"
      },
      "craft_time": "recipe.time / (machine.speed * (1 + spd))",
      "output_amount": "port.amount * port.chance * (1 + extra)",
      "extra_ports": [
        { "id": "spray", "dir": "in", "resource_param": "prolif",
          "amount": "recipe.input_items_total / param.prolif.charges", "enabled": "on" }
      ],
      "metrics": { "power": { "per_machine": "machine.power * if(on, param.prolif.power_mult, 1)" } }
    }
  },
  "machines": [
    { "id": "assembler-mk3", "name": "Assembling Machine Mk.III", "profile": "dsp_crafter",
      "categories": ["assembler"], "speed": 1.5, "attrs": { "power": 0.78 } }
  ],
  "recipes": [
    { "id": "circuit-board", "name": "Circuit Board", "category": "assembler", "time": 1,
      "attrs": { "allows_extra": true },
      "in": [["iron-ingot", 2], ["copper-ingot", 1]], "out": [["circuit-board", 2]] }
  ]
}
```

**Check (Mk.III):** `extra` mode → `craft_time = 0.6667 s`, `out:0 = 2.5`, `xin:spray = 0.05`, `power = 1.95 MW`. `speed` mode → `craft_time = 0.3333 s`, `out:0 = 2`, `xin:spray = 0.05`, `power = 1.95 MW`.

### 5.5 Factorio 2.0: Assembling Machine 3 with modules & beacons

```json
{
  "game": { "id": "factorio", "name": "Factorio", "data_version": "2.0",
            "attrs": { "beacon_efficiency": 1.5 } },
  "attributes": {
    "machine": { "power": { "type": "number", "default": 0 },
                 "module_slots": { "type": "number", "default": 0 },
                 "base_prod": { "type": "number", "default": 0 } },
    "recipe":  { "allows_productivity": { "type": "bool", "default": false } }
  },
  "option_sets": {
    "modules": {
      "attrs": { "speed": { "type": "number", "default": 0 },
                 "prod": { "type": "number", "default": 0 },
                 "consumption": { "type": "number", "default": 0 } },
      "options": [
        { "key": "speed-module-3",        "label": "Speed module 3",        "attrs": { "speed": 0.5, "consumption": 0.7 } },
        { "key": "productivity-module-3", "label": "Productivity module 3", "attrs": { "speed": -0.15, "prod": 0.1, "consumption": 0.8 } },
        { "key": "efficiency-module-3",   "label": "Efficiency module 3",   "attrs": { "consumption": -0.5 } }
      ]
    }
  },
  "parameters": {
    "modules": { "label": "Modules", "type": "slots", "option_set": "modules",
                 "count": "machine.module_slots",
                 "allowed_if": "option.prod <= 0 || recipe.allows_productivity",
                 "visible_if": "machine.module_slots > 0",
                 "ui": { "widget": "slot_row", "placement": "body" } },
    "beacon_count": { "label": "Beacons", "type": "int", "default": 0, "min": "0", "max": "16",
                      "visible_if": "machine.module_slots > 0",
                      "ui": { "widget": "stepper", "group": "Beacons" } },
    "beacon_modules": { "label": "Beacon modules", "type": "slots", "option_set": "modules",
                        "count": "2", "allowed_if": "option.prod <= 0",
                        "visible_if": "param.beacon_count > 0",
                        "ui": { "widget": "slot_row", "group": "Beacons" } }
  },
  "metrics": {
    "power": { "label": "Power", "unit": "MW", "display": ["tooltip", "totals"], "aggregate": "sum", "format": "%.2f" }
  },
  "profiles": {
    "fa_crafter": {
      "params": ["modules", "beacon_count", "beacon_modules"],
      "let": {
        "n": "param.beacon_count",
        "bmul": "if(n > 0, game.beacon_efficiency * sqrt(n), 0)",
        "s": "sum(param.modules.speed) + bmul * sum(param.beacon_modules.speed)",
        "p": "machine.base_prod + sum(param.modules.prod)",
        "e": "sum(param.modules.consumption) + bmul * sum(param.beacon_modules.consumption)",
        "speed": "max(0.2, 1 + s)",
        "prod": "1 + clamp(p, 0, 3)",
        "energy": "max(0.2, 1 + e)"
      },
      "craft_time": "recipe.time / (machine.speed * speed)",
      "output_amount": "(port.catalyst + (port.amount - port.catalyst) * prod) * port.chance",
      "metrics": { "power": { "per_machine": "machine.power * energy" } }
    }
  },
  "machines": [
    { "id": "assembling-machine-3", "name": "Assembling machine 3", "profile": "fa_crafter",
      "categories": ["crafting"], "speed": 1.25, "attrs": { "power": 0.375, "module_slots": 4 } }
  ],
  "recipes": [
    { "id": "electronic-circuit", "name": "Electronic circuit", "category": "crafting", "time": 0.5,
      "attrs": { "allows_productivity": true },
      "in": [["iron-plate", 1], ["copper-cable", 3]], "out": [["electronic-circuit", 1]] }
  ]
}
```

**Check (4× prod-3, 8 beacons × 2 speed-3, target 30 circuits/s):** `bmul = 4.242641`, `speed = 4.642641`, `prod = 1.4`, `energy = 10.139697`, `craft_time = 0.086158 s`, `out:0 = 1.4`, `power coeff = 3.8024 MW/machine`, `M = 1.8462 machines`, `power = 7.020 MW`.

---

## 6. Mechanic → Schema Pattern Cookbook

| Game Mechanic | Examples | Idiomatic GameData v4 Pattern |
|---|---|---|
| **Speed / clock / RPM / aura scaling** | Satisfactory clock, Create RPM, Mindustry tile efficiency & overdrive, Anno additive productivity, CoI Unity | `number`/`int`/`enum` param in `craft_time: "recipe.time / (machine.speed * speed_mult)"`; power/SU/maintenance as `per_machine` metric (`machine.su_per_rpm * param.rpm`, `machine.power * c ^ 1.321928`). |
| **Variable-power recipes** | Satisfactory Particle Accelerator / Converter | Recipe attrs `min_power`, `max_power`; expose average `power` (`per_machine`: `(recipe.min_power + recipe.max_power) / 2 * ...`) and worst-case `peak_power` (`value`: `ceil(max(0, node.machines - 1e-6)) * recipe.max_power * ...`) metrics. |
| **Fixed-draw sinks & completion timers** | Satisfactory AWESOME Sink, Space Elevator phases | For sinks with a throughput/belt-tier `enum` in `craft_time`, compute active power via `value`: `ceil(max(0, node.machines - 1e-6)) * pwr` so changing belt tier does not alter power draw for identical input flow. For deliverable targets, compute post-solve completion time via `if(node.crafts > 0, 1 / (node.crafts * 3600), 0)`. |
| **Power-only / 0-port buildings (`V-NUM-4`)** | Satisfactory Geothermal, Solar / Wind generators | Because `V-NUM-4` requires ≥ 1 port, either defer in `PACK_NOTES.md` or emit a synthetic marker output (`out: [["<marker>", 1]]`, `time: 1`) so a standalone node reports negative power without requiring a second dummy source node. |
| **Module slots & beacons** | Factorio modules/beacons, PyAL living modules | `slots` param with `count: "machine.module_slots"`, `allowed_if` for productivity/creature gating, aggregate with `sum(param.modules.<attr>)` and `count(param.modules)`. |
| **Voltage tiers, OC & sub-tick parallels** | GTNH singleblocks, multi-amp hatches, Processing Array, mega multiblocks | Single machine per family + `voltage_tier` `enum` filtered by `allowed_if`; `requires` gates `v >= recipe.eut`; `let` computes `oc` and parallel count `P`, scaling `input_amount` & `output_amount` by `P` when sub-tick/parallel. |
| **Coil / burner heat gating & bonuses** | GTNH EBF (900 K discount / 1800 K perfect OC), Erekir `HeatCrafter`, Create Blaze Burner | `coil` `enum` with `heat` attr (or `supplied_heat` `number` param); `requires` checks `h >= recipe.min_heat`; `let` computes `floor((h - recipe.min_heat) / 900)` and `/ 1800`; Create adds conditional fuel `extra_ports` when `recipe.min_heat >= 2`. |
| **Tier-gated & conditional ports** | GTNH HV+ byproducts, Mindustry liquid boosters, ONI wild vs domesticated plants, Anno tractors/silos, AtS rain engines | Recipe ports: `port_enabled: "port.min_tier <= t"` or `input_enabled: "param.domesticated"`. Machine-added ports: `extra_ports` with `"enabled": "param.boosted"`. |
| **Probabilistic & boosted outputs** | GTNH chance boost, AtS crit chance, Create sequenced assembly, GTNH bees | Store base probability in port `chance`; compute `output_amount` via `min(1, port.chance + boost)` or `port.amount * port.chance * (1 + crit)`. |
| **Catalysts & setup items** | Factorio Kovarex (`catalyst`), GTNH circuits/molds/lenses (`setup`) | Non-amplified portion: set `"catalyst"` on long-form port and `(port.catalyst + (port.amount - port.catalyst) * prod)`. Non-consumed setup items: put in `"setup": [["circuit-1", 1]]` (never `amount: 0` in `in`). |
| **Burner / fluid fuel + ash / spent container** | Factorio furnaces, Pyanodons ash, Nuclear spent cells | `fuel` `resource_choice` param (`tag: "fuel"`, attrs `fuel_mj`, `burnt_result`); `extra_ports` input with `resource_param: "fuel"`, `amount: "power_mw * craft_time / (machine.eff * param.fuel.fuel_mj)"`, plus output with `resource_expr: "param.fuel.burnt_result"` (returns `""` when no ash → port auto-inactive). |
| **Ingredient tag slots & input swaps** | Against the Storm categories, GTNH OreDict, Anno Trade Union replacements | Long-form recipe port with `"resource_param": "slot_0"`, optional `"res"` fallback, and port `attrs: { "slot_tag": "meat" }`; bind `allowed_if: "has_tag(option, port.slot_tag)"`. |
| **Global tech upgrades & weather cycles** | Factorio mining prod, Pyanodons T.U.R.D., Timberborn workdays & seasons | Define under `"globals"`. Read as `global.<id>` in profiles or recipe `overrides.requires`. For seasonal extractors, scale `craft_time` by `D_total / D_wet` and add a post-solve buffer storage `value` metric. |
| **Non-linear mechanics (approximations)** | ONI thermal mixing, Factorio Gleba spoilage, Stationeers P/T windows | Parameterize `input_temp_c` or `freshness_pct` as a node param (pre-solve) and report net `kDTU/s`, storage buffer, or `[P_min, P_max]` / `[T_min, T_max]` as post-solve metrics. Discrete fluid temperatures → distinct resource IDs (e.g. `steam-500c`). |

---

## 7. Data Extraction Sources

Converters live outside the C++ app repo (in `factory-planner-packs` or your own workspace) and transform authoritative raw dumps into GameData v4 JSON:

| Game / Modpack | Primary Source | Location / Extraction | Native Format & Scale |
|---|---|---|---|
| **Satisfactory** | Official Community Docs (`Docs.json`) | `<Install>/CommunityResources/Docs/Docs.json` (UTF-16LE or UTF-8) | JSON (~400 recipes, ~60 machines; Unreal tuple strings) |
| **Factorio 2.0 / Space Age** | Built-in CLI `--dump-data` | `factorio --dump-data` → `script-output/data-raw-dump.json` (+ `--dump-icon-sprites`) | Clean JSON (~500 recipes) |
| **Pyanodons / Seablock** | Factorio `--dump-data` with mods enabled | Enable mod list, run `factorio --dump-data` | JSON (~6k–12k recipes; PyAL modules, fluid temps) |
| **Dyson Sphere Program** | Unity `ProtoSet` / BepInEx dump | `DSPGAME_Data/resources.assets` (`ItemProtoSet`, `RecipeProtoSet`) | JSON (~180 recipes, `TimeSpend` in 1/60 s ticks) |
| **Mindustry (Serpulo & Erekir)** | Open-source Java / dump mod | `Anuken/Mindustry` `content/{Blocks,Items,Liquids}.java` or headless mod | Java DSL / JSON (~100 blocks) |
| **GregTech: New Horizons** | NEI dump / `RecEx` exporter mod | Run GTNH with `RecEx` / NEI recipe export | JSON/CSV (**50k+ recipes**; `mEUt`, `mDuration` ticks, `mChances` / 10000) |
| **Captain of Industry** | `Mafi.Core.dll` reflection dump | Harmony/BepInEx dumper over `Mafi.Core.Prototypes` | JSON (~500 recipes, ~150 machines) |
| **Oxygen Not Included** | `Database.Db` + `StreamingAssets/elements/` | Assembly/mod export + element YAMLs (`SHC`, temps) | JSON + YAML (~250 recipes, ~100 elements) |
| **Anno 1800** | `assets.xml` from `maindata/*.rda` | Unpack via `RDAExplorer` / `FileDBReader` | XML (~150 chains, ~600 Trade Union items) |
| **Against the Storm** | Unity ScriptableObjects | `resources.assets` via AssetStudio / BepInEx dumper | JSON (~180 recipes, ~45 buildings) |
| **Create (Minecraft)** | Mod `.jar` datapack JSONs | Inside `create-*.jar` at `data/create/recipes/<type>/*.json` | Standard Minecraft recipe JSON (~600 recipes) |
| **Timberborn** | `StreamingAssets` blueprints | `Timberborn_Data/StreamingAssets/ModdableTimberborn/Blueprints/` | JSON (~80 recipes) |
| **Stationeers** | `StreamingAssets` XML | `rocketstation_Data/StreamingAssets/Data/*.xml` | XML (~400 recipes/alloys) |

---

## 8. Converter Rules & Slugification

### 8.1 Converter rules

1. **Deterministic output:** Same raw input → byte-identical JSON (sorted maps and entity lists, fixed float formatting, `allow_nan=False`, no timestamps or absolute paths).
2. **Scoped writes:** Clean and rewrite **only** the JSON files under the targeted `packs/<game>/<pkg>/` (plus the converter's own `report.json` and `.tmp_icons/`). Never touch `raw/`, sibling package versions, or `icons.pak`.
3. **Stable identity & port order:** Derive `id`s from stable raw/internal keys, never display names. For recipes without a raw id (e.g. GTNH), use `<category>-<short_hash_of_canonical_inputs_and_outputs>`. **Preserve raw `in`/`out` port order** (slot indices are identity in `.fpp` files). Resolve ID collisions deterministically and log them in `report.json`.
4. **No silent drops:** Record every skipped raw record in `report.json` with a reason, and reconcile counts against `gamedata_tool stats`.
5. **Pure data in recipes:** Formulas live exclusively in `template/game.json` profiles. Emit compact short-form tuple ports `[res, amount]` / `[res, amount, chance]` and shard large recipe sets by category or recipe map.
6. **Warning-free (`--werror`):** Prune unreferenced resources/machines/parameters/tables/metrics (`V-W-2`) and merge identical duplicate ports (`V-W-3`).
7. **Icons are optional:** If no icons are exported, create no `icons/` directory ("no icons" mode). If icons exist, pad transparently to square and area-downscale to ≤ 64×64 PNGs into `icons/` or `.tmp_icons/`, waive intentionally text-only option sets (`V-W-1`) in `waivers.json` instead of generating dummy PNGs, verify loose PNGs with `check-icons --from` (no `--werror`), pack with `pack-icons --from ... --verify`, then run `check-icons --from ... --werror` and `validate --werror`.
8. **Dependency discipline:** Prefer Python 3 standard library (`json`, `csv`, `xml.etree`, `zipfile`, `gzip`, `sqlite3`, `struct`, `zlib`) or already-installed system tools. Third-party packages are allowed only in a project-local `.venv/` after explicit user confirmation. Never install packages globally (`--user`, `--break-system-packages`, `sudo`).

### 8.2 Exact `slugify()` specification

Converters should match `core_gamedata`'s `slugify()` (`src/common/StringUtils.h`) so entity IDs and cross-pack icon fallback matching stay identical:
1. Trim leading/trailing whitespace and convert ASCII to lowercase.
2. Replace `+` with `plus`.
3. Replace `:`, `.`, and `/` with `-`.
4. Remove every character not matching `[a-z0-9\s:._-]`.
5. Collapse consecutive whitespace/hyphens (`[\s-]+`) into a single `-`.
6. Trim leading and trailing `-`.

---

## 9. `gamedata_tool` Command Reference

`gamedata_tool` links the exact same headless `core_gamedata` library as Factory Planner, so validation and evaluation never drift. `<pkg>` accepts either a package directory or its `game.json`.

**Exit codes:** `0` OK (warnings allowed unless `--werror`) · `1` validation / eval / check failure (or warnings with `--werror`) · `2` fatal error (`F-*`) or I/O error · `64` CLI usage error.
With `--json`, stdout carries **one** JSON document and diagnostics go to stderr.

### 9.1 Commands & flags

| Command | Usage & Flags |
|---|---|
| `--version` / `--help` | `gamedata_tool --version` (`-V`), `gamedata_tool --help` (`-h`), `gamedata_tool <cmd> --help` |
| `init` | `gamedata_tool init <dir> [--id SLUG] [--name NAME] [--data-version VER] [--time s\|tick] [--tps N] [--rate tick\|s\|min\|h] [--icons] [--force] [--json]`<br>Creates a minimal valid v4 scaffold + `.gitignore` (no icons by default; `--icons` writes 16×16 placeholder PNGs). |
| `validate` | `gamedata_tool validate <pkg> [--json] [--werror] [--no-icons] [--ignore-errors] [--max-groups N] [--examples N] [--all] [--timing]`<br>Runs full load, expression compile, spec §9.2 rules, icon checks (T8 order), and root-cause grouping. `--json` emits `report_version: 1` with `counts`, `groups`, `cascades`, `notes`, `timing_ms` (`--all` appends flat `diagnostics[]`). |
| `eval` | `gamedata_tool eval <pkg> --recipe ID [--machine ID \| --all-machines] [--param k=json ...] [--global k=json ...] [--sweep PARAM] [--machines N] [--rate tick\|s\|min\|h] [--json]`<br>or `gamedata_tool eval <pkg> --cases <file\|dir> [--json]`<br>Note: `--param`/`--global` values are JSON literals (`clock=150`, `tier='"hv"'`, `mods='["speed-3",null]'`, `prolif=null`). `--sweep` sweeps enum options or int ranges (max 32 values). |
| `stats` | `gamedata_tool stats <pkg> [--repeat N] [--timing] [--ignore-errors] [--json]`<br>Prints entity counts, JSON byte size, per-category/machine/profile/file breakdowns, icon coverage, and median load timing (parse/compile/total). |
| `check-icons` | `gamedata_tool check-icons <pkg> [--from DIR] [--werror] [--json]`<br>Checks missing (`V-W-1`, honoring `waive_warnings`), unreferenced, malformed/oversize (`V-ICON-1`), case mismatches, and stale `icons.pak`. Exits 1 on `V-ICON-1` (and with `--werror`, on missing/unreferenced/case mismatch/stale pak — run without `--werror` before `pack-icons`, and with `--werror` after `pack-icons` when updating an existing pak). |
| `prune-icons` | `gamedata_tool prune-icons <pkg> [--from DIR] [--apply] [--json]`<br>Lists unreferenced loose PNGs; deletes them only with `--apply`. Never touches `icons.pak`. |
| `pack-icons` | `gamedata_tool pack-icons <pkg> [--from DIR] [-o FILE] [--verify] [--json]`<br>or `gamedata_tool pack-icons --list <file> [--json]`<br>Builds deterministic `<package_root>/icons.pak` containing only referenced icons (plus `name_slug` for cross-pack fallback). Fails on `V-ICON-1`. `--verify` reads back and compares every PNG byte-for-byte. |
| `guide` | `gamedata_tool guide [prompt\|reference] [--json]`<br>Prints the embedded pack-authoring prompt (`prompt`) or this author reference (`reference`). |

### 9.2 Regression `--cases` JSON format

A `--cases` file (or every `*.json` under a `--cases` directory) is a JSON array of case objects:

```json
[
  {
    "name": "constructor 150% + 1 sloop",
    "recipe": "iron-plate",
    "machine": "constructor",
    "params": { "clock": 150, "somersloops": 1 },
    "globals": {},
    "machines": 1.5,
    "tol": 1e-3,
    "expect": {
      "status": "OK",
      "craft_time_s": 4,
      "ports": { "in:0": 3, "out:0": 4 },
      "metric_coeffs": { "power": 27.35 },
      "metrics": { "shards": 2, "sloops": 2 },
      "resolved_params": { "clock": 150 }
    }
  }
]
```

- All fields inside `expect` are optional; unspecified fields are not checked. Numeric comparisons pass when `|actual - expected| <= tol * max(1, |actual|, |expected|)` (`tol` default `1e-6`).
- `expect.ports.<slot>` accepts a number (`amount_per_craft`), a boolean (`active`), or an object `{ "active", "resource", "amount" }`.
- `expect.metric_coeffs.<id>` accepts a number (`coeff`) or `{ "enabled", "coeff" }`.
- `expect.metrics.<id>` (requires `"machines"` on the case) accepts a number (`value`) or `{ "enabled", "status", "value" }`. Values are always canonical (for `is_rate: true` / `per_craft` metrics, canonical **per-second** rates regardless of `units.default_rate`).

### 9.3 Scale budgets (50k-recipe target)

Measure large packs (such as GTNH) with `gamedata_tool stats <pkg> --timing --repeat 5`:
- Package load (Release): **< 2 s** for 50k recipes.
- `icons.pak` index read: **< 50 ms**; `pack-icons`: **< 10 s**.
- Keep recipes compact (short tuple ports, formulas in profiles only, sharded by category/map) to stay well within budget.
