/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityEdgeFunction.h"
#include "analyses/feasibility/util.h"
#include <chrono>

namespace Feasibility {



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

    // No JoinEF: widen
    return EF(std::in_place_type<FeasibilityAllTopEF>);
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
    if (source.isBottom()) {
        return source;
    }

    auto *S = source.getStore();
    l_t out = source;


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

EF FeasibilitySetSSAEF::compose(psr::EdgeFunctionRef<FeasibilitySetSSAEF> thisFunc,
                                const EF &secondFunction) {
    if (secondFunction.template isa<FeasibilityIdentityEF>() ||
        secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF{thisFunc};
    }

    if (secondFunction.template isa<FeasibilityAllTopEF>() ||
        llvm::isa<psr::AllTop<l_t>>(secondFunction)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    if (secondFunction.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(secondFunction)) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    if (secondFunction.template isa<FeasibilitySetSSAEF>()) {
        const auto &other = secondFunction.template cast<FeasibilitySetSSAEF>();
        if (thisFunc->Key == other->Key) {
            // later write wins
            return EF{secondFunction};
        }
    }

    // in FeasibilitySetSSAEF::compose(...)
    if (secondFunction.template isa<FeasibilityAssumeIcmpEF>()) {
        const auto &A = secondFunction.template cast<FeasibilityAssumeIcmpEF>();
        FeasibilityPackedEF R;
        R.pushBack(PackedOp::mkSetSSA(thisFunc->Loc, thisFunc->Key));
        R.pushBack(PackedOp::mkAssume(A->Cmp, A->TakeTrueEdge));
        return EF(std::in_place_type<FeasibilityPackedEF>, R);
    }

    if (secondFunction.template isa<FeasibilityPhiEF>()) {
        const auto &P = secondFunction.template cast<FeasibilityPhiEF>();
        FeasibilityPackedEF R;
        R.pushBack(PackedOp::mkSetSSA(thisFunc->Loc, thisFunc->Key));
        R.pushBack(PackedOp::mkPhi(P->phinode, P->incomingVal));
        return EF(std::in_place_type<FeasibilityPackedEF>, R);
    }

    if (secondFunction.template isa<FeasibilityPackedEF>()) {
        const auto &P2 = secondFunction.template cast<FeasibilityPackedEF>();
        FeasibilityPackedEF R;
        R.pushBack(PackedOp::mkSetSSA(thisFunc->Loc, thisFunc->Key));
        for (unsigned i = 0; i < P2->N; ++i)
            if (!R.pushBack(P2->Ops[i])) return EF(std::in_place_type<FeasibilityAllTopEF>);
        return EF(std::in_place_type<FeasibilityPackedEF>, R);
    }

    // otherwise: if you truly can't represent it, THEN AllTop is safe
    return EF(std::in_place_type<FeasibilityAllTopEF>);
}


EF FeasibilitySetSSAEF::join(psr::EdgeFunctionRef<FeasibilitySetSSAEF> thisFunc, const psr::EdgeFunction<l_t> &other) {
    if (other.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(other) ||
        other.template isa<FeasibilityIdentityEF>() ||
        other.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(thisFunc);
    }

    if (other.template isa<FeasibilityAllTopEF>() || llvm::isa<psr::AllTop<l_t>>(other)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    if (other.template isa<FeasibilitySetSSAEF>()) {
        const auto &o = other.template cast<FeasibilitySetSSAEF>();
        if (*thisFunc == *o) {
            return EF{thisFunc};
        }
    }

    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

bool FeasibilitySetSSAEF::isConstant() const noexcept {
    return false;
}

// ========================= FeasibilitySetMemEF =================================

l_t FeasibilitySetMemEF::computeTarget(const l_t &source) const {
    if (source.isBottom()) {
        return source;
    }
    l_t out = source;

    out.memId = source.getStore()->Mem.set(out.memId, Loc, ValueId);
    return out;
}

EF FeasibilitySetMemEF::compose(psr::EdgeFunctionRef<FeasibilitySetMemEF> thisFunc,
                                const EF &secondFunction) {
  // Keep existing fast paths if you want:
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

  // Optional: collapse consecutive SetMem on same location (later wins)
  if (secondFunction.template isa<FeasibilitySetMemEF>()) {
    const auto &other = secondFunction.template cast<FeasibilitySetMemEF>();
    if (thisFunc->Loc == other->Loc) {
      return EF{secondFunction}; // later write wins
    }
  }

  // --- Packing cases (analog to SetSSAEF::compose) ---

  // SetMem ∘ Assume => Packed[SetMem, Assume]
  if (secondFunction.template isa<FeasibilityAssumeIcmpEF>()) {
    const auto &A = secondFunction.template cast<FeasibilityAssumeIcmpEF>();
    FeasibilityPackedEF R;
    R.pushBack(PackedOp::mkSetMem(thisFunc->Loc, thisFunc->ValueId));
    R.pushBack(PackedOp::mkAssume(A->Cmp, A->TakeTrueEdge));
    return EF(std::in_place_type<FeasibilityPackedEF>, R);
  }

  // SetMem ∘ Phi => Packed[SetMem, Phi]
  if (secondFunction.template isa<FeasibilityPhiEF>()) {
    const auto &P = secondFunction.template cast<FeasibilityPhiEF>();
    FeasibilityPackedEF R;
    R.pushBack(PackedOp::mkSetMem(thisFunc->Loc, thisFunc->ValueId));
    R.pushBack(PackedOp::mkPhi(P->phinode, P->incomingVal));
    return EF(std::in_place_type<FeasibilityPackedEF>, R);
  }

  // SetMem ∘ Packed => prepend SetMem
  if (secondFunction.template isa<FeasibilityPackedEF>()) {
    const auto &P2 = secondFunction.template cast<FeasibilityPackedEF>();
    FeasibilityPackedEF R;
    R.pushBack(PackedOp::mkSetMem(thisFunc->Loc, thisFunc->ValueId));
    for (unsigned i = 0; i < P2->N; ++i) {
      if (!R.pushBack(P2->Ops[i])) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
      }
    }
    return EF(std::in_place_type<FeasibilityPackedEF>, R);
  }

  // If you can't represent the composition precisely, be safe:
  return EF(std::in_place_type<FeasibilityAllTopEF>);
}

EF FeasibilitySetMemEF::join(psr::EdgeFunctionRef<FeasibilitySetMemEF> thisFunc,
                             const psr::EdgeFunction<l_t> &other) {
    if (other.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(other) ||
        other.template isa<FeasibilityIdentityEF>() ||
        other.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(thisFunc);
    }

    if (other.template isa<FeasibilityAllTopEF>() || llvm::isa<psr::AllTop<l_t>>(other)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    if (other.template isa<FeasibilitySetMemEF>()) {
        const auto &o = other.template cast<FeasibilitySetMemEF>();
        if (*thisFunc == *o) return EF{thisFunc};
    }

    return EF(std::in_place_type<FeasibilityAllTopEF>);
}


bool FeasibilitySetMemEF::isConstant() const noexcept {
    return false;
}

l_t FeasibilityAssumeIcmpEF::computeTarget(const l_t &source) const {
    auto *S = source.getStore();

    // ⊥ = killed/unreachable: must stay ⊥
    if (source.isBottom()) {
        return source;
    }

    if (!Cmp || !S) {
        return source;
    }

    llvm::errs() << "FeasibilityAssumeIcmpEF: computing target for ICMP " << *Cmp
                 << " with source pcId=" << source.pcId
                 << " ssaId=" << source.ssaId
                 << " memId=" << source.memId
                 << "\n";

    // Resolve operands of ICMP to their symbolic expression ids while source is valid
    auto Lid = Feasibility::Util::resolveId(Cmp->getOperand(0), source, S);
    auto Rid = Feasibility::Util::resolveId(Cmp->getOperand(1), source, S);

    if (!Lid || !Rid) {
        llvm::errs() << "AssumeIcmp, computeTarget, lookup failed:"
        << "\n  Operand0: " << *Cmp->getOperand(0) << " -> " << (Lid ? std::to_string(*Lid) : "<none>") << "\n"
        << "  Operand1: " << *Cmp->getOperand(1) << " -> " << (Rid ? std::to_string(*Rid) : "<none>") << "\n";

        return source;
    }

    // ICMP condition is a boolean condition on the path, so we can cache it for future reuse
    FeasibilityStateStore::CmpCondKey ck{source.ssaId, source.memId, Cmp, TakeTrueEdge};
    if (auto it = S->CmpCondCache.find(ck); it != S->CmpCondCache.end()) {
        // already have boolean condition expr id
        const z3::expr &Cond = S->exprOf(it->second);

        if (source.pcId == S->PcTrueId && Cond.is_true()) return source;

        l_t out = source.assume(Cond);
        return out;
    }

    // Query the actual expressions from the calculated expression ids valid during source
    const z3::expr &L = S->exprOf(*Lid);
    const z3::expr &R = S->exprOf(*Rid);

    // Type sanity
    if (!L.is_bv() || !R.is_bv() || L.get_sort().bv_size() != R.get_sort().bv_size()) {
        return source;
    }

    // cheap syntactic fast paths
    if (*Lid == *Rid) {
        if (Cmp->getPredicate() == llvm::CmpInst::ICMP_EQ) {
            return source;
        }
        if (Cmp->getPredicate() == llvm::CmpInst::ICMP_NE) {
            return l_t::bottom(S);
        }
    }

    z3::expr Cond = S->ctx().bool_val(true);
    switch (Cmp->getPredicate()) {
        case llvm::CmpInst::ICMP_EQ:  Cond = (L == R); break;
        case llvm::CmpInst::ICMP_NE:  Cond = (L != R); break;

        case llvm::CmpInst::ICMP_ULT: Cond = z3::ult(L, R); break;
        case llvm::CmpInst::ICMP_ULE: Cond = z3::ule(L, R); break;
        case llvm::CmpInst::ICMP_UGT: Cond = z3::ugt(L, R); break;
        case llvm::CmpInst::ICMP_UGE: Cond = z3::uge(L, R); break;

        case llvm::CmpInst::ICMP_SLT: Cond = z3::slt(L, R); break;
        case llvm::CmpInst::ICMP_SLE: Cond = z3::sle(L, R); break;
        case llvm::CmpInst::ICMP_SGT: Cond = z3::sgt(L, R); break;
        case llvm::CmpInst::ICMP_SGE: Cond = z3::sge(L, R); break;

        default: {
            return source;
        }
    }

    if (!TakeTrueEdge) {
        Cond = !Cond;
    }

    if (Cond.is_false()) {
        return l_t::bottom(S);
    }

    if (Cond.is_true()) {
        return source;
    }

    auto cid = S->internExpr(Cond);
    S->CmpCondCache.emplace(ck, cid);
    l_t out = source.assume(S->exprOf(cid));

    auto resultingPC = S->getPathConstraint(out.pcId);
    llvm::errs() << "Resulting pc from assume: " << resultingPC.to_string() << "\n";

    return out;
}



EF FeasibilityAssumeIcmpEF::compose(psr::EdgeFunctionRef<FeasibilityAssumeIcmpEF> self,
                                    const EF &second) {
    if (second.template isa<FeasibilityIdentityEF>() ||
        second.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(self);
        }
    if (second.template isa<FeasibilityAllTopEF>() || llvm::isa<psr::AllTop<l_t>>(second)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }
    if (second.template isa<FeasibilityAllBottomEF>() || llvm::isa<psr::AllBottom<l_t>>(second)) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    // Assume ∘ Packed => prepend Assume
    if (second.template isa<FeasibilityPackedEF>()) {
        const auto &P2 = second.template cast<FeasibilityPackedEF>();
        FeasibilityPackedEF R;
        R.pushBack(PackedOp::mkAssume(self->Cmp, self->TakeTrueEdge));
        for (unsigned i = 0; i < P2->N; ++i) {
            if (!R.pushBack(P2->Ops[i])) return EF(std::in_place_type<FeasibilityAllTopEF>);
        }
        return EF(std::in_place_type<FeasibilityPackedEF>, R);
    }

    // Assume ∘ Phi => Packed[Assume, Phi]
    if (second.template isa<FeasibilityPhiEF>()) {
        const auto &P = second.template cast<FeasibilityPhiEF>();
        FeasibilityPackedEF R;
        R.pushBack(PackedOp::mkAssume(self->Cmp, self->TakeTrueEdge));
        R.pushBack(PackedOp::mkPhi(P->phinode, P->incomingVal));
        return EF(std::in_place_type<FeasibilityPackedEF>, R);
    }

    // Assume ∘ Assume => Packed[Assume, Assume] (keeps both constraints!)
    if (second.template isa<FeasibilityAssumeIcmpEF>()) {
        const auto &A = second.template cast<FeasibilityAssumeIcmpEF>();
        FeasibilityPackedEF R;
        R.pushBack(PackedOp::mkAssume(self->Cmp, self->TakeTrueEdge));
        R.pushBack(PackedOp::mkAssume(A->Cmp, A->TakeTrueEdge));
        return EF(std::in_place_type<FeasibilityPackedEF>, R);
    }

    // Assume ∘ SetSSA => Packed[Assume, SetSSA]
    if (second.template isa<FeasibilitySetSSAEF>()) {
        const auto &S = second.template cast<FeasibilitySetSSAEF>();
        FeasibilityPackedEF R;
        R.pushBack(PackedOp::mkAssume(self->Cmp, self->TakeTrueEdge));
        R.pushBack(PackedOp::mkSetSSA(S->Loc, S->Key));
        return EF(std::in_place_type<FeasibilityPackedEF>, R);
    }

    // Assume ∘ SetMem => Packed[Assume, SetMem]
    if (second.template isa<FeasibilitySetMemEF>()) {
        const auto &M = second.template cast<FeasibilitySetMemEF>();
        FeasibilityPackedEF R;
        R.pushBack(PackedOp::mkAssume(self->Cmp, self->TakeTrueEdge));
        R.pushBack(PackedOp::mkSetMem(M->Loc, M->ValueId));
        return EF(std::in_place_type<FeasibilityPackedEF>, R);
    }

    return EF(std::in_place_type<FeasibilityAllTopEF>);
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
        if (*self == *O) return EF(self);
    }

    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

l_t FeasibilityPhiEF::computeTarget(const l_t &source) const {
    auto *S = source.getStore();
    l_t out = source;
    if (out.isBottom() || !phinode || !incomingVal) return out;

    unsigned bw = 32;
    if (phinode->getType()->isIntegerTy())
        bw = phinode->getType()->getIntegerBitWidth();

    // Stable across all preds:
    auto PhiSymId = S->getOrCreateSym(phinode, bw, "phi");

    // Resolve incoming under current SSA/mem:
    auto InId = Util::resolveId(incomingVal, out, S);
    if (!InId) return out;

    // SSA: PHI result always maps to the same symbol
    out.ssaId = S->Ssa.set(out.ssaId, phinode, PhiSymId);

    // PC: edge-specific link
    const z3::expr &PhiSym = S->exprOf(PhiSymId);
    const z3::expr &InExpr = S->exprOf(*InId);
    out = out.assume(PhiSym == InExpr);

    llvm::errs() << "AssumeIcmp: out pcId=" << out.pcId
                 << " ssaId=" << out.ssaId
                 << " memId=" << out.memId << "\n";
    llvm::errs() << "AssumeIcmp: pc = " << S->getPathConstraint(out.pcId).to_string() << "\n";

    return out;
}

EF FeasibilityPhiEF::compose(psr::EdgeFunctionRef<FeasibilityPhiEF> self,
                             const EF &second) {
    if (second.template isa<FeasibilityIdentityEF>() ||
        second.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(self);
        }
    if (second.template isa<FeasibilityAllTopEF>() || llvm::isa<psr::AllTop<l_t>>(second)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }
    if (second.template isa<FeasibilityAllBottomEF>() || llvm::isa<psr::AllBottom<l_t>>(second)) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    // Phi ∘ Packed => prepend Phi
    if (second.template isa<FeasibilityPackedEF>()) {
        const auto &P2 = second.template cast<FeasibilityPackedEF>();
        FeasibilityPackedEF R;
        // prepend Phi then copy packed
        R.pushBack(PackedOp::mkPhi(self->phinode, self->incomingVal));
        for (unsigned i = 0; i < P2->N; ++i) {
            if (!R.pushBack(P2->Ops[i])) return EF(std::in_place_type<FeasibilityAllTopEF>);
        }
        return EF(std::in_place_type<FeasibilityPackedEF>, R);
    }

    // Phi ∘ Assume => Packed[Phi, Assume]
    if (second.template isa<FeasibilityAssumeIcmpEF>()) {
        const auto &A = second.template cast<FeasibilityAssumeIcmpEF>();
        FeasibilityPackedEF R;
        R.pushBack(PackedOp::mkPhi(self->phinode, self->incomingVal));
        R.pushBack(PackedOp::mkAssume(A->Cmp, A->TakeTrueEdge));
        return EF(std::in_place_type<FeasibilityPackedEF>, R);
    }

    // Phi ∘ Phi => Packed[Phi, Phi] (rare but legal)
    if (second.template isa<FeasibilityPhiEF>()) {
        const auto &P = second.template cast<FeasibilityPhiEF>();
        FeasibilityPackedEF R;
        R.pushBack(PackedOp::mkPhi(self->phinode, self->incomingVal));
        R.pushBack(PackedOp::mkPhi(P->phinode, P->incomingVal));
        return EF(std::in_place_type<FeasibilityPackedEF>, R);
    }

    if (second.template isa<FeasibilitySetSSAEF>()) {
        const auto &S = second.template cast<FeasibilitySetSSAEF>();
        FeasibilityPackedEF R;
        R.pushBack(PackedOp::mkPhi(self->phinode, self->incomingVal));
        R.pushBack(PackedOp::mkSetSSA(S->Loc, S->Key));
        return EF(std::in_place_type<FeasibilityPackedEF>, R);
    }

    // Phi ∘ SetMem => Packed[Phi, SetMem]
    if (second.template isa<FeasibilitySetMemEF>()) {
        const auto &M = second.template cast<FeasibilitySetMemEF>();
        FeasibilityPackedEF R;
        R.pushBack(PackedOp::mkPhi(self->phinode, self->incomingVal));
        R.pushBack(PackedOp::mkSetMem(M->Loc, M->ValueId));
        return EF(std::in_place_type<FeasibilityPackedEF>, R);
    }

    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

EF FeasibilityPhiEF::join(psr::EdgeFunctionRef<FeasibilityPhiEF> self, const psr::EdgeFunction<l_t> &other) {
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
    if (other.template isa<FeasibilityPhiEF>()) {
        const auto &O = other.template cast<FeasibilityPhiEF>();
        if (*self == *O) return EF(self);
    }

    return EF(std::in_place_type<FeasibilityAllTopEF>);
}


EF edgeIdentity() { return EF(std::in_place_type<FeasibilityIdentityEF>); }
EF edgeTop() { return EF(std::in_place_type<FeasibilityAllTopEF>); }
EF edgeBottom() { return EF(std::in_place_type<FeasibilityAllBottomEF>); }

} // namespace Feasibility
