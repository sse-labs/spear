/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <vector>
#include <memory>
#include <utility>

#include "ILP/ILPSolver.h"

#include <sstream>

#include "Logger.h"

namespace {

std::string boundToString(double value, bool isLowerBound) {
    if (isLowerBound && value <= -COIN_DBL_MAX / 2.0) {
        return "-inf";
    }

    if (!isLowerBound && value >= COIN_DBL_MAX / 2.0) {
        return "+inf";
    }

    std::ostringstream outputStream;
    outputStream << value;
    return outputStream.str();
}

std::string packedVectorToString(const CoinPackedVector &rowVector) {
    std::ostringstream outputStream;

    const int numberOfElements = rowVector.getNumElements();
    const int *indices = rowVector.getIndices();
    const double *elements = rowVector.getElements();

    if (numberOfElements == 0) {
        return "0";
    }

    for (int elementIndex = 0; elementIndex < numberOfElements; ++elementIndex) {
        const double coefficient = elements[elementIndex];
        const int columnIndex = indices[elementIndex];

        if (elementIndex > 0) {
            outputStream << (coefficient >= 0.0 ? " + " : " - ");
        } else if (coefficient < 0.0) {
            outputStream << "-";
        }

        outputStream << std::abs(coefficient) << "*x" << columnIndex;
    }

    return outputStream.str();
}

void dumpOsiModel(const OsiSolverInterface &solverInterface, const std::string &label) {
    Logger::getInstance().log("========== OSI MODEL DUMP BEGIN: " + label + " ==========", LOGLEVEL::INFO);

    const int numberOfColumns = solverInterface.getNumCols();
    const int numberOfRows = solverInterface.getNumRows();

    Logger::getInstance().log(
        "OSI summary: variables=" + std::to_string(numberOfColumns) +
        ", constraints=" + std::to_string(numberOfRows),
        LOGLEVEL::INFO);

    const double *columnLowerBounds = solverInterface.getColLower();
    const double *columnUpperBounds = solverInterface.getColUpper();
    const double *objectiveCoefficients = solverInterface.getObjCoefficients();
    const double *rowLowerBounds = solverInterface.getRowLower();
    const double *rowUpperBounds = solverInterface.getRowUpper();

    Logger::getInstance().log("OSI Variables:", LOGLEVEL::INFO);
    for (int columnIndex = 0; columnIndex < numberOfColumns; ++columnIndex) {
        std::ostringstream outputStream;
        outputStream << "  x" << columnIndex
                     << ": bounds=["
                     << boundToString(columnLowerBounds[columnIndex], true)
                     << ", "
                     << boundToString(columnUpperBounds[columnIndex], false)
                     << "], obj=" << objectiveCoefficients[columnIndex]
                     << ", integer=" << solverInterface.isInteger(columnIndex);
        Logger::getInstance().log(outputStream.str(), LOGLEVEL::INFO);
    }

    const CoinPackedMatrix *matrixByRow = solverInterface.getMatrixByRow();

    Logger::getInstance().log("OSI Constraints:", LOGLEVEL::INFO);
    for (int rowIndex = 0; rowIndex < numberOfRows; ++rowIndex) {
        const CoinPackedVector rowVector = matrixByRow->getVector(rowIndex);

        std::ostringstream outputStream;
        outputStream << "  c" << rowIndex << ": ";

        const double lowerBound = rowLowerBounds[rowIndex];
        const double upperBound = rowUpperBounds[rowIndex];

        if (lowerBound <= -COIN_DBL_MAX / 2.0 && upperBound < COIN_DBL_MAX / 2.0) {
            outputStream << packedVectorToString(rowVector) << " <= " << upperBound;
        } else if (upperBound >= COIN_DBL_MAX / 2.0 && lowerBound > -COIN_DBL_MAX / 2.0) {
            outputStream << packedVectorToString(rowVector) << " >= " << lowerBound;
        } else {
            outputStream << lowerBound << " <= " << packedVectorToString(rowVector) << " <= " << upperBound;
        }

        Logger::getInstance().log(outputStream.str(), LOGLEVEL::INFO);
    }

    Logger::getInstance().log("========== OSI MODEL DUMP END: " + label + " ==========", LOGLEVEL::INFO);
}

}  // namespace

ILPSolver::ILPSolver(const ILPModel& model) : underlyingILPModel(model), solutionModel(nullptr) {
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

    dumpOsiModel(solver, "after loadProblem + setInteger");

    // Create a solution instance
    solutionModel = std::make_unique<CbcModel>(solver);
    solutionModel->setLogLevel(0);

    dumpOsiModel(*solutionModel->solver(), "after CbcModel copy, before solve");

    // Pre-solve
    //solutionModel->initialSolve();

    // PERFORM THE ACTUAL SOLVING
    solutionModel->branchAndBound();

    Logger::getInstance().log(
        "CBC status debug: provenOptimal=" + std::to_string(solutionModel->isProvenOptimal()) +
        ", provenInfeasible=" + std::to_string(solutionModel->isProvenInfeasible()) +
        ", secondsLimitReached=" + std::to_string(solutionModel->isSecondsLimitReached()) +
        ", abandoned=" + std::to_string(solutionModel->isAbandoned()) +
        ", bestSolution=" + std::to_string(solutionModel->bestSolution() != nullptr),
        LOGLEVEL::INFO);
}

bool ILPSolver::solutionExists() const {
    if (!solutionModel) {
        return false;
    }

    return solutionModel->bestSolution() != nullptr;
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


ILPSolverStatus ILPSolver::getStatus() const {
    if (!solutionModel) {
        return ILPSolverStatus::NUMERICAL_ISSUES;
    }

    if (solutionModel->isProvenOptimal()) {
        return ILPSolverStatus::OPTIMAL;
    }

    if (solutionModel->isProvenInfeasible()) {
        return ILPSolverStatus::INFEASIBLE;
    }

    if (solutionModel->isSecondsLimitReached()) {
        return ILPSolverStatus::TIME_LIMIT;
    }

    if (solutionModel->isAbandoned()) {
        return ILPSolverStatus::NUMERICAL_ISSUES;
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

    if (solutionModel->isSecondsLimitReached()) {
        return "Time limit reached";
    }

    if (solutionModel->isAbandoned()) {
        return "Abandoned / numerical issues";
    }

    return "Unknown status";
}
