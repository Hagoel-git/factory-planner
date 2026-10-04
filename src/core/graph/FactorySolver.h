#ifndef FACTORYSOLVER_H
#define FACTORYSOLVER_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ortools/linear_solver/linear_solver.h"

class FactoryGraph;
struct Node;
struct Port;
struct Recipe;

/**
 * Models the factory graph as a Linear Program and solves it.
 *
 * Constraint semantics (see docs/solver_redesign_spec.md):
 *  - LIMIT  (capacity): 0 <= rate <= value. Used for raw producers (recipes consuming only "nothing").
 *  - TARGET (demand):   rate + deficit - overshoot = value. Used for every other constrained port.
 *
 * The objective is lexicographic; each stage is solved to optimality and then locked in before the next:
 *  1. Minimize total target deficit (meet demands).
 *  2. Minimize target overshoot (only exceed a target when unavoidable, e.g. multi-output recipes).
 *  3. Maximize leaf throughput (reachable from constrained ports).
 *  4. Minimize waste (unconsumed output excess, then unnecessary flow).
 *  5. Balance splits/merges (min over ports of max-branch minus min-branch flow).
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
        LIMIT,  // Hard capacity: rate <= value
        TARGET  // Demand goal:   rate -> value
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

    double getLastSolveTime() const { return m_lastSolveTime; }
    std::string getLastSolverStatus() const { return m_lastSolverStatus; }
private:
    using Terms = std::vector<std::pair<operations_research::MPVariable *, double>>;

    std::unique_ptr<operations_research::MPSolver> m_solver;

    double m_lastSolveTime = 0.0;
    std::string m_lastSolverStatus = "NOT_SOLVED";

    std::unordered_map<uint64_t, operations_research::MPVariable *> m_portVariables;
    std::unordered_map<uint64_t, operations_research::MPVariable *> m_connectionVariables;
    std::unordered_map<uint64_t, operations_research::MPVariable *> m_excessVariables;    // output port -> unconsumed production
    std::vector<std::pair<operations_research::MPVariable *, operations_research::MPVariable *>> m_balanceVariables; // (max, min) per split/merge
    std::vector<operations_research::MPConstraint *> m_constraints; // position = constraint id
    std::vector<double> m_solution; // last accepted solution, indexed by MPVariable::index()

    void createAllVariables(const FactoryGraph &factory_graph);

    Terms buildThroughputTerms(const FactoryGraph &factory_graph);

    std::unordered_set<uint64_t> findReachablePorts(const FactoryGraph &factory_graph);

    void addAllConstraints(const FactoryGraph &factory_graph);
    void addRecipeConstraints(const Node &node, const Recipe &recipe);
    void addConnectionConstraints(const FactoryGraph &factory_graph);
    std::unordered_map<uint64_t, int> computeNodeDepths(const FactoryGraph &factory_graph) const;


    operations_research::MPSolver::ResultStatus solveLexicographic(const FactoryGraph &factory_graph);
    operations_research::MPSolver::ResultStatus solveStage(const Terms &terms, bool maximize, double &objective_value);
    void lockStage(const Terms &terms, bool maximize, double objective_value);
    void storeSolution();
    double solutionValue(const operations_research::MPVariable *var) const;

    void updateFactoryGraph(FactoryGraph &factory_graph) const;
    SolverResultStatus convertSolverStatus(operations_research::MPSolver::ResultStatus status) const;
};


#endif //FACTORYSOLVER_H
