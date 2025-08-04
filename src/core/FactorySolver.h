//
// Created by hagoel on 8/4/25.
//

#ifndef FACTORYSOLVER_H
#define FACTORYSOLVER_H


#include <absl/base/log_severity.h>

#include "FactoryGraph.h"
#include "absl/log/globals.h"
#include "absl/strings/string_view.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "ortools/base/init_google.h"
#include "ortools/linear_solver/linear_solver.h"


class FactorySolver {
public:
    void solve(const FactoryGraph &factory_graph);

    FactorySolver() {
        std::string solver_name = "GLOP";
        operations_research::MPSolver::OptimizationProblemType problem_type;
        if (!operations_research::MPSolver::ParseSolverType(solver_name, &problem_type)) {
            std::cerr << "Unknown solver type: " << solver_name << std::endl;
            return;
        }
        if (!operations_research::MPSolver::SupportsProblemType(problem_type)) {
            std::cerr << "Problem type not supported" << std::endl;
            return;
        }
        solver = new operations_research::MPSolver("FactorySolver", problem_type);
    }

private:
    operations_research::MPSolver *solver;
    double infinity = operations_research::MPSolver::infinity();

    std::vector<operations_research::MPVariable *> variables; // position = port id
    std::vector<operations_research::MPConstraint *> constraints; // position = constraint id

    void createAllVariables(const FactoryGraph &factory_graph);

    void addObjectiveFunction(const FactoryGraph &factory_graph);

    void addAllConstraints(const FactoryGraph &factory_graph);
    void addRecipeConstraints(const Node &node, const Recipe &recipe);
    void addConnectionConstraints(const FactoryGraph &factory_graph);
};


#endif //FACTORYSOLVER_H
