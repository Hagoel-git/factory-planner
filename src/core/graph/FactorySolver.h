#ifndef FACTORYSOLVER_H
#define FACTORYSOLVER_H

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Port.h"
#include "ortools/linear_solver/linear_solver.h"

class FactoryGraph;
struct Node;
struct Recipe;

/**
 * Models the factory graph as a Linear Program and solves it via Google OR-Tools (GLOP).
 *
 * Each weakly connected component K of the graph is analyzed and solved independently:
 *  - Port constraint modes (AUTO -> LIMIT, TARGET, or PUSH) and the sink-dedicated subgraph S+_K
 *    are resolved locally per component.
 *  - Active goals G_K (ports with user_constraint > 0 and resolved mode TARGET or PUSH) are
 *    satisfied via max-min water-filling.
 *  - Solved lexicographically in 4 stages per component:
 *      Stage 1:  Iterative max-min water-filling on active goals G_K.
 *      Stage 2a: Minimal machine activity on target-serving subgraph T_K.
 *      Stage 2b: Maximal wired consumption (minimal unconsumed excess E_p).
 *      Stage 2c: Mixed-splitter sink-branch minimization (2c.1) and non-sink splitter fairness (2c.2).
 *      Stage 3a: Minimal machine activity on remaining non-sink nodes N_K \ (T_K U S+_K).
 *      Stage 3b: Minimal port flow on sink-dedicated subgraph S+_K.
 *      Stage 4:  Terminal fair branch splitting across all splitters and mergers.
 *
 * See docs/solver.md for the full formulation.
 */
class FactorySolver {
public:
    enum class SolverResultStatus {
        SUCCESS,
        INFEASIBLE,
        UNBOUNDED,
        ERROR_
    };

    enum class ConstraintKind {
        LIMIT,  // Hard capacity ceiling: rate <= value
        TARGET, // Demand pull goal:      rate -> value (pulls from upstream)
        PUSH    // Supply push goal:      rate -> value (pushes into downstream)
    };

    static std::string toString(SolverResultStatus status) {
        switch (status) {
            case SolverResultStatus::SUCCESS: return "SUCCESS";
            case SolverResultStatus::INFEASIBLE: return "INFEASIBLE";
            case SolverResultStatus::UNBOUNDED: return "UNBOUNDED";
            case SolverResultStatus::ERROR_: return "ERROR";
            default: return "UNKNOWN";
        }
    }

    struct SolverResult {
        SolverResultStatus status;
        double total_solve_time_ms;
        double setup_time_ms;
        double solve_time_ms;
        double update_factory_time_ms;
    };

    explicit FactorySolver(const std::string& solver_name = "GLOP");
    ~FactorySolver() = default;

    FactorySolver(FactorySolver&) = delete;
    FactorySolver& operator=(const FactorySolver&) = delete;

    FactorySolver(FactorySolver&&) = default;
    FactorySolver& operator=(FactorySolver&&) = default;

    [[nodiscard]] SolverResult solve(FactoryGraph &factory_graph);

    /// How a user constraint on the given port is interpreted by the solver.
    static ConstraintKind classifyConstraint(const FactoryGraph &factory_graph, const Port &port);

    /// Resolves ConstraintMode (AUTO -> LIMIT, TARGET, or PUSH) for all ports in the graph.
    static std::unordered_map<uint64_t, ConstraintMode> resolveConstraintModes(const FactoryGraph &factory_graph);

    /// Resolved constraint mode for a port from the most recent solve() call.
    ConstraintMode getResolvedConstraintMode(uint64_t port_id) const;
    const std::unordered_map<uint64_t, ConstraintMode>& getResolvedConstraintModes() const {
        return m_resolvedModes;
    }

    /// Achieved saturation ratio R_p / g_p in [0.0, 1.0] for an active goal port from the most recent solve(),
    /// or -1.0 if the port is not an active goal.
    double getGoalSaturation(uint64_t port_id) const;
    const std::unordered_map<uint64_t, double>& getGoalSaturations() const {
        return m_goalSaturations;
    }

