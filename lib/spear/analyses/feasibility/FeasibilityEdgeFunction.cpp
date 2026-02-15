/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityEdgeFunction.h"
#include "analyses/feasibility/util.h"

namespace Feasibility {

namespace {

[[nodiscard]] EF makeSeq(const EF &first, const EF &second) {
    if (first.template isa<FeasibilitySeqEF>()) {
        const auto &f = first.template cast<FeasibilitySeqEF>();
        // (a;b);c  =>  a;(b;c)
        return EF(std::in_place_type<FeasibilitySeqEF>, f->First, makeSeq(f->Second, second));
    }
    if (second.template isa<FeasibilityIdentityEF>() ||
        second.template isa<psr::EdgeIdentity<l_t>>()) {
        return first;
        }
    return EF(std::in_place_type<FeasibilitySeqEF>, first, second);
}

[[nodiscard]] EF makeJoin(const EF &left, const EF &right) {
    return EF(std::in_place_type<FeasibilityJoinEF>, left, right);
}

} // namespace


// ========================= FeasibilityIdentityEF =================================

l_t FeasibilityIdentityEF::computeTarget(const l_t &source) const {
    return source;
}

EF FeasibilityIdentityEF::compose(psr::EdgeFunctionRef<FeasibilityIdentityEF>,
                                  const EF &secondFunction) {
    return secondFunction;
}

EF FeasibilityIdentityEF::join(psr::EdgeFunctionRef<FeasibilityIdentityEF> thisFunc,
                               const psr::EdgeFunction<l_t> &otherFunc) {
  // Id ⊔ ⊥ = Id   (since x ⊔ ⊥ = x)
  if (otherFunc.template isa<FeasibilityAllBottomEF>() ||
      llvm::isa<psr::AllBottom<l_t>>(otherFunc)) {
    return EF(thisFunc);
  }

  // Id ⊔ Id = Id
  if (otherFunc.template isa<FeasibilityIdentityEF>() ||
      otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
    return EF(thisFunc);
  }

  // Id ⊔ AllTop = AllTop   (since x ⊔ ⊤ = ⊤)
  if (otherFunc.template isa<FeasibilityAllTopEF>() ||
      llvm::isa<psr::AllTop<l_t>>(otherFunc)) {
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }

  // Otherwise, keep the more precise representation (join node).
  return makeJoin(EF(thisFunc), EF(otherFunc));
}

bool FeasibilityIdentityEF::isConstant() const noexcept {
    return false;
}


// ========================= FeasibilityAllTopEF =================================

l_t FeasibilityAllTopEF::computeTarget(const l_t &source) const {
    if (source.isBottom()) {
        return source;
    }
    return l_t::top(source.getStore());
}

EF FeasibilityAllTopEF::compose(psr::EdgeFunctionRef<FeasibilityAllTopEF> thisFunc, const EF &second) {

    if (Feasibility::Util::F_DebugEnabled.load()) {
        llvm::errs() << "[FDBG] AllTop::compose second=";
        Feasibility::Util::dumpEF(second);
        llvm::errs() << " kind=";
        Feasibility::Util::dumpEFKind(second);
        llvm::errs() << "\n";
    }

    if (second.template isa<FeasibilityIdentityEF>() || second.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
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
        return EF{second};
    }

    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

EF FeasibilityAllTopEF::join(psr::EdgeFunctionRef<FeasibilityAllTopEF> thisFunc, const psr::EdgeFunction<l_t> &) {
    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

bool FeasibilityAllTopEF::isConstant() const noexcept {
    return false;
}


// ========================= FeasibilityAllBottomEF =================================

l_t FeasibilityAllBottomEF::computeTarget(const l_t &source) const {
    if (source.isBottom()) {
        return source;
    }
    return l_t::bottom(source.getStore());
}

EF FeasibilityAllBottomEF::compose(psr::EdgeFunctionRef<FeasibilityAllBottomEF> thisFunc, const EF &secondFunction) {
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
}

EF FeasibilityAllBottomEF::join(psr::EdgeFunctionRef<FeasibilityAllBottomEF>, const psr::EdgeFunction<l_t> &otherFunc) {
    return otherFunc;
}

bool FeasibilityAllBottomEF::isConstant() const noexcept {
    return true;
}

// ========================= FeasibilityAssumeEF =================================

l_t FeasibilityAssumeEF::computeTarget(const l_t &source) const {
    if (source.isBottom()) {
        return source;
    }

    if (source.isTop()) {
        return source;
    }

    return source.assume(Cond);
}

EF FeasibilityAssumeEF::compose(psr::EdgeFunctionRef<FeasibilityAssumeEF> thisFunc, const EF &secondFunction) {

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
        z3::expr composed = (thisFunc->Cond && other->Cond).simplify();
        return EF(std::in_place_type<FeasibilityAssumeEF>, composed);
    }

    return makeSeq(EF(thisFunc), secondFunction);
}

EF FeasibilityAssumeEF::join(psr::EdgeFunctionRef<FeasibilityAssumeEF> thisFunc, const psr::EdgeFunction<l_t> &other) {
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
        return makeJoin(EF(thisFunc), EF(other));
    }

    if (other.template isa<FeasibilityAssumeEF>()) {
        const auto &otherFunc = other.template cast<FeasibilityAssumeEF>();
        if (z3::eq(thisFunc->Cond, otherFunc->Cond)) {
            return EF(thisFunc);
        }
        return makeJoin(EF(thisFunc), EF(other));
    }

    return makeJoin(EF(thisFunc), EF(other));
}

bool FeasibilityAssumeEF::operator==(const FeasibilityAssumeEF &Other) const {
    return z3::eq(Cond, Other.Cond);
}

bool FeasibilityAssumeEF::isConstant() const noexcept {
    return false;
}


// ========================= FeasibilitySetSSAEF =================================

l_t FeasibilitySetSSAEF::computeTarget(const l_t &source) const {
    if (source.isBottom()) {
        return source;
    }

    if (source.isTop()) {
        return source;
    }

    return source;
}

EF FeasibilitySetSSAEF::compose(psr::EdgeFunctionRef<FeasibilitySetSSAEF> thisFunc, const EF &secondFunction) {
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
        if (thisFunc->Key == other->Key) {
            return EF{secondFunction};
        }

        return makeSeq(EF(thisFunc), secondFunction);
    }

