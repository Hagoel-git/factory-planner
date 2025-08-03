//
// Created by hagoel on 8/3/25.
//

#ifndef LPSOLVER_H
#define LPSOLVER_H
#include <iostream>
#include <memory>

#include <absl/base/log_severity.h>
#include "absl/log/globals.h"
#include "absl/strings/string_view.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "ortools/base/init_google.h"
#include "ortools/linear_solver/linear_solver.h"

class LPSolver {
public:
    static void RunLinearProgrammingExample(std::string solver_name) {
        operations_research::MPSolver::OptimizationProblemType problem_type;
        if (!operations_research::MPSolver::ParseSolverType(solver_name, &problem_type)) {
            std::cerr << "Unknown solver type: " << solver_name << std::endl;
            return;
        }

        if (!operations_research::MPSolver::SupportsProblemType(problem_type)) {
            std::cerr << "Problem type not supported" << std::endl;
            return;
        }
        operations_research::MPSolver solver("TestSolver", problem_type);
        const double infinity = operations_research::MPSolver::infinity();

        operations_research::MPVariable* const x1 = solver.MakeNumVar(0.0, infinity, "x1");
        operations_research::MPVariable* const x2 = solver.MakeNumVar(0.0, infinity, "x2");
        operations_research::MPVariable* const x3 = solver.MakeNumVar(0.0, infinity, "x3");
        operations_research::MPVariable* const x4 = solver.MakeNumVar(0.0, infinity, "x4");

        operations_research::MPObjective* const objective = solver.MutableObjective();
        objective->SetCoefficient(x1, 0);
        objective->SetCoefficient(x2, 0);
        objective->SetCoefficient(x3, 0);
        objective->SetCoefficient(x4, 1);
        objective->SetMinimization();

        operations_research::MPConstraint* const c0 = solver.MakeRowConstraint(0, 0);
        c0->SetCoefficient(x1, -0.5);
        c0->SetCoefficient(x2, 0);
        c0->SetCoefficient(x3, 1);
        c0->SetCoefficient(x4, 0);

        operations_research::MPConstraint* const c1 = solver.MakeRowConstraint(0, 0);
        c1->SetCoefficient(x1, 0);
        c1->SetCoefficient(x2, 1);
        c1->SetCoefficient(x3, -1);
        c1->SetCoefficient(x4, 0);

        operations_research::MPConstraint* const c2 = solver.MakeRowConstraint(0, 0);
        c2->SetCoefficient(x1, 0);
        c2->SetCoefficient(x2, -1);
        c2->SetCoefficient(x3, 0);
        c2->SetCoefficient(x4, 1);

        operations_research::MPConstraint* const c3 = solver.MakeRowConstraint(100, infinity);
        c3->SetCoefficient(x1, 0.5);
        c3->SetCoefficient(x2, 0);
        c3->SetCoefficient(x3, 0);
        c3->SetCoefficient(x4, 0);

        std::cout << "Number of variables = " << solver.NumVariables() << std::endl;
        std::cout << "Number of constraints = " << solver.NumConstraints() << std::endl;

        const operations_research::MPSolver::ResultStatus result_status = solver.Solve();

        if (result_status != operations_research::MPSolver::OPTIMAL) {
            std::cerr << "The problem does not have an optimal solution." << std::endl;
            return;
        }

        std::cout << "Problem solved in " << solver.wall_time() << " milliseconds" << std::endl;
        std::cout << "Solution: " << std::endl;
        std::cout << "Objective value = " << objective->Value() << std::endl
                    << "x1 = " << x1->solution_value() << std::endl
                    << "x2 = " << x2->solution_value() << std::endl
                    << "x3 = " << x3->solution_value() << std::endl
                    << "x4 = " << x4->solution_value() << std::endl;




    }
    static void RunAllExamples(int argc, char** argv) {
        //InitGoogle(argv[0], &argc, &argv, true);
        RunLinearProgrammingExample("GLOP");
        RunLinearProgrammingExample("CLP");
        RunLinearProgrammingExample("GUROBI_LP");
        RunLinearProgrammingExample("CPLEX_LP");
        RunLinearProgrammingExample("GLPK_LP");
        RunLinearProgrammingExample("XPRESS_LP");
        RunLinearProgrammingExample("PDLP");
        RunLinearProgrammingExample("HIGHS_LP");
    }
};



#endif //LPSOLVER_H