    /// True if port_id was an active goal in the last solve() and achieved < 100% of its goal rate.
    bool isGoalBottlenecked(uint64_t port_id) const;

    double getLastSolveTime() const { return m_lastSolveTime; }
    std::string getLastSolverStatus() const { return m_lastSolverStatus; }

private:
    using Terms = std::vector<std::pair<operations_research::MPVariable *, double>>;

    struct ComponentData {
        std::vector<uint64_t> nodes;
        std::unordered_set<uint64_t> node_set;
        std::vector<uint64_t> ports;
        std::unordered_set<uint64_t> port_set;
        std::vector<uint64_t> connections;

        std::map<uint64_t, std::vector<uint64_t>> port_outgoing;
        std::map<uint64_t, std::vector<uint64_t>> port_incoming;

        std::unordered_set<uint64_t> input_ports;          // P_K^in
        std::unordered_set<uint64_t> output_ports;         // P_K^out
        std::unordered_set<uint64_t> raw_ports;            // B_K^raw
        std::unordered_set<uint64_t> imp_ports;            // B_K^imp
        std::unordered_set<uint64_t> export_ports;         // X_K
        std::unordered_set<uint64_t> sink_nodes;           // S_K
        std::unordered_set<uint64_t> sink_dedicated_nodes; // S_K^+

        std::unordered_map<uint64_t, ConstraintMode> resolved_modes;
        std::vector<uint64_t> active_goals;                // G_K
        std::unordered_set<uint64_t> target_serving_nodes; // T_K
        std::unordered_set<uint64_t> no_overflow_ports;    // Z_K
        std::vector<uint64_t> mixed_splitter_sink_conns;   // W_K^mix-sink
    };

    operations_research::MPSolver::OptimizationProblemType m_problemType;
    std::unique_ptr<operations_research::MPSolver> m_solver;

    double m_lastSolveTime = 0.0;
    std::string m_lastSolverStatus = "NOT_SOLVED";

    std::unordered_map<uint64_t, ConstraintMode> m_resolvedModes;
    std::unordered_map<uint64_t, double> m_goalSaturations;

    std::unordered_map<uint64_t, operations_research::MPVariable *> m_portVariables;
    std::unordered_map<uint64_t, operations_research::MPVariable *> m_connectionVariables;
    std::unordered_map<uint64_t, operations_research::MPVariable *> m_excessVariables;
    std::vector<std::pair<operations_research::MPVariable *, operations_research::MPVariable *>> m_nonSinkBalanceVariables;
    std::vector<std::pair<operations_research::MPVariable *, operations_research::MPVariable *>> m_balanceVariables;
    std::vector<operations_research::MPConstraint *> m_constraints;
    std::vector<double> m_solution;

    static std::vector<ComponentData> partitionComponents(const FactoryGraph &factory_graph);
    static void analyzeComponent(const FactoryGraph &factory_graph, ComponentData &comp);

    void clearComponentSolverState();
    void createComponentVariables(const FactoryGraph &factory_graph, const ComponentData &comp);
    void addComponentConstraints(const FactoryGraph &factory_graph, ComponentData &comp);
    void addRecipeConstraints(const Node &node, const Recipe &recipe);
    void addConnectionConstraints(const FactoryGraph &factory_graph, ComponentData &comp);

    Terms buildMachineActivityTerms(const FactoryGraph &factory_graph,
                                    const std::vector<uint64_t> &candidate_nodes) const;

    operations_research::MPSolver::ResultStatus solveComponentLexicographic(
        const FactoryGraph &factory_graph, const ComponentData &comp);
    operations_research::MPSolver::ResultStatus solveStage(const Terms &terms, bool maximize, double &objective_value);
    void lockStage(const Terms &terms, bool maximize, double objective_value, int stage_step = 1);
    void storeSolution();
    double solutionValue(const operations_research::MPVariable *var) const;

    void updateComponentGraph(FactoryGraph &factory_graph, const ComponentData &comp);
    static void zeroComponentGraph(FactoryGraph &factory_graph, const ComponentData &comp);
    SolverResultStatus convertSolverStatus(operations_research::MPSolver::ResultStatus status) const;
};

#endif //FACTORYSOLVER_H