    return makeSeq(EF(thisFunc), secondFunction);
}

EF FeasibilitySetSSAEF::join(psr::EdgeFunctionRef<FeasibilitySetSSAEF> thisFunc, const psr::EdgeFunction<l_t> &other) {
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

    if (other.template isa<FeasibilitySetMemEF>()) {
        return makeJoin(EF(thisFunc), EF(other));
    }

    if (other.template isa<FeasibilityAssumeEF>()) {
        return makeJoin(EF(thisFunc), EF(other));
    }

    if (other.template isa<FeasibilitySetSSAEF>()) {
        const auto &o = other.template cast<FeasibilitySetSSAEF>();
        if (*thisFunc == *o) {
            return EF{thisFunc};
        }
        return makeJoin(EF(thisFunc), EF(other));
    }

    return makeJoin(EF(thisFunc), EF(other));
}

bool FeasibilitySetSSAEF::isConstant() const noexcept {
    return false;
}

// ========================= FeasibilitySetMemEF =================================

l_t FeasibilitySetMemEF::computeTarget(const l_t &source) const {
    if (source.isBottom()) {
        return source;
    }

    if (source.isTop()) {
        return source;
    }

    return source;
}

EF FeasibilitySetMemEF::compose(psr::EdgeFunctionRef<FeasibilitySetMemEF> self,
                                const EF &second) {
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
        return makeSeq(EF(self), second);
    }

    return makeSeq(EF(self), second);
}

