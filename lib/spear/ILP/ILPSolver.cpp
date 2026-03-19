/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "ILP/ILPSolver.h"

ILPSolver::ILPSolver(ILPModel model) : underlyingILPModel(std::move(model)), solutionModel(nullptr) {
    OsiClpSolverInterface solver;
    solver.getModelPtr()->setLogLevel(0);

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

    solutionModel = std::make_unique<CbcModel>(solver);
    solutionModel->setLogLevel(0);
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