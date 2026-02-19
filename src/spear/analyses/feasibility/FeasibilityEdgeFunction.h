/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_FEASIBILITYEDGEFUNCTION_H
#define SPEAR_FEASIBILITYEDGEFUNCTION_H

#include <phasar/DataFlow/IfdsIde/EdgeFunction.h>
#include <llvm/IR/Value.h>           // add if not already pulled transitively
#include <z3++.h>                    // add if not already pulled transitively

#include <utility>
#include <llvm/IR/Instructions.h>
#include <phasar/DataFlow/IfdsIde/EdgeFunctionUtils.h>

#include "FeasibilityElement.h"

namespace Feasibility {

using l_t = Feasibility::FeasibilityElement;
using EF = psr::EdgeFunction<l_t>;

/**
 * Top edge function for the FeasibilityAnalysis. Maps any input to top.
 */
struct FeasibilityAddConstrainEF {
    using l_t = Feasibility::l_t;

    const uint32_t pathConditionId; // ID of the constraint to add, represented as an index into the manager's formula vector
    FeasibilityAnalysisManager *manager = nullptr;

    FeasibilityAddConstrainEF(FeasibilityAnalysisManager *manager, uint32_t C) :
    manager(manager), pathConditionId(C) {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAddConstrainEF>,
                                   const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAddConstrainEF> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityAddConstrainEF &) const = default;

    bool isConstant() const noexcept { return false; };
};

/**
 * Top edge function for the FeasibilityAnalysis. Maps any input to top.
 */
struct FeasibilityAllTopEF {
    using l_t = Feasibility::l_t;

    FeasibilityAllTopEF() {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAllTopEF>,
                                   const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAllTopEF> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityAllTopEF &) const = default;

    bool isConstant() const noexcept { return false; };
};

/**
 * Bottom edge function for the FeasibilityAnalysis. Maps any input to bottom.
 */
struct FeasibilityAllBottomEF {
    using l_t = Feasibility::l_t;

    FeasibilityAllBottomEF() {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAllBottomEF>,
                                   const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAllBottomEF> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityAllBottomEF &) const = default;

    bool isConstant() const noexcept { return false; };
};


}  // namespace Feasibility

#endif  // SPEAR_FEASIBILITYEDGEFUNCTION_H

