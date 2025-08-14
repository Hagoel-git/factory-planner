//
// Created by hagoel on 8/4/25.
//

#ifndef FACTORYSOLVER_H
#define FACTORYSOLVER_H


#include "FactoryGraph.h"
#include "ortools/linear_solver/linear_solver.h"


class FactorySolver {
public:
    enum class SolverResult {
        SUCCESS,
        INFEASIBLE,
        UNBOUNDED,
        ERROR
    };

    FactorySolver(const std::string& solver_name = "GLOP");
    ~FactorySolver() = default;

    // Delete copy constructor and assignment operator to prevent issues with solver ownership
    FactorySolver(FactorySolver&) = delete;
    FactorySolver& operator=(const FactorySolver&) = delete;

    // Move constructor and assignment are okay
    FactorySolver(FactorySolver&&) = default;
    FactorySolver& operator=(FactorySolver&&) = default;

    SolverResult solve(FactoryGraph &factory_graph);

    double getLastSolveTime() const { return last_solve_time; }
    std::string getLastSolverStatus() const { return last_solver_status; }
private:
    std::unique_ptr<operations_research::MPSolver> solver_;
    const double infinity = operations_research::MPSolver::infinity();

    double last_solve_time = 0.0;
    std::string last_solver_status = "NOT_SOLVED";

    std::unordered_map<int, operations_research::MPVariable *> variables; // position = port id
    std::vector<operations_research::MPConstraint *> constraints; // position = constraint id

    void createAllVariables(const FactoryGraph &factory_graph);

    void addObjectiveFunction(const FactoryGraph &factory_graph);

    std::unordered_set<int> findReachablePorts(const FactoryGraph &factory_graph);

    void addAllConstraints(const FactoryGraph &factory_graph);
    void addRecipeConstraints(const Node &node, const Recipe &recipe);
    void addConnectionConstraints(const FactoryGraph &factory_graph);

    void updateFactoryGraph(FactoryGraph &factory_graph) const;
    SolverResult convertSolverStatus(operations_research::MPSolver::ResultStatus status) const;
};


#endif //FACTORYSOLVER_H
