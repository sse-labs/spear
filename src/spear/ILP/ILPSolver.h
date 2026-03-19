/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_ILPSOLVER_H
#define SPEAR_ILPSOLVER_H

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "CbcModel.hpp"
#include "OsiClpSolverInterface.hpp"
#include "ILPBuilder.h"

class ILPSolver {
public:
    explicit ILPSolver(ILPModel model);

    bool solutionExists() const;

    std::optional<double> getSolvedModelValue() const;

    std::optional<std::vector<double>> getSolvedSolution() const;

private:
    ILPModel underlyingILPModel;
    std::unique_ptr<CbcModel> solutionModel;
};

#endif  // SPEAR_ILPSOLVER_H