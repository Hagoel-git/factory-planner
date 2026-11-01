# Factory Solver

`FactorySolver` (`src/core/graph/FactorySolver.h`, `src/core/graph/FactorySolver.cpp`) computes the steady-state flow rates and machine counts of a factory graph by formulating each weakly connected component as a Linear Program (LP) and solving it with Google OR-Tools' **GLOP** simplex solver.

This document describes the **v3 solver formulation** implemented by `FactorySolver`: connected-component partitioning, port constraint modes (`AUTO`, `LIMIT`, `TARGET`, `PUSH`), the sink-dedicated and target-serving subgraphs, no-overflow rules, and the 4-stage lexicographic optimization sequence.

---

## 1. Graph Elements & Connected Component Partitioning

Before building any LP variables or resolving `AUTO` constraint modes, `FactorySolver::solve` partitions the `FactoryGraph` into **weakly connected components** $\mathcal{K}$ over the undirected projection of nodes and connections.

Each component $K = (\mathcal{N}_K, \mathcal{P}_K, \mathcal{W}_K) \in \mathcal{K}$ is analyzed and solved in its own independent `MPSolver` pass:
- **Nodes ($\mathcal{N}_K$):** Each node $n \in \mathcal{N}_K$ runs a selected `Recipe` (`selected_recipe_key`) on a `Machine` (`machine_key`) at a `clock_speed` ($\%$) and `production_multiplier` ($\%$). Its solved `machine_count` is written back after solving.
- **Ports ($\mathcal{P}_K = \mathcal{P}^{\text{in}}_K \cup \mathcal{P}^{\text{out}}_K$):** Input and output pins on nodes in $\mathcal{N}_K$. Ports with `resource_key == "nothing"` represent environmental extraction (miners, water extractors) or disposal (sinks).
  - Each port $p$ has a `user_constraint` ($-1$ when unconstrained, $\ge 0$ when active), a `constraint_mode` (`ConstraintMode::{AUTO, LIMIT, TARGET, PUSH}`), a solved `rate` $R_p$, and (for wired output ports) an unconsumed `excess_rate` $E_p$.
- **Connections ($\mathcal{W}_K$):** Directed edges $c = (p_{\text{out}} \to p_{\text{in}})$ between ports of the same resource in $K$, each carrying a solved `rate` $F_c$.
- **Component Failure Isolation:** If one component $K \in \mathcal{K}$ fails to solve (`INFEASIBLE`, `UNBOUNDED`, or `ERROR`), only the nodes, ports, and connections belonging to $K$ are zeroed; all other valid components retain their solved rates and machine counts.

### Structural Subsets within Component $K$

For each component $K$, let $W^+(p)$ and $W^-(p)$ denote the outgoing and incoming connections of port $p$, and $\nu(p) \in \mathcal{N}_K$ the node owning $p$:

- **Raw Sources ($\mathcal{B}^{\text{raw}}_K$):** Output ports of nodes whose input ports are empty or all have `resource_key == "nothing"`.
- **Unwired Imports ($\mathcal{B}^{\text{imp}}_K$):** Input ports with no incoming connections ($W^-(p) = \emptyset$) and `resource_key != "nothing"`.
- **Exports ($\mathcal{X}_K$):** Output ports with no outgoing connections ($W^+(p) = \emptyset$) and `resource_key != "nothing"`.
- **Sinks ($\mathcal{S}_K$):** Nodes $n \in \mathcal{N}_K$ that have no output ports, or whose output ports all have `resource_key == "nothing"`.
- **Sink-Dedicated Subgraph ($\mathcal{S}^+_K$):** The set of nodes $n \in \mathcal{N}_K$ that have at least one directed path to $\mathcal{S}_K$, and for which **every** directed path starting at $n$ terminates at a sink in $\mathcal{S}_K$ without ever visiting an export port in $\mathcal{X}_K$, a pre-resolved active `TARGET` port ($g > 0$), or an explicit `PUSH` port. Computed once before Step 3 of `AUTO` resolution via backward reachability:
  1. $R_{\text{sink}} = \text{BackwardBFS}(\mathcal{S}_K)$
  2. $B = \{\nu(p) \mid p \in \mathcal{X}_K \cup G^{\text{pre}}_K \cup P^{\text{push,expl}}_K\} \cup (\mathcal{N}_K \setminus R_{\text{sink}})$
  3. $\mathcal{S}^+_K = R_{\text{sink}} \setminus \text{BackwardBFS}(B)$

