/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <vector>
#include <memory>
#include <string>

#include "ILP/ILPSolver.h"

/**
 * Scaling factor to the objective function values.
 * Reduces errors where the solver returns a solution that is slightly worse than the optimal solution due to numerical
 */
constexpr double objectiveScalingFactor = 1.0e15;

ILPSolver::ILPSolver(const ILPModel& model) : underlyingILPModel(model), solutionModel(nullptr) {
    OsiClpSolverInterface solver;
    solver.getModelPtr()->setLogLevel(0);

    /**
     * Scale the objective function with the objectiveScalingFactor
     */
    std::vector<double> scaledObjective = underlyingILPModel.obj;
    for (double &objectiveCoefficient : scaledObjective) {
        objectiveCoefficient *= objectiveScalingFactor;
    }

    // Load the problem into the solver
    solver.loadProblem(
        underlyingILPModel.matrix,
        underlyingILPModel.col_lb.data(),
        underlyingILPModel.col_ub.data(),
        scaledObjective.data(),
        underlyingILPModel.row_lb.data(),
        underlyingILPModel.row_ub.data());

    // Set the goal of the solver to maximize (-1 max/ +1 min)
    solver.setObjSense(-1.0);

    // Set the expected values to be integers, as edges can only be executed integer-wise
    const int numberOfColumns = underlyingILPModel.matrix.getNumCols();
    for (int columnIndex = 0; columnIndex < numberOfColumns; ++columnIndex) {
        solver.setInteger(columnIndex);
    }

    solutionModel = std::make_unique<CbcModel>(solver);
    solutionModel->setLogLevel(0);

    // Execute the actual solving
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

    // When returning optimal solution we have to scale it back using the objectivescalingfactor
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

    if (solutionModel->isProvenOptimal()) {
        return ILPSolverStatus::OPTIMAL;
    }

    if (solutionModel->isProvenInfeasible()) {
        return ILPSolverStatus::INFEASIBLE;
    }

    if (solutionModel->isContinuousUnbounded()) {
        return ILPSolverStatus::UNBOUNDED;
    }

    if (solutionModel->isAbandoned()) {
        return ILPSolverStatus::NUMERICAL_ISSUES;
    }

    if (solutionModel->isSecondsLimitReached() || solutionModel->isNodeLimitReached()) {
        return ILPSolverStatus::TIME_LIMIT;
    }

    return ILPSolverStatus::UNKNOWN;
}

std::string ILPSolver::getStatusString() const {
    if (!solutionModel) {
        return "No solution model available";
    }

    if (solutionModel->isProvenOptimal()) {
        return "Optimal";
    }

    if (solutionModel->isProvenInfeasible()) {
        return "Infeasible";
    }

    if (solutionModel->isContinuousUnbounded()) {
        return "Unbounded";
    }

    if (solutionModel->isAbandoned()) {
        return "Numerical issues";
    }

    if (solutionModel->isSecondsLimitReached()) {
        return "Time limit reached";
    }

    if (solutionModel->isNodeLimitReached()) {
        return "Node limit reached";
    }

    return "Unknown status";
}
