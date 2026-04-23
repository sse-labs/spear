/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <vector>
#include <memory>
#include <string>

#include "ILP/ILPSolver.h"

constexpr double objectiveScalingFactor = 1.0e12;

ILPSolver::ILPSolver(const ILPModel& model) : underlyingILPModel(model), solutionModel(nullptr) {
    OsiClpSolverInterface solver;
    solver.getModelPtr()->setLogLevel(0);

    std::vector<double> scaledObjective = underlyingILPModel.obj;
    for (double &objectiveCoefficient : scaledObjective) {
        objectiveCoefficient *= objectiveScalingFactor;
    }

    solver.loadProblem(
        underlyingILPModel.matrix,
        underlyingILPModel.col_lb.data(),
        underlyingILPModel.col_ub.data(),
        scaledObjective.data(),
        underlyingILPModel.row_lb.data(),
        underlyingILPModel.row_ub.data());

    solver.setObjSense(-1.0);

    const int numberOfColumns = underlyingILPModel.matrix.getNumCols();
    for (int columnIndex = 0; columnIndex < numberOfColumns; ++columnIndex) {
        solver.setInteger(columnIndex);
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

    return solutionModel->getObjValue() / objectiveScalingFactor;
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


ILPSolverStatus ILPSolver::getStatus() const {
    if (!solutionModel) {
        return ILPSolverStatus::INFEASIBLE;
    }

    int cbcStatus = solutionModel->status();

    switch (cbcStatus) {
        case 0: return ILPSolverStatus::INFEASIBLE;
        case 1: return ILPSolverStatus::UNBOUNDED;
        case 2: return ILPSolverStatus::TIME_LIMIT;
        case 3: return ILPSolverStatus::NUMERICAL_ISSUES;
        default: return ILPSolverStatus::INFEASIBLE;
    }
}

std::string ILPSolver::getStatusString() const {
    if (!solutionModel) {
        return "No solution model available";
    }

    int cbcStatus = solutionModel->status();

    switch (cbcStatus) {
        case 0: return "Infeasible";
        case 1: return "Unbounded";
        case 2: return "Time limit reached";
        case 3: return "Numerical issues";
        default: return "Unknown status";
    }
}
