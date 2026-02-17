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
  if (otherFunc.template isa<FeasibilityAllBottomEF>() ||
      llvm::isa<psr::AllBottom<l_t>>(otherFunc)) {
    return EF(thisFunc);
  }
  if (otherFunc.template isa<FeasibilityAllTopEF>() ||
      llvm::isa<psr::AllTop<l_t>>(otherFunc)) {
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }
  if (otherFunc.template isa<FeasibilityIdentityEF>() ||
      otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
    return EF(thisFunc);
  }
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

    if (F_DEBUG_ENABLED) {
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


// ========================= FeasibilitySetSSAEF =================================

l_t FeasibilitySetSSAEF::computeTarget(const l_t &source) const {
    if (source.isBottom() || source.isIdeAbsorbing()) {
        return source;
    }

    auto *S = source.getStore();
    l_t out = source;

    if (out.isTop() || out.isIdeNeutral()) {
        out = l_t::initial(S);
    }

    // 1) try read memory
    if (auto mv = S->Mem.getValue(out.memId, Loc)) {
        out.ssaId = S->Ssa.set(out.ssaId, Key, *mv);
        return out;
    }

    unsigned bw = 32;
    z3::expr sym = Feasibility::Util::mkSymBV(Loc, bw, "mem", &S->ctx());
    auto eid = S->internExpr(sym);

    out.memId = S->Mem.set(out.memId, Loc, eid);
    out.ssaId = S->Ssa.set(out.ssaId, Key, eid);
    return out;
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
    if (source.isBottom() || source.isIdeAbsorbing()) {
        return source;
    }
    l_t out = source;
    if (out.isTop() || out.isIdeNeutral()) {
        out = l_t::initial(source.getStore());
    }
    out.memId = source.getStore()->Mem.set(out.memId, Loc, ValueId);
    return out;
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
    // No "|| construction" here: keep a join node.
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
    // No "|| construction": keep structure.
    return makeJoin(EF(thisFunc), EF(otherFunc));
}

bool FeasibilityJoinEF::isConstant() const noexcept {
    return false;
}

l_t FeasibilityAssumeIcmpEF::computeTarget(const l_t &source) const {
    // ⊥ = killed/unreachable: must stay ⊥
    if (source.isBottom()) {
        return source;
    }

    // If you still have IdeAbsorbing around, treat it like ⊥ (old behavior).
    if (source.isIdeAbsorbing()) {
        return l_t::bottom(source.getStore());
    }

    if (!Cmp) {
        return source;
    }

    auto *S = source.getStore();
    if (!S) {
        return source;
    }

    // Resolve operands in the state of *source*
    auto L = Feasibility::Util::resolve(Cmp->getOperand(0), source, S);
    auto R = Feasibility::Util::resolve(Cmp->getOperand(1), source, S);

    // If we can't resolve the condition, we must not constrain anything.
    // IMPORTANT: do NOT mark infeasible, do NOT drop to ⊥.
    if (!L || !R) {
        return source;
    }

    z3::expr Cond = S->ctx().bool_val(true);
    switch (Cmp->getPredicate()) {
        case llvm::CmpInst::ICMP_EQ:  Cond = (*L == *R); break;
        case llvm::CmpInst::ICMP_NE:  Cond = (*L != *R); break;

        case llvm::CmpInst::ICMP_ULT: Cond = z3::ult(*L, *R); break;
        case llvm::CmpInst::ICMP_ULE: Cond = z3::ule(*L, *R); break;
        case llvm::CmpInst::ICMP_UGT: Cond = z3::ugt(*L, *R); break;
        case llvm::CmpInst::ICMP_UGE: Cond = z3::uge(*L, *R); break;

        case llvm::CmpInst::ICMP_SLT: Cond = z3::slt(*L, *R); break;
        case llvm::CmpInst::ICMP_SLE: Cond = z3::sle(*L, *R); break;
        case llvm::CmpInst::ICMP_SGT: Cond = z3::sgt(*L, *R); break;
        case llvm::CmpInst::ICMP_SGE: Cond = z3::sge(*L, *R); break;

        default: {
            return source; // unsupported predicate => no constraint
        }
    }

    // Apply edge polarity
    if (!TakeTrueEdge) {
        Cond = !Cond;
    }

    // Simplify: catch constant cases early
    //Cond = Cond;

    // Edge condition is definitely false => killed path (Bottom)
    if (Cond.is_false()) {
        return l_t::bottom(S);
    }

    // Definitely true => no constraint added
    if (Cond.is_true()) {
        return source;
    }

    // Add the constraint
    l_t out = source.assume(Cond);

    // Old behavior: UNSAT is represented as Bottom.
    if (out.isIdeAbsorbing()) {
        return l_t::bottom(S);
    }
    if (!out.isSatisfiable()) {
        return l_t::bottom(S);
    }

    return out;
}


EF FeasibilityAssumeIcmpEF::compose(psr::EdgeFunctionRef<FeasibilityAssumeIcmpEF> self,
                                    const EF &second) {
  if (second.template isa<FeasibilityIdentityEF>() ||
      second.template isa<psr::EdgeIdentity<l_t>>()) {
    return EF(self);
  }
  if (second.template isa<FeasibilityAllTopEF>()) {
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }
  if (second.template isa<FeasibilityAllBottomEF>()) {
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
  }
  // keep order: (assumeIcmp);(other)
  return EF(std::in_place_type<FeasibilitySeqEF>, EF(self), second);
}

EF FeasibilityAssumeIcmpEF::join(psr::EdgeFunctionRef<FeasibilityAssumeIcmpEF> self,
                                 const psr::EdgeFunction<l_t> &other) {
  if (other.template isa<FeasibilityAllBottomEF>() ||
      llvm::isa<psr::AllBottom<l_t>>(other) ||
      other.template isa<FeasibilityIdentityEF>() ||
      other.template isa<psr::EdgeIdentity<l_t>>()) {
    return EF(self);
  }
  if (other.template isa<FeasibilityAllTopEF>() ||
      llvm::isa<psr::AllTop<l_t>>(other)) {
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }
  if (other.template isa<FeasibilityAssumeIcmpEF>()) {
    const auto &O = other.template cast<FeasibilityAssumeIcmpEF>();
    if (*self == *O) {
      return EF(self);
    }
  }
  return EF(std::in_place_type<FeasibilityJoinEF>, EF(self), EF(other));
}



EF edgeIdentity() { return EF(std::in_place_type<FeasibilityIdentityEF>); }
EF edgeTop() { return EF(std::in_place_type<FeasibilityAllTopEF>); }
EF edgeBottom() { return EF(std::in_place_type<FeasibilityAllBottomEF>); }

} // namespace Feasibility