EF FeasibilitySetMemEF::join(psr::EdgeFunctionRef<FeasibilitySetMemEF> thisFunc, const psr::EdgeFunction<l_t> &other) {
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

    if (other.template isa<FeasibilityAssumeEF>()) {
        return makeJoin(EF(thisFunc), EF(other));
    }

    if (other.template isa<FeasibilitySetSSAEF>()) {
        return makeJoin(EF(thisFunc), EF(other));
    }

    if (other.template isa<FeasibilitySetMemEF>()) {
        const auto &o = other.template cast<FeasibilitySetMemEF>();
        if (*thisFunc == *o) {
            return EF{thisFunc};
        }
        return makeJoin(EF(thisFunc), EF(other));
    }

    return makeJoin(EF(thisFunc), EF(other));
}

bool FeasibilitySetMemEF::isConstant() const noexcept {
    return false;
}

// ========================= FeasibilitySeqEF =================================

l_t FeasibilitySeqEF::computeTarget(const l_t &source) const {
    // Second(First(source))
    l_t mid = First.computeTarget(source);
    return Second.computeTarget(mid);
}

EF FeasibilitySeqEF::compose(psr::EdgeFunctionRef<FeasibilitySeqEF> thisFunc, const EF &secondFunction) {
    if (secondFunction.template isa<FeasibilityIdentityEF>() ||
        secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(thisFunc);
    }

    if (secondFunction.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(secondFunction)) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    if (secondFunction.template isa<FeasibilityAllTopEF>() ||
        llvm::isa<psr::AllTop<l_t>>(secondFunction)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    return makeSeq(EF(thisFunc), secondFunction);
}

EF FeasibilitySeqEF::join(psr::EdgeFunctionRef<FeasibilitySeqEF> thisFunc, const psr::EdgeFunction<l_t> &otherFunc) {
    if (otherFunc.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(otherFunc)) {
        return EF(thisFunc);
    }

    if (otherFunc.template isa<FeasibilityAllTopEF>() ||
        llvm::isa<psr::AllTop<l_t>>(otherFunc)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    return makeJoin(EF(thisFunc), EF(otherFunc));
}

bool FeasibilitySeqEF::isConstant() const noexcept {
    return false;
}


// ========================= FeasibilityJoinEF =================================

l_t FeasibilityJoinEF::computeTarget(const l_t &source) const {
    l_t a = Left.computeTarget(source);
    l_t b = Right.computeTarget(source);
    return a.join(b);
}

EF FeasibilityJoinEF::compose(psr::EdgeFunctionRef<FeasibilityJoinEF> thisFunc, const EF &secondFunction) {
    if (secondFunction.template isa<FeasibilityIdentityEF>() ||
        secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(thisFunc);
    }

    if (secondFunction.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(secondFunction)) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    if (secondFunction.template isa<FeasibilityAllTopEF>() ||
        llvm::isa<psr::AllTop<l_t>>(secondFunction)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    EF leftComposed  = EF::compose(thisFunc->Left, secondFunction);
    EF rightComposed = EF::compose(thisFunc->Right, secondFunction);

    return makeJoin(leftComposed, rightComposed);
}

EF FeasibilityJoinEF::join(psr::EdgeFunctionRef<FeasibilityJoinEF> thisFunc, const psr::EdgeFunction<l_t> &otherFunc) {
    if (otherFunc.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(otherFunc)) {
        return EF(thisFunc);
    }

    if (otherFunc.template isa<FeasibilityAllTopEF>() ||
        llvm::isa<psr::AllTop<l_t>>(otherFunc)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    return makeJoin(EF(thisFunc), EF(otherFunc));
}

bool FeasibilityJoinEF::isConstant() const noexcept {
    return false;
}


EF edgeIdentity() { return EF(std::in_place_type<FeasibilityIdentityEF>); }
EF edgeTop() { return EF(std::in_place_type<FeasibilityAllTopEF>); }
EF edgeBottom() { return EF(std::in_place_type<FeasibilityAllBottomEF>); }

} // namespace Feasibility