---

## 2. Port Constraint Modes & `AUTO` Resolution

Every port $p$ carries a `ConstraintMode`:

| Mode | Semantics |
| :--- | :--- |
| **`AUTO`** | Default. Resolved per component $K$ into `LIMIT`, `TARGET`, or `PUSH` (see below). |
| **`LIMIT`** | Hard capacity ceiling: $R_p \le c$. Never actively pulls or pushes flow. |
| **`TARGET`** | Demand pull goal: enforces $R_p \le g_p$ and maximizes $R_p \to g_p$ in Stage 1 by pulling from upstream. |
| **`PUSH`** | Supply push goal:<li>**Rated (`user_constraint >= 0`):** enforces $R_p \le g_p$, maximizes $R_p \to g_p$ in Stage 1, and forces $E_q = 0$ on all downstream wired output ports $q \in \mathrm{Cl}(p)$.</li><li>**Unrated (`user_constraint < 0`):** imposes no numeric rate ceiling or Stage 1 goal, but forces $E_q = 0$ on downstream non-target-serving wired output ports $q \in \mathrm{Cl}_{\neg\mathcal{T}}(p)$.</li> |

### 3-Step `AUTO` Resolution within Component $K$

For any port $p \in \mathcal{P}_K$ with `constraint_mode == ConstraintMode::AUTO`:

1. **Step 1 — Non-Boundary Ports:**
   - Wired input ports ($p \in \mathcal{P}^{\text{in}}_K \setminus \mathcal{B}^{\text{imp}}_K$) resolve to **`LIMIT`**.
   - Non-raw output ports ($p \in \mathcal{P}^{\text{out}}_K \setminus \mathcal{B}^{\text{raw}}_K$) resolve to **`TARGET`**.
2. **Step 2 — Sink-Dedicated Subgraph:** Compute $\mathcal{S}^+_K$ using the pre-resolved `TARGET` ($g > 0$) and explicit `PUSH` ports.
3. **Step 3 — Boundary Supply Ports ($p \in \mathcal{B}^{\text{raw}}_K \cup \mathcal{B}^{\text{imp}}_K$):**
   - **Demand-Driven Component** ($K$ has $\ge 1$ effective `TARGET` port with `user_constraint > 0`; a `TARGET` with `user_constraint == 0` is only a zero cap and does not switch $K$ into demand mode): every `AUTO` boundary port resolves to **`LIMIT`**.
   - **Supply-Driven Component** ($K$ has $0$ effective `TARGET`s):
     - *Auxiliary sink-supplier demotion:* If $\mathcal{N}_K \setminus \mathcal{S}^+_K \neq \emptyset$ and $\nu(p) \in \mathcal{S}^+_K$, $p$ is demoted to **`LIMIT`** if and only if every sink $s \in \mathcal{S}_K$ reachable from $p$ is reached exclusively through sink input port(s) that have no directed path from $\mathcal{N}_K \setminus \mathcal{S}^+_K$, while $s$ has at least one other input port that *does* have a directed path from $\mathcal{N}_K \setminus \mathcal{S}^+_K$.
     - All other `AUTO` boundary supply ports resolve to **`PUSH`**.

---

## 3. Variables & Hard Constraints

### LP Variables (per Component $K$)

All variables are non-negative ($\ge 0$):

| Variable | Created for | Meaning |
| :--- | :--- | :--- |
| $R_p \ge 0$ | Every port $p \in \mathcal{P}_K$ | Flow rate through port $p$ |
| $F_c \ge 0$ | Every connection $c \in \mathcal{W}_K$ | Flow rate along wire $c$ |
| $E_p \ge 0$ | Every wired output port $p \in \mathcal{P}^{\text{out}}_K$ ($W^+(p) \neq \emptyset$) | Unconsumed production on port $p$ |
| $S \in [0, 1]$ | Stage 1 (when $G_K \neq \emptyset$) | Common water-filling saturation fraction |
| $hi^{\text{ns}}_p, lo^{\text{ns}}_p \ge 0$ | Every output port $p$ with $\ge 2$ outgoing wires to $(\mathcal{N}_K \setminus \mathcal{S}^+_K) \setminus \mathcal{T}_K$ | Max and min branch flow across non-sink, non-target-serving outgoing wires of $p$ |
| $hi_p, lo_p \ge 0$ | Every port $p$ with $\ge 2$ outgoing or $\ge 2$ incoming wires | Max and min branch flow across all wires of $p$ |

### Hard Constraints

