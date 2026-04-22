/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_ILP_ILPSOLVER_H_
#define SRC_SPEAR_ILP_ILPSOLVER_H_

#include <memory>
#include <optional>
#include <vector>

#include <CbcModel.hpp>
#include <OsiClpSolverInterface.hpp>
#include "ILPBuilder.h"

enum ILPSolverStatus {
    INFEASIBLE,
    UNBOUNDED,
    TIME_LIMIT,
    NUMERICAL_ISSUES,
    OPTIMAL,
    UNKNOWN
};

/**
 * ILPSolver class.
 * Handles the solving of CBC ILP models with the underlying solver API exposed by CBC
 */
class ILPSolver {
 public:
    /**
     * Create a new solver for the given ILPModel
     * @param model Model to construct the solver for
     */
    explicit ILPSolver(const ILPModel& model);

    /**
     * Check if a solution for the solved model was found
     * @return True if a solution was found, false otherwise
     */
    bool solutionExists() const;

    /**
     * Query the solver for the optimal value of the solved model
     * @return Returns the optimal value if a solution was found, std::nullopt otherwise
     */
    std::optional<double> getSolvedModelValue() const;

    /**
     * Query the solver for the values of the variables in the optimal solution of the solved model
     * @return Returns the values of the variables as double vector, if a solution was found. std::nullopt otherwise
     */
    std::optional<std::vector<double>> getSolvedSolution() const;

    /**
     * Get the status of the solver after solving the model
     * @return Status of the solver as ILPSolverStatus enum value
     */
    ILPSolverStatus getStatus() const;

    /**
     *
     * @return
     */
    std::string getStatusString() const;

 private:
    /**
     * Model the solver was build upon
     */
    ILPModel underlyingILPModel;

    /**
     * Solution exposed by the CBC API
     */
    std::unique_ptr<CbcModel> solutionModel;
};

#endif  // SRC_SPEAR_ILP_ILPSOLVER_H_
