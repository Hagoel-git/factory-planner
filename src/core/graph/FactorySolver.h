#ifndef FACTORYSOLVER_H
#define FACTORYSOLVER_H

#include "ortools/linear_solver/linear_solver.h"

class FactoryGraph;
struct Node;
struct Recipe;

class FactorySolver {
public:
    enum class SolverResultStatus {
        SUCCESS,
        INFEASIBLE,
        UNBOUNDED,
        ERROR_
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

    double getLastSolveTime() const { return m_lastSolveTime; }
    std::string getLastSolverStatus() const { return m_lastSolverStatus; }
private:
    std::unique_ptr<operations_research::MPSolver> m_solver;

    double m_lastSolveTime = 0.0;
    std::string m_lastSolverStatus = "NOT_SOLVED";

    std::unordered_map<uint64_t, operations_research::MPVariable *> m_portVariables;
    std::unordered_map<uint64_t, operations_research::MPVariable *> m_connectionVariables;
    std::unordered_map<uint64_t, operations_research::MPVariable *> m_excessVariables;
    std::unordered_map<uint64_t, operations_research::MPVariable *> m_overflowVariables;
    std::vector<operations_research::MPConstraint *> m_constraints; // position = constraint id

    void createAllVariables(const FactoryGraph &factory_graph);

    void addObjectiveFunction(const FactoryGraph &factory_graph);

    std::unordered_set<uint64_t> findReachablePorts(const FactoryGraph &factory_graph);

    void addAllConstraints(const FactoryGraph &factory_graph);
    void addRecipeConstraints(const Node &node, const Recipe &recipe);
    void addConnectionConstraints(const FactoryGraph &factory_graph);

    void updateFactoryGraph(FactoryGraph &factory_graph) const;
    SolverResultStatus convertSolverStatus(operations_research::MPSolver::ResultStatus status) const;
};


#endif //FACTORYSOLVER_H