1. **Recipe Stoichiometry:** For each node $n \in \mathcal{N}_K$ with a valid recipe, positive `clock_speed`, and positive `production_multiplier` $P = \text{production\_multiplier} / 100$:
   - If the node's recipe is missing, or if `clock_speed <= 0` or `production_multiplier <= 0`, all ports of $n$ are clamped to $R_p = 0$.
   - Otherwise, all input and output port rates are locked in exact linear proportion to the first positive output port $o_0$ (or first positive input port $i_0$ for sink recipes):
     $$a_{o_0} \cdot P \cdot R_{i_k} - a_{i_k} \cdot R_{o_0} = 0, \qquad a_{o_k} \cdot R_{o_0} - a_{o_0} \cdot R_{o_k} = 0$$
2. **User Rate Ceilings:** Every port $p \in \mathcal{P}_K$ with `user_constraint >= 0` (regardless of whether its resolved mode is `LIMIT`, `TARGET`, or `PUSH`) enforces:
   $$R_p \le p.\text{user\_constraint}$$
3. **Wire Flow Conservation:**
   - Wired output port ($W^+(p) \neq \emptyset$): $R_p - E_p - \sum_{c \in W^+(p)} F_c = 0$
   - Wired input port ($W^-(p) \neq \emptyset$): $\sum_{c \in W^-(p)} F_c - R_p = 0$
4. **No-Overflow Rules ($E_p = 0$):**
   Let:
   - $G_K = \{p \in \mathcal{P}_K \mid p.\text{user\_constraint} > 0 \land \text{mode}(p) \in \{\text{TARGET}, \text{PUSH}\}\}$ be the **active goals** of $K$.
   - $\mathcal{T}_K = \text{BackwardBFS}(\{\nu(p) \mid p \in G_K \land \text{mode}(p) = \text{TARGET}\}) \setminus \mathcal{S}^+_K$ be the **target-serving subgraph** of $K$.
   - $Z_K = \{p \in \mathcal{P}^{\text{out}}_K \mid W^+(p) \neq \emptyset \land p \in \mathrm{Cl}(P^{\text{push,rated}}_K) \cup \mathrm{Cl}_{\neg\mathcal{T}}(P^{\text{push,unrated}}_K)\}$ be the **push-driven no-overflow port set**, where $\mathrm{Cl}$ is forward reachability over wires and node input$\to$output links, and $\mathrm{Cl}_{\neg\mathcal{T}}$ restricts internal node traversal to $n \in \mathcal{N}_K \setminus \mathcal{T}_K$.

   Then $E_p$ is fixed to zero (`excess->SetUB(0.0)`) on every wired output port $p$ satisfying:
   - **Rule 1:** $p \in Z_K$ (push-driven paths cannot dump flow into unconsumed wire excess), OR
   - **Rule 2:** $\nu(p) \notin \mathcal{T}_K$ (nodes outside the target-serving subgraph cannot generate unconsumed wire excess).

   Consequently, $E_p > 0$ is permitted **only** on target-serving nodes ($\nu(p) \in \mathcal{T}_K$) not subject to a push constraint ($p \notin Z_K$), allowing co-product/byproduct shedding when required to satisfy a `TARGET`.

---

## 4. The 4-Stage Lexicographic Solve

Each component $K$ is solved through a fixed sequence of LP stages. After each intermediate stage achieves optimum $z^\star$, `lockStage` adds a linear constraint locking that stage's objective within $\tau(z^\star) = 10^{-9} \cdot \max(1, |z^\star|)$.

### Stage 1: Max-Min Water-Filling on Active Goals ($G_K$)

If $G_K \neq \emptyset$, introduce $S \in [0, 1]$ with $R_p - g_p \cdot S \ge 0$ for all $p \in G_K$, and iteratively maximize $S$:
- If $S^\star \ge 1 - 10^{-9}$, all remaining goals in $G_K$ are locked at $R_p \ge g_p$ and the loop terminates.
- Otherwise, $S$ is temporarily fixed to $S^\star$ and a **Hybrid Filter** identifies bottlenecked goals:
  1. **Sum solve:** Maximize $\sum_{p \in G_{\text{active}}} R_p$. Any goal with $R_p > g_p S^\star + 10^{-6}$ has headroom and stays active.
  2. **Suspect verification:** Each goal that did not grow in the sum solve is maximized individually. If it still cannot exceed $g_p S^\star + 10^{-6}$, it is a genuine bottleneck: its lower bound is locked at $g_p S^\star - \tau(g_p S^\star)$ and its $S$-constraint is decoupled.
