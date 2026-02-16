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

#include "FeasibilityElement.h"

namespace Feasibility {

using l_t = Feasibility::FeasibilityElement;
using EF = psr::EdgeFunction<l_t>;

/**
 * Identity edge function for the FeasibilityAnalysis. Maps any input to itself.
 */
struct FeasibilityIdentityEF {
    using l_t = Feasibility::l_t;

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityIdentityEF>,
                                   const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityIdentityEF> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityIdentityEF &) const = default;

    bool isConstant() const noexcept;
};

/**
 * Top edge function for the FeasibilityAnalysis. Maps any input to top.
 */
struct FeasibilityAllTopEF {
    using l_t = Feasibility::l_t;

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAllTopEF>,
                                   const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAllTopEF> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityAllTopEF &) const = default;

    bool isConstant() const noexcept;
};

/**
 * Bottom edge function for the FeasibilityAnalysis. Maps any input to bottom.
 */
struct FeasibilityAllBottomEF {
    using l_t = Feasibility::l_t;

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAllBottomEF>,
                                   const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAllBottomEF> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityAllBottomEF &) const = default;

    bool isConstant() const noexcept;
};

/**
 * Assume edge function for the FeasibilityAnalysis. Maps any input to the result of assuming a condition.
 * The condition is stored as a z3::expr in the edge function and is applied to the input element when computeTarget is called.
 */
struct FeasibilityAssumeEF {
    using l_t = Feasibility::l_t;

    const z3::expr Cond;

    FeasibilityAssumeEF(z3::expr ValueExpr)
        : Cond(ValueExpr) {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAssumeEF>,
                                   const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAssumeEF> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    // Required by psr::EdgeFunction: must be equality comparable (unless empty).
    bool operator==(const FeasibilityAssumeEF &Other) const;

    bool isConstant() const noexcept;
};

/**
 * SSA edge function to manipulate SSA store of the FeasibilityElement.
 * Maps any input to the result of updating the SSA store with a new binding.
 */
struct FeasibilitySetSSAEF {
    using l_t = Feasibility::l_t;

    const llvm::Value *Key = nullptr;
    const llvm::Value *Loc = nullptr;

    // Direct set: SSA[Key] := ValueExpr
    FeasibilitySetSSAEF(const llvm::Value *Key, const llvm::Value *Loc)
        : Key(Key), Loc(Loc) {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;
    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilitySetSSAEF>, const EF &secondFunction);
    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilitySetSSAEF>, const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilitySetSSAEF &O) const = default;

    bool isConstant() const noexcept;
};

/**
 * Memory edge function to manipulate memory store of the FeasibilityElement.
 * Maps any input to the result of updating the memory store with a new binding.
 */
struct FeasibilitySetMemEF {
    using l_t = Feasibility::l_t;

    const llvm::Value *Loc = nullptr;
    FeasibilityStateStore::ExprId ValueId = 0;

    FeasibilitySetMemEF(const llvm::Value *Loc, FeasibilityStateStore::ExprId ValueExpr)
        : Loc(Loc), ValueId(ValueExpr) {}


    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilitySetMemEF>,
                                   const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilitySetMemEF> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilitySetMemEF &) const = default;

    bool isConstant() const noexcept;
};

/**
 * Sequential composition edge function.
 * Represents Second ∘ First, i.e., computeTarget(x) = Second(First(x)).
 */
struct FeasibilitySeqEF {
    using l_t = Feasibility::l_t;

    EF First;
    EF Second;

    FeasibilitySeqEF(EF First, EF Second)
        : First(std::move(First)), Second(std::move(Second)) {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilitySeqEF> thisFunc,
                                    const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilitySeqEF> thisFunc,
                                 const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilitySeqEF &) const = default;

    bool isConstant() const noexcept;
};

/**
 * Pointwise join edge function.
 * Represents (First ⊔ Second), i.e., computeTarget(x) = First(x) ⊔ Second(x).
 *
 * This is necessary because the IDE solver joins edge functions, not lattice values.
 */
struct FeasibilityJoinEF {
    using l_t = Feasibility::l_t;

    EF Left;
    EF Right;

    FeasibilityJoinEF(EF Left, EF Right)
        : Left(std::move(Left)), Right(std::move(Right)) {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityJoinEF> thisFunc,
                                    const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityJoinEF> thisFunc,
                                 const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityJoinEF &) const = default;

    bool isConstant() const noexcept;
};

struct FeasibilityAssumeIcmpEF {
    using l_t = Feasibility::l_t;

    const llvm::ICmpInst *Cmp = nullptr;
    bool TakeTrueEdge = true; // if false => use !cond

    FeasibilityAssumeIcmpEF(const llvm::ICmpInst *C, bool T)
        : Cmp(C), TakeTrueEdge(T) {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAssumeIcmpEF>,
                                    const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAssumeIcmpEF> thisFunc,
                                 const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityAssumeIcmpEF &O) const {
        return Cmp == O.Cmp && TakeTrueEdge == O.TakeTrueEdge;
    }

    bool isConstant() const noexcept { return false; }
};

EF edgeIdentity();

EF edgeTop();

EF edgeBottom();

}  // namespace Feasibility

#endif  // SPEAR_FEASIBILITYEDGEFUNCTION_H

