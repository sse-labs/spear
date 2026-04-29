/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ILP/ILPSolver.h"

#include "ILP/ILPDebug.h"

namespace {
constexpr double energyScalingFactor = 1.0;
constexpr double integerTolerance = 1.0e-12;
constexpr double solutionZeroTolerance = 1.0e-9;
constexpr bool enableSolverDebugOutput = true;

std::vector<double> buildScaledEnergyObjective(const ILPModel &model) {
    std::vector<double> scaledObjective(model.obj.size(), 0.0);

    double minimumPositiveOriginalCoefficient = std::numeric_limits<double>::max();
    double maximumPositiveOriginalCoefficient = 0.0;
    double minimumPositiveScaledCoefficient = std::numeric_limits<double>::max();
    double maximumPositiveScaledCoefficient = 0.0;
    int positiveCoefficientCount = 0;
    int tooSmallAfterScalingCount = 0;

    for (int columnIndex = 0; columnIndex < static_cast<int>(model.obj.size()); ++columnIndex) {
        const double originalCoefficient = model.obj[columnIndex];

        if (originalCoefficient <= 0.0) {
            scaledObjective[columnIndex] = 0.0;
            continue;
        }

        ++positiveCoefficientCount;

        minimumPositiveOriginalCoefficient = std::min(minimumPositiveOriginalCoefficient, originalCoefficient);
        maximumPositiveOriginalCoefficient = std::max(maximumPositiveOriginalCoefficient, originalCoefficient);

        const double scaledCoefficient = std::round(originalCoefficient * energyScalingFactor);

        if (scaledCoefficient < 1.0) {
            ++tooSmallAfterScalingCount;

            if (enableSolverDebugOutput) {
                std::cout << std::scientific << std::setprecision(12)
                          << "[ILP OBJECTIVE ERROR] Positive coefficient too small after scaling: "
                          << "x" << columnIndex
                          << " original=" << originalCoefficient
                          << " scaled=" << scaledCoefficient
                          << " factor=" << energyScalingFactor
                          << "\n";
            }

            scaledObjective[columnIndex] = 0.0;
            continue;
        }

        minimumPositiveScaledCoefficient = std::min(minimumPositiveScaledCoefficient, scaledCoefficient);
        maximumPositiveScaledCoefficient = std::max(maximumPositiveScaledCoefficient, scaledCoefficient);
        scaledObjective[columnIndex] = scaledCoefficient;
    }

    if (enableSolverDebugOutput) {
        std::cout << std::scientific << std::setprecision(12)
                  << "[ILP OBJECTIVE DEBUG] positiveCoefficients=" << positiveCoefficientCount
                  << " tooSmallAfterScaling=" << tooSmallAfterScalingCount
                  << " scalingFactor=" << energyScalingFactor
                  << "\n";

        if (positiveCoefficientCount > 0) {
            std::cout << std::scientific << std::setprecision(12)
                      << "[ILP OBJECTIVE DEBUG] originalMin=" << minimumPositiveOriginalCoefficient
                      << " originalMax=" << maximumPositiveOriginalCoefficient
                      << " originalRange=" << maximumPositiveOriginalCoefficient / minimumPositiveOriginalCoefficient
                      << "\n";
        }

        if (maximumPositiveScaledCoefficient > 0.0) {
            std::cout << std::scientific << std::setprecision(12)
                      << "[ILP OBJECTIVE DEBUG] scaledMin=" << minimumPositiveScaledCoefficient
                      << " scaledMax=" << maximumPositiveScaledCoefficient
                      << " scaledRange=" << maximumPositiveScaledCoefficient / minimumPositiveScaledCoefficient
                      << "\n";
        }
    }

    return scaledObjective;
}

void markAllColumnsAsInteger(OsiClpSolverInterface &solver) {
    const int numberOfColumns = solver.getNumCols();

    for (int columnIndex = 0; columnIndex < numberOfColumns; ++columnIndex) {
        solver.setInteger(columnIndex);
    }
}

void configureCbcModel(CbcModel &cbcModel) {
    cbcModel.setLogLevel(enableSolverDebugOutput ? 1 : 0);
    cbcModel.setAllowableGap(0.0);
    cbcModel.setAllowablePercentageGap(0.0);
    cbcModel.setAllowableFractionGap(0.0);
    cbcModel.setIntegerTolerance(integerTolerance);
    cbcModel.setDblParam(CbcModel::CbcCutoffIncrement, 0.0);
}

void printChosenSolutionDebugInformation(
    const ILPModel &model,
    const std::vector<double> &scaledObjective,
    const double *solution) {

    if (!enableSolverDebugOutput || solution == nullptr) {
        return;
    }

    double originalObjectiveValue = 0.0;
    double scaledObjectiveValue = 0.0;
    int selectedColumnCount = 0;

    std::cout << std::scientific << std::setprecision(12)
              << "[ILP SOLUTION DEBUG] selected columns:\n";

    for (int columnIndex = 0; columnIndex < model.matrix.getNumCols(); ++columnIndex) {
        const double value = std::abs(solution[columnIndex]) <= solutionZeroTolerance ? 0.0 : solution[columnIndex];

        if (value == 0.0) {
            continue;
        }

        ++selectedColumnCount;

        const double originalContribution = value * model.obj[columnIndex];
        const double scaledContribution = value * scaledObjective[columnIndex];

        originalObjectiveValue += originalContribution;
        scaledObjectiveValue += scaledContribution;

        std::cout << std::scientific << std::setprecision(12)
                  << "  x" << columnIndex
                  << " value=" << value
                  << " originalObj=" << model.obj[columnIndex]
                  << " scaledObj=" << scaledObjective[columnIndex]
                  << " originalContribution=" << originalContribution
                  << " scaledContribution=" << scaledContribution
                  << "\n";
    }

    std::cout << std::scientific << std::setprecision(12)
              << "[ILP SOLUTION DEBUG] selectedColumnCount=" << selectedColumnCount
              << " reconstructedOriginalObjective=" << originalObjectiveValue
              << " reconstructedScaledObjective=" << scaledObjectiveValue
              << "\n";
}

void printUnboundedDebugInformation(const CbcModel &cbcModel, const ILPModel &model) {
    if (!cbcModel.isContinuousUnbounded()) {
        return;
    }

    OsiSolverInterface *solverInterface = cbcModel.solver();

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
                  << " objective=" << model.obj[columnIndex]
                  << "\n";
    }