- When all goals are locked, all $S$-constraints are decoupled so $S$ is inert in Stages 2–4.

### Stage 2: Upstream Conservation & Overflow Routing

- **Stage 2a — Minimal Target-Serving Machine Activity:**
  $$\min \sum_{n \in \mathcal{T}_K} w_n \cdot R_{\text{ref}(n)}$$
  where $\text{ref}(n)$ is the first non-`"nothing"` input port (or first positive output port for raw sources), and $w_n$ is the machine-count weight per unit reference flow (using $B = 1.0$ if no valid machine is assigned):
  $$w_n = \frac{\text{recipe.time\_seconds}}{\text{ref\_amount} \cdot B \cdot U \cdot C}$$
  Only the total weighted sum is locked via `lockStage`, preserving freedom for Stage 4 to balance parallel equal-efficiency branches.
- **Stage 2b — Maximal Wired Consumption (Minimal Unconsumed Excess):**
  $$\min \sum_{p \in \mathcal{P}^{\text{out}}_K,\, W^+(p) \neq \emptyset} E_p$$
  Forces all produced flow on wired output ports into connected downstream wires (including sinks) up to their capacity before leaving any excess $E_p > 0$.
- **Stage 2c — Non-Sink Routing Priority & Fair Splitting:**
  - **Stage 2c.1 (Mixed-Splitter Sink-Branch Minimization):** For every mixed splitter port $p$ (having both sink-dedicated outgoing wires $c \in W^+_{\text{sink}}(p)$ to $\mathcal{S}^+_K$ and non-sink outgoing wires $c \in W^+_{\text{ns}}(p)$ to $\mathcal{N}_K \setminus \mathcal{S}^+_K$), minimize total flow into the sink-dedicated branches:
    $$\min \sum_{c \in W^{\text{mix-sink}}_K} F_c$$
  - **Stage 2c.2 (Non-Sink Splitter Fairness):** For every output port $p$ with $\ge 2$ outgoing wires $W^+_{\text{ns}\setminus\mathcal{T}}(p)$ to non-sink nodes outside $\mathcal{T}_K$ ($(\mathcal{N}_K \setminus \mathcal{S}^+_K) \setminus \mathcal{T}_K$), minimize the spread across those branches:
    $$\min \sum_{p:\, |W^+_{\text{ns}\setminus\mathcal{T}}(p)| \ge 2} \left(hi^{\text{ns}}_p - lo^{\text{ns}}_p\right)$$

### Stage 3: Residual & Sink Subgraph Minimization

- **Stage 3a — Minimal Remaining Non-Sink Machine Activity:**
  $$\min \sum_{n \in \mathcal{N}_K \setminus (\mathcal{T}_K \cup \mathcal{S}^+_K)} w_n \cdot R_{\text{ref}(n)}$$
  Drives unconstrained nodes, unused branches, and unconstrained amplifying cycles to zero while respecting all locked upstream pushes and targets.
- **Stage 3b — Minimal Sink-Dedicated Port Flow:**
  $$\min \sum_{p \in \mathcal{P}_K:\, \nu(p) \in \mathcal{S}^+_K} R_p$$
  Ensures sink-dedicated subgraphs run only as much as forced by Stage 1 goals or Stage 2b overflow absorption.

### Stage 4: Terminal Fair Branch Splitting

$$\min \sum_{p:\, |W^+(p)| \ge 2 \text{ or } |W^-(p)| \ge 2} \left(hi_p - lo_p\right)$$
Balances any remaining degrees of freedom across all splitters and mergers (including equal-efficiency upstream suppliers and parallel sink lines).

---

## 5. Post-Processing & Diagnostics

After solving each component $K$:
- Any solved value with $|v| < 10^{-9}$ is snapped to `0.0`.
- Port rates $R_p$, wire rates $F_c$, and unconsumed excess rates $E_p$ are written back to `FactoryGraph`.
- Node `machine_count` is computed from the solved reference port rate, recipe craft time, machine `base_crafting_speed`, time unit (`seconds` = 1, `minutes` = 60, `hours` = 3600), `clock_speed`, and `production_multiplier`.
- Diagnostic getters on `FactorySolver` expose the per-port resolved `ConstraintMode` (`getResolvedConstraintMode`) and per-goal saturation ratio $R_p / g_p \in [0, 1]$ (`getGoalSaturation`, `isGoalBottlenecked`).
