//
// Created by hagoel on 8/3/25.
//

#ifndef LPSOLVER_H
#define LPSOLVER_H
#include <iostream>
#include <memory>

#include "../core/Recipe.h"
#include "../core/Connection.h"
#include "../core/Node.h"

#include <absl/base/log_severity.h>
#include "absl/log/globals.h"
#include "absl/strings/string_view.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "ortools/base/init_google.h"
#include "ortools/linear_solver/linear_solver.h"

class LPSolver {
public:
    void parseNode(operations_research::MPSolver &mpSolver, Node &node, Recipe &recipe);

    void parseConnection(operations_research::MPSolver &mpSolver, const Connection &connection);

    static void anotherExample() {
        std::string solver_name = "GLOP"; // Change this to the desired solver type
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

        operations_research::MPVariable *const x0 = solver.MakeNumVar(0.0, infinity, "Recipe Activity");
        operations_research::MPVariable *const x1 = solver.MakeNumVar(0.0, infinity, "dark matter crystal");
        operations_research::MPVariable *const x2 = solver.MakeNumVar(0.0, infinity, "crystal oscillator");
        operations_research::MPVariable *const x3 = solver.MakeNumVar(0.0, infinity, "aclad aluminum sheet");
        operations_research::MPVariable *const x4 = solver.MakeNumVar(0.0, infinity, "excited photonic matter");
        operations_research::MPVariable *const x5 = solver.MakeNumVar(0.0, infinity, "superposition oscillator");
        operations_research::MPVariable *const x6 = solver.MakeNumVar(0.0, infinity, "dark matter residue");

        operations_research::MPObjective *const objective = solver.MutableObjective();
        objective->SetCoefficient(x1, 1);
        objective->SetMinimization();

        // operations_research::MPConstraint *const c0 = solver.MakeRowConstraint(0, 0);
        // c0->SetCoefficient(x1, 6);
        // c0->SetCoefficient(x2, 1);
        // c0->SetCoefficient(x3, 9);
        // c0->SetCoefficient(x4, 25);
        // c0->SetCoefficient(x5, -1);
        // c0->SetCoefficient(x6, -25);

        operations_research::MPConstraint *const c0 = solver.MakeRowConstraint(0, 0);
        c0->SetCoefficient(x1, 1);
        c0->SetCoefficient(x5, -6);

        operations_research::MPConstraint *const c1 = solver.MakeRowConstraint(0, 0);
        c1->SetCoefficient(x2, 1);
        c1->SetCoefficient(x5, -1);

        operations_research::MPConstraint *const c2 = solver.MakeRowConstraint(0, 0);
        c2->SetCoefficient(x3, 1);
        c2->SetCoefficient(x5, -9);

        operations_research::MPConstraint *const c3 = solver.MakeRowConstraint(0, 0);
        c3->SetCoefficient(x4, 1);
        c3->SetCoefficient(x5, -25);

        operations_research::MPConstraint *const c4 = solver.MakeRowConstraint(0, 0);
        c4->SetCoefficient(x5, 25);
        c4->SetCoefficient(x6, -1);


        operations_research::MPConstraint *c8 = solver.MakeRowConstraint(100, infinity);
        c8->SetCoefficient(x6, 1);

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
                << "dark matter crystal = " << x1->solution_value() << std::endl
                << "crystal oscillator = " << x2->solution_value() << std::endl
                << "aclad aluminum sheet = " << x3->solution_value() << std::endl
                << "excited photonic matter = " << x4->solution_value() << std::endl
                << "superposition oscillator = " << x5->solution_value() << std::endl
                << "dark matter residue = " << x6->solution_value() << std::endl;
    }

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

        operations_research::MPVariable *const x1 = solver.MakeNumVar(0.0, infinity, "x1");
        operations_research::MPVariable *const x2 = solver.MakeNumVar(0.0, infinity, "x2");
        operations_research::MPVariable *const x3 = solver.MakeNumVar(0.0, infinity, "x3");
        operations_research::MPVariable *const x4 = solver.MakeNumVar(0.0, infinity, "x4");

        operations_research::MPObjective *const objective = solver.MutableObjective();
        objective->SetCoefficient(x4, 1);
        objective->SetMinimization();

        operations_research::MPConstraint *const c0 = solver.MakeRowConstraint(0, 0);
        c0->SetCoefficient(x1, -0.5);
        c0->SetCoefficient(x3, 1);

        operations_research::MPConstraint *const c1 = solver.MakeRowConstraint(0, 0);
        c1->SetCoefficient(x2, 1000);
        c1->SetCoefficient(x3, -1000);

        operations_research::MPConstraint *const c2 = solver.MakeRowConstraint(0, 0);
        c2->SetCoefficient(x2, -10);
        c2->SetCoefficient(x4, 10);

        operations_research::MPConstraint *const c3 = solver.MakeRowConstraint(1000, infinity);
        c3->SetCoefficient(x1, 5);

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

    static void RunAllExamples() {
        RunLinearProgrammingExample("GLOP");
        std::cout << std::endl;
        std::cout << std::endl;
        std::cout << std::endl;
        std::cout << std::endl;
        std::cout << std::endl;
        anotherExample();
    }
};


#endif //LPSOLVER_H