    delete[] primalRay;
}

void cleanNearZeroSolutionValues(std::vector<double> &solution) {
    for (double &value : solution) {
        if (std::abs(value) <= solutionZeroTolerance) {
            value = 0.0;
        }
    }
}
}

ILPSolver::ILPSolver(const ILPModel &model, std::string fname)
    : underlyingILPModel(model), solutionModel(nullptr) {

    OsiClpSolverInterface solver;
    solver.getModelPtr()->setLogLevel(enableSolverDebugOutput ? 1 : 0);

    const std::vector<double> scaledObjective = underlyingILPModel.obj;

    solver.loadProblem(
        underlyingILPModel.matrix,
        underlyingILPModel.col_lb.data(),
        underlyingILPModel.col_ub.data(),
        scaledObjective.data(),
        underlyingILPModel.row_lb.data(),
        underlyingILPModel.row_ub.data());

    solver.setObjSense(-1.0);
    // markAllColumnsAsInteger(solver);

    solutionModel = std::make_unique<CbcModel>(solver);
    configureCbcModel(*solutionModel);

    if (enableSolverDebugOutput) {
        std::cout << "[ILP SOLVER DEBUG] Solving model";
        if (!fname.empty()) {
            std::cout << " for " << fname;
        }
        std::cout << " columns=" << underlyingILPModel.matrix.getNumCols()
                  << " rows=" << underlyingILPModel.matrix.getNumRows()
                  << "\n";
    }

    solutionModel->branchAndBound();

    if (enableSolverDebugOutput) {
        std::cout << "[ILP SOLVER DEBUG] status=" << getStatusString()
                  << " provenOptimal=" << solutionModel->isProvenOptimal()
                  << " provenInfeasible=" << solutionModel->isProvenInfeasible()
                  << " abandoned=" << solutionModel->isAbandoned()
                  << " nodeLimit=" << solutionModel->isNodeLimitReached()
                  << " timeLimit=" << solutionModel->isSecondsLimitReached()
                  << "\n";
    }

    printUnboundedDebugInformation(*solutionModel, underlyingILPModel);

    if (fname == "emit_ancillary_info") {
        ILPDebug::dumpILPModel(model, {}, fname);
    }

    if (solutionModel->isProvenOptimal()) {
        printChosenSolutionDebugInformation(underlyingILPModel, scaledObjective, solutionModel->bestSolution());
    }
}

bool ILPSolver::solutionExists() const {
    return solutionModel && solutionModel->isProvenOptimal();
}

std::optional<double> ILPSolver::getSolvedModelValue() const {
    if (!solutionExists()) {
        return std::nullopt;
    }

    const double *solution = solutionModel->bestSolution();

    if (solution == nullptr) {
        return std::nullopt;
    }

    double originalObjectiveValue = 0.0;

    for (int columnIndex = 0; columnIndex < underlyingILPModel.matrix.getNumCols(); ++columnIndex) {
        const double cleanedValue =
            std::abs(solution[columnIndex]) <= solutionZeroTolerance ? 0.0 : solution[columnIndex];

        originalObjectiveValue += cleanedValue * underlyingILPModel.obj[columnIndex];
    }

    return originalObjectiveValue;
}

std::optional<std::vector<double>> ILPSolver::getSolvedSolution() const {
    if (!solutionExists()) {
        return std::nullopt;
    }

    const double *solution = solutionModel->bestSolution();

    if (solution == nullptr) {
        return std::nullopt;
    }

    const int numberOfColumns = underlyingILPModel.matrix.getNumCols();
    std::vector<double> cleanedSolution(solution, solution + numberOfColumns);
    cleanNearZeroSolutionValues(cleanedSolution);

    return cleanedSolution;
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