/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <vector>
#include <memory>
#include <string>

#include "ILP/ILPSolver.h"

constexpr double objectiveScalingFactor = 1.0e10;

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

    if (solutionModel->isContinuousUnbounded()) {
        OsiSolverInterface *solverInterface = solutionModel->solver();

        if (solverInterface == nullptr) {
            std::cout << "[ILP DEBUG] Unbounded, but solver interface is null.\n";
            return;
        }

        std::vector<double *> primalRays = solverInterface->getPrimalRays(1);

        if (primalRays.empty() || primalRays[0] == nullptr) {
            std::cout << "[ILP DEBUG] Unbounded, but no primal ray was returned.\n";
            return;
        }

        double *primalRay = primalRays[0];
        const int numberOfSolverColumns = solverInterface->getNumCols();

        std::cout << "[ILP DEBUG] Unbounded primal ray:\n";

        for (int columnIndex = 0; columnIndex < numberOfSolverColumns; ++columnIndex) {
            if (std::abs(primalRay[columnIndex]) <= 1.0e-9) {
                continue;
            }

            std::cout << "  x" << columnIndex
                      << " direction=" << primalRay[columnIndex]
                      << " objective=" << underlyingILPModel.obj[columnIndex]
                      << "\n";
        }

        delete[] primalRay;
    }
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