/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <vector>
#include <memory>
#include <utility>

#include "ILP/ILPSolver.h"

ILPSolver::ILPSolver(ILPModel model) : underlyingILPModel(std::move(model)), solutionModel(nullptr) {
    // Create a new solver instance
    OsiClpSolverInterface solver;
    // Limit the log level of the solver to 0 to not get freacking spammed with log messages from the underlying solver.
    solver.getModelPtr()->setLogLevel(0);

    // Inser the given ILP-Model into the solver
    solver.loadProblem(underlyingILPModel.matrix,
        underlyingILPModel.col_lb.data(),
        underlyingILPModel.col_ub.data(),
        underlyingILPModel.obj.data(),
        underlyingILPModel.row_lb.data(),
        underlyingILPModel.row_ub.data());

    // Maximize objective
    solver.setObjSense(-1.0);

    // Mark all columns as integer
    const int numCols = underlyingILPModel.matrix.getNumCols();
    for (int c = 0; c < numCols; ++c) {
        solver.setInteger(c);
    }

    // Create a solution instance
    solutionModel = std::make_unique<CbcModel>(solver);
    solutionModel->setLogLevel(0);

    // Pre-solve
    solutionModel->initialSolve();

    // PERFORM THE ACTUAL SOLVING
    solutionModel->branchAndBound();
}

bool ILPSolver::solutionExists() const {
    if (!solutionModel) {
        return false;
    }

    return solutionModel->isProvenOptimal();
}

std::optional<double> ILPSolver::getSolvedModelValue() const {
    if (!solutionExists()) {
        return std::nullopt;
    }

    return solutionModel->getObjValue();
}

std::optional<std::vector<double>> ILPSolver::getSolvedSolution() const {
    if (!solutionExists()) {
        return std::nullopt;
    }

    const double* solution = solutionModel->bestSolution();
    if (!solution) {
        return std::nullopt;
    }

    const int size = underlyingILPModel.matrix.getNumCols();
    return std::vector<double>(solution, solution + size);
}
