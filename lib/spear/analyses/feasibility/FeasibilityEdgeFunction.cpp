/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityEdgeFunction.h"
#include "analyses/feasibility/util.h"

namespace Feasibility {

// ============================================================================
// FeasibilityIdentityEF
// ============================================================================
l_t FeasibilityIdentityEF::computeTarget(const l_t &source) const {
    return source;
}

EF FeasibilityIdentityEF::compose(psr::EdgeFunctionRef<FeasibilityIdentityEF>,
                                  const EF &secondFunction) {
    return secondFunction;
}

EF FeasibilityIdentityEF::join(psr::EdgeFunctionRef<FeasibilityIdentityEF>,
                               const psr::EdgeFunction<l_t> &otherFunc) {
    // pointwise join(id, other) is generally not representable -> use JoinEF
    return otherFunc;
}

bool FeasibilityIdentityEF::isConstant() const noexcept { return false; }

// ============================================================================
// FeasibilityAllTopEF  (edge-function TOP, maps to value-top "unknown")
// ============================================================================
l_t FeasibilityAllTopEF::computeTarget(const l_t &source) const {
    if (source.isBottom()) {
        return source;
    }
    return l_t::top(source.getStore());
}

EF FeasibilityAllTopEF::compose(psr::EdgeFunctionRef<FeasibilityAllTopEF> thisFunc,
                                const EF &second) {

    if (Feasibility::Util::F_DebugEnabled.load()) {
        llvm::errs() << "[FDBG] AllTop::compose second=";
        Feasibility::Util::dumpEF(second);
        llvm::errs() << " kind=";
        Feasibility::Util::dumpEFKind(second);
        llvm::errs() << "\n";
    }

    // Top maps any non-bottom to ⊤, preserves ⊥.
    if (second.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(second)) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    if (second.template isa<FeasibilityAllTopEF>() ||
        llvm::isa<psr::AllTop<l_t>>(second)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    if (second.template isa<FeasibilityAssumeEF>()) {
        return EF{second}; // DEBUG: let assume survive
    }


    // Any other second gets ⊤ as input => conservative is ⊤
    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

EF FeasibilityAllTopEF::join(psr::EdgeFunctionRef<FeasibilityAllTopEF> thisFunc,
                             const psr::EdgeFunction<l_t> &) {
    // top dominates join
    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

bool FeasibilityAllTopEF::isConstant() const noexcept { return false; }

// ============================================================================
// FeasibilityAllBottomEF (edge-function BOTTOM, maps to value-bottom "infeasible")
// ============================================================================
l_t FeasibilityAllBottomEF::computeTarget(const l_t &source) const {
    if (source.isBottom()) {
        return source;
    }
    return l_t::bottom(source.getStore());
}

EF FeasibilityAllBottomEF::compose(psr::EdgeFunctionRef<FeasibilityAllBottomEF> thisFunc,
                                   const EF &secondFunction) {
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
}

EF FeasibilityAllBottomEF::join(psr::EdgeFunctionRef<FeasibilityAllBottomEF>,
                                const psr::EdgeFunction<l_t> &otherFunc) {
    // bottom is neutral for join
    return otherFunc;
}

bool FeasibilityAllBottomEF::isConstant() const noexcept { return true; }

// ============================================================================
// FeasibilityAssumeEF
// ============================================================================
l_t FeasibilityAssumeEF::computeTarget(const l_t &source) const {
    if (source.isBottom()) {
        return source;
    }

    if (source.isTop()) {
        return source;
    }


    return source.assume(Cond);
}

EF FeasibilityAssumeEF::compose(psr::EdgeFunctionRef<FeasibilityAssumeEF> thisFunc,
                                const EF &secondFunction) {

    if (Feasibility::Util::F_DebugEnabled.load()) {
        llvm::errs() << "[FDBG] AssumeEF::compose second=";
        Feasibility::Util::dumpEF(secondFunction);
        llvm::errs() << " kind=";
        Feasibility::Util::dumpEFKind(secondFunction);
        llvm::errs() << "\n";
    }

    if (secondFunction.template isa<FeasibilityIdentityEF>() ||
        secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF{thisFunc};
    }
    if (secondFunction.template isa<FeasibilityAllTopEF>()) {
        // assume then allTop => allTop overwrites
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }
    if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }
    if (secondFunction.template isa<FeasibilityAssumeEF>()) {
        const auto &other = secondFunction.template cast<FeasibilityAssumeEF>();
        z3::expr composed = thisFunc->Cond && other->Cond;
        return EF(std::in_place_type<FeasibilityAssumeEF>, composed);
    }

    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

EF FeasibilityAssumeEF::join(psr::EdgeFunctionRef<FeasibilityAssumeEF> thisFunc,
                             const psr::EdgeFunction<l_t> &other) {
    // IMPORTANT: Identity and Bottom are neutral here.
    if (other.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(other) ||
        other.template isa<FeasibilityIdentityEF>() ||
        other.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(thisFunc);
    }

    if (other.template isa<FeasibilityAllTopEF>() ||
        llvm::isa<psr::AllTop<l_t>>(other)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    if (other.template isa<FeasibilitySetSSAEF>() || other.template isa<FeasibilitySetMemEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    if (other.template isa<FeasibilityAssumeEF>()) {
        const auto &otherfunc = other.template cast<FeasibilityAssumeEF>();
        z3::expr joined = thisFunc->Cond || otherfunc->Cond;
        return EF(std::in_place_type<FeasibilityAssumeEF>, joined);
    }

    // In general: pointwise join, use wrapper
    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

bool FeasibilityAssumeEF::operator==(const FeasibilityAssumeEF &Other) const {
    return z3::eq(Cond, Other.Cond);
}

bool FeasibilityAssumeEF::isConstant() const noexcept { return false; }

// ============================================================================
// FeasibilitySetSSAEF
// ============================================================================
l_t FeasibilitySetSSAEF::computeTarget(const l_t &source) const {
    if (source.isBottom()) {
        return source;
    }

    if (source.isTop()) {
        return source;
    }

    return source.setSSA(Key, ValueExpr);
}

EF FeasibilitySetSSAEF::compose(psr::EdgeFunctionRef<FeasibilitySetSSAEF> thisFunc,
                                const EF &secondFunction) {
    if (secondFunction.template isa<FeasibilityIdentityEF>() ||
        secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF{thisFunc};
    }

    if (secondFunction.template isa<FeasibilityAllTopEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    if (secondFunction.template isa<FeasibilityAssumeEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    if (secondFunction.template isa<FeasibilitySetMemEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    if (secondFunction.template isa<FeasibilitySetSSAEF>()) {
        const auto &other = secondFunction.template cast<FeasibilitySetSSAEF>();
        // setSSA then setSSA:
        // - if same key, later overwrites -> return 'other'
        // - else need both -> Seq
        if (thisFunc->Key == other->Key) {
            return EF{secondFunction};
        }

        return EF(std::in_place_type<FeasibilitySetSSAEF>, thisFunc->Key, thisFunc->ValueExpr);
    }

    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

EF FeasibilitySetSSAEF::join(psr::EdgeFunctionRef<FeasibilitySetSSAEF> thisFunc,
                             const psr::EdgeFunction<l_t> &other) {
    // IMPORTANT: Identity and Bottom are neutral here.
    if (other.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(other) ||
        other.template isa<FeasibilityIdentityEF>() ||
        other.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(thisFunc);
    }

    if (other.template isa<FeasibilityAllTopEF>() ||
        llvm::isa<psr::AllTop<l_t>>(other)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
        }

    // Do not mix lattice element types
    if (other.template isa<FeasibilitySetMemEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    if (other.template isa<FeasibilityAssumeEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    if (other.template isa<FeasibilitySetSSAEF>()) {
        const auto &otherfunc = other.template cast<FeasibilitySetSSAEF>();
        // If same update, keep one; otherwise pointwise join needed.
        if (*thisFunc == *otherfunc) {
            return EF{thisFunc};
        }

        return EF(std::in_place_type<FeasibilitySetSSAEF>, thisFunc->Key, thisFunc->ValueExpr);
    }

    // Mixing Elements => conservative
    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

bool FeasibilitySetSSAEF::isConstant() const noexcept { return false; }

// ============================================================================
// FeasibilitySetMemEF
// ============================================================================
l_t FeasibilitySetMemEF::computeTarget(const l_t &source) const {
    if (source.isBottom()) {
        return source;
    }

    if (source.isTop()) {
        return source;
    }

    return source.setMem(Loc, ValueExpr);
}

EF FeasibilitySetMemEF::compose(psr::EdgeFunctionRef<FeasibilitySetMemEF> self,
                                const EF &second) {
    // second ∘ collect
    if (second.template isa<FeasibilityIdentityEF>() ||
        second.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(self);
        }

    if (second.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(second)) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
        }

    if (second.template isa<FeasibilityAllTopEF>() ||
        llvm::isa<psr::AllTop<l_t>>(second)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
        }

    // Do not mix lattice element types
    if (second.template isa<FeasibilityAssumeEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    if (second.template isa<FeasibilitySetSSAEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    if (second.template isa<FeasibilitySetMemEF>()) {
        const auto &other = second.template cast<FeasibilitySetMemEF>();
        if (self->Loc == other->Loc) {
            return EF{second};
        }
        return EF(std::in_place_type<FeasibilitySetMemEF>, self->Loc, other->ValueExpr);
    }

    // Mixing families => conservative
    return EF(std::in_place_type<FeasibilityAllTopEF>);


}

EF FeasibilitySetMemEF::join(psr::EdgeFunctionRef<FeasibilitySetMemEF> thisFunc,
                             const psr::EdgeFunction<l_t> &other) {
    // IMPORTANT: Identity and Bottom are neutral here.
    if (other.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(other) ||
        other.template isa<FeasibilityIdentityEF>() ||
        other.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(thisFunc);
        }

    if (other.template isa<FeasibilityAllTopEF>() ||
        llvm::isa<psr::AllTop<l_t>>(other)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
        }

    // Do not mix lattice element types
    if (other.template isa<FeasibilityAssumeEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    if (other.template isa<FeasibilitySetSSAEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    if (other.template isa<FeasibilitySetMemEF>()) {
        const auto &otherFunc = other.template cast<FeasibilitySetMemEF>();
        if (*thisFunc == *otherFunc) {
            return EF{thisFunc};
        }
        return EF(std::in_place_type<FeasibilitySetMemEF>, thisFunc->Loc, otherFunc->ValueExpr);
    }

    // Mixing Elements => conservative
    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

bool FeasibilitySetMemEF::isConstant() const noexcept { return false; }

// --- factories ---
EF edgeIdentity() { return EF(std::in_place_type<FeasibilityIdentityEF>); }
EF edgeTop() { return EF(std::in_place_type<FeasibilityAllTopEF>); }
EF edgeBottom() { return EF(std::in_place_type<FeasibilityAllBottomEF>); }

} // namespace Feasibility
