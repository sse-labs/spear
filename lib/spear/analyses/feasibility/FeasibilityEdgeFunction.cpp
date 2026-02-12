/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <algorithm>

#include "analyses/feasibility/FeasibilityEdgeFunction.h"

#include <phasar/DataFlow/IfdsIde/EdgeFunctionUtils.h>

namespace Feasibility {

// --- FeasibilityIdentityEF ---
l_t FeasibilityIdentityEF::computeTarget(const l_t &source) const {
    // Preserve IDE special elements exactly.
    if (source.isIdeNeutral() || source.isIdeAbsorbing()) {
        return source;
    }
    return source;
}
EF FeasibilityIdentityEF::compose(psr::EdgeFunctionRef<FeasibilityIdentityEF>, const EF &secondFunction) {
    return secondFunction;
}
EF FeasibilityIdentityEF::join(psr::EdgeFunctionRef<FeasibilityIdentityEF>, const EF &otherFunc) {
    return otherFunc;
}
bool FeasibilityIdentityEF::isConstant() const noexcept { return false; }

// --- FeasibilityAllTopEF ---
l_t FeasibilityAllTopEF::computeTarget(const l_t &source) const {
    // IDE all-top must map to the IDE-neutral element (identity for lifting).
    return l_t::ideNeutral(source.getStore());
}
EF FeasibilityAllTopEF::compose(psr::EdgeFunctionRef<FeasibilityAllTopEF> thisFunc, const EF &) {
    return thisFunc; // Top overwrites everything
}
EF FeasibilityAllTopEF::join(psr::EdgeFunctionRef<FeasibilityAllTopEF> thisFunc, const EF &) {
    return thisFunc; // Top dominates
}
bool FeasibilityAllTopEF::isConstant() const noexcept { return true; }

// --- FeasibilityAllBottomEF ---
l_t FeasibilityAllBottomEF::computeTarget(const l_t &source) const {
    // IDE bottom must be absorbing.
    return l_t::ideAbsorbing(source.getStore());
}
EF FeasibilityAllBottomEF::compose(psr::EdgeFunctionRef<FeasibilityAllBottomEF> thisFunc, const EF &) {
    return thisFunc; // Bottom overwrites everything (for your lattice semantics)
}
EF FeasibilityAllBottomEF::join(psr::EdgeFunctionRef<FeasibilityAllBottomEF>, const EF &otherFunc) {
    return otherFunc; // other >= bottom
}
bool FeasibilityAllBottomEF::isConstant() const noexcept { return true; }

// --- FeasibilityAssumeEF ---
l_t FeasibilityAssumeEF::computeTarget(const l_t &source) const {
    if (Negate) {
        return source.assume(!Cond);
    }
    return source.assume(Cond);
}
EF FeasibilityAssumeEF::compose(psr::EdgeFunctionRef<FeasibilityAssumeEF> thisFunc, const EF &secondFunction) {
    if (secondFunction.template isa<FeasibilityIdentityEF>() ||
        secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF{thisFunc};
    }

    if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    if (secondFunction.template isa<FeasibilityAllTopEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

EF FeasibilityAssumeEF::join(psr::EdgeFunctionRef<FeasibilityAssumeEF> thisFunc, const psr::EdgeFunction<l_t> &otherFunc) {
    if (otherFunc.template isa<FeasibilityAllBottomEF>() ||
        otherFunc.template isa<FeasibilityIdentityEF>() ||
        otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF{thisFunc};
    }

    if (otherFunc.template isa<FeasibilityAllTopEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    return EF(std::in_place_type<FeasibilityAllTopEF>);
}
bool FeasibilityAssumeEF::operator==(const FeasibilityAssumeEF &Other) const {
    return Negate == Other.Negate && z3::eq(Cond, Other.Cond);
}
bool FeasibilityAssumeEF::isConstant() const noexcept { return false; }

// --- FeasibilitySetSSAEF ---
l_t FeasibilitySetSSAEF::computeTarget(const l_t &source) const {
    return source.setSSA(Key, ValueExpr);
}
EF FeasibilitySetSSAEF::compose(psr::EdgeFunctionRef<FeasibilitySetSSAEF> thisFunc, const EF &secondFunction) {
    if (secondFunction.template isa<FeasibilityIdentityEF>() ||
        secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF{thisFunc};
    }
    if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }
    if (secondFunction.template isa<FeasibilityAllTopEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }
    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

EF FeasibilitySetSSAEF::join(psr::EdgeFunctionRef<FeasibilitySetSSAEF> thisFunc, const psr::EdgeFunction<l_t> &otherFunc) {
    if (otherFunc.template isa<FeasibilityAllBottomEF>() ||
        otherFunc.template isa<FeasibilityIdentityEF>() ||
        otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF{thisFunc};
    }
    if (otherFunc.template isa<FeasibilityAllTopEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }
    return EF(std::in_place_type<FeasibilityAllTopEF>);
}
bool FeasibilitySetSSAEF::isConstant() const noexcept { return false; }

// --- FeasibilitySetMemEF ---
l_t FeasibilitySetMemEF::computeTarget(const l_t &source) const {
    return source.setMem(Loc, ValueExpr);
}
EF FeasibilitySetMemEF::compose(psr::EdgeFunctionRef<FeasibilitySetMemEF> thisFunc, const EF &secondFunction) {
    if (secondFunction.template isa<FeasibilityIdentityEF>() ||
        secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF{thisFunc};
    }
    if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }
    if (secondFunction.template isa<FeasibilityAllTopEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }
    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

EF FeasibilitySetMemEF::join(psr::EdgeFunctionRef<FeasibilitySetMemEF> thisFunc, const psr::EdgeFunction<l_t> &otherFunc) {
    if (otherFunc.template isa<FeasibilityAllBottomEF>() ||
        otherFunc.template isa<FeasibilityIdentityEF>() ||
        otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF{thisFunc};
    }
    if (otherFunc.template isa<FeasibilityAllTopEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }
    return EF(std::in_place_type<FeasibilityAllTopEF>);
}
bool FeasibilitySetMemEF::isConstant() const noexcept { return false; }

// --- factories declared in header ---
EF edgeIdentity() { return FeasibilityIdentityEF{}; }
EF edgeTop() { return FeasibilityAllTopEF{}; }
EF edgeBottom() { return FeasibilityAllBottomEF{}; }

} // namespace Feasibility

