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

    // No SeqEF: drop "thisFunc" and keep the later effect (sound over-approx)
    return secondFunction;
}


EF FeasibilitySetSSAEF::join(psr::EdgeFunctionRef<FeasibilitySetSSAEF> thisFunc,
                             const psr::EdgeFunction<l_t> &other) {
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
        const auto &o = other.template cast<FeasibilitySetSSAEF>();
        if (*thisFunc == *o) return EF{thisFunc};
    }

    // No JoinEF: widen
    return EF(std::in_place_type<FeasibilityAllTopEF>);
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

    if (second.template isa<FeasibilitySetMemEF>()) {
        const auto &other = second.template cast<FeasibilitySetMemEF>();
        if (self->Loc == other->Loc) {
            // later store wins
            return EF{second};
        }
    }

    // No SeqEF: drop earlier store and keep later effect
    return second;
}

EF FeasibilitySetMemEF::join(psr::EdgeFunctionRef<FeasibilitySetMemEF> thisFunc,
                             const psr::EdgeFunction<l_t> &other) {
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
        const auto &o = other.template cast<FeasibilitySetMemEF>();
        if (*thisFunc == *o) return EF{thisFunc};
    }

    // No JoinEF: widen
    return EF(std::in_place_type<FeasibilityAllTopEF>);
}


bool FeasibilitySetMemEF::isConstant() const noexcept {
    return false;
}

l_t FeasibilityAssumeIcmpEF::computeTarget(const l_t &source) const {
    auto *S = source.getStore();
    if (S) {
        S->metrics.edgeFunctionCount++;
    }

    // ---- Sampling setup ----
    // Sample every 1024th execution (tune with mask: 0x3FF -> 1024, 0xFF -> 256, etc.)
    constexpr uint64_t SampleMask = 0x3FF;          // 1024-1
    constexpr uint64_t SampleRate = SampleMask + 1; // 1024

    const uint64_t cnt = S ? S->metrics.edgeFunctionCount.load(std::memory_order_relaxed) : 0;
    const bool doSample = S && ((cnt & SampleMask) == 0);

    std::chrono::high_resolution_clock::time_point start;
    if (doSample) {
        start = std::chrono::high_resolution_clock::now();
    }

    // ⊥ = killed/unreachable: must stay ⊥
    if (source.isBottom()) {
        if (doSample) {
            auto end = std::chrono::high_resolution_clock::now();
            auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            S->metrics.totalEdgeFunctionTime += static_cast<uint64_t>(dur) * SampleRate;
        }
        return source;
    }

    // If you still have IdeAbsorbing around, treat it like ⊥ (old behavior).
    if (source.isIdeAbsorbing()) {
        if (doSample) {
            auto end = std::chrono::high_resolution_clock::now();
            auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            S->metrics.totalEdgeFunctionTime += static_cast<uint64_t>(dur) * SampleRate;
        }
        return l_t::bottom(source.getStore());
    }

    if (!Cmp || !S) {
        if (doSample) {
            auto end = std::chrono::high_resolution_clock::now();
            auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            S->metrics.totalEdgeFunctionTime += static_cast<uint64_t>(dur) * SampleRate;
        }
        return source;
    }

    // Resolve operands in the state of *source*
    auto Lid = Feasibility::Util::resolveId(Cmp->getOperand(0), source, S);
    auto Rid = Feasibility::Util::resolveId(Cmp->getOperand(1), source, S);
    if (!Lid || !Rid) {
        if (doSample) {
            auto end = std::chrono::high_resolution_clock::now();
            auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            S->metrics.totalEdgeFunctionTime += static_cast<uint64_t>(dur) * SampleRate;
        }
        return source;
    }

    FeasibilityStateStore::CmpCondKey ck{source.ssaId, source.memId, Cmp, TakeTrueEdge};
    if (auto it = S->CmpCondCache.find(ck); it != S->CmpCondCache.end()) {
        if (doSample) {
            auto end = std::chrono::high_resolution_clock::now();
            auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            S->metrics.totalEdgeFunctionTime += static_cast<uint64_t>(dur) * SampleRate;
        }

        // already have boolean condition expr id
        const z3::expr &Cond = S->exprOf(it->second);

        if (source.pcId == 0 && Cond.is_true()) return source;

        l_t out = source.assume(Cond);
        return out;
    }

    const z3::expr &L = S->exprOf(*Lid);
    const z3::expr &R = S->exprOf(*Rid);

    // Type sanity
    if (!L.is_bv() || !R.is_bv() || L.get_sort().bv_size() != R.get_sort().bv_size()) {
        if (doSample) {
            auto end = std::chrono::high_resolution_clock::now();
            auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            S->metrics.totalEdgeFunctionTime += static_cast<uint64_t>(dur) * SampleRate;
        }
        return source;
    }

    // cheap syntactic fast paths
    if (*Lid == *Rid) {
        if (Cmp->getPredicate() == llvm::CmpInst::ICMP_EQ) {
            if (doSample) {
                auto end = std::chrono::high_resolution_clock::now();
                auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                S->metrics.totalEdgeFunctionTime += static_cast<uint64_t>(dur) * SampleRate;
            }
            return source;
        }
        if (Cmp->getPredicate() == llvm::CmpInst::ICMP_NE) {
            if (doSample) {
                auto end = std::chrono::high_resolution_clock::now();
                auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                S->metrics.totalEdgeFunctionTime += static_cast<uint64_t>(dur) * SampleRate;
            }
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
            if (doSample) {
                auto end = std::chrono::high_resolution_clock::now();
                auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                S->metrics.totalEdgeFunctionTime += static_cast<uint64_t>(dur) * SampleRate;
            }
            return source;
        }
    }

    if (!TakeTrueEdge) {
        Cond = !Cond;
    }

    if (Cond.is_false()) {
        if (doSample) {
            auto end = std::chrono::high_resolution_clock::now();
            auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            S->metrics.totalEdgeFunctionTime += static_cast<uint64_t>(dur) * SampleRate;
        }
        return l_t::bottom(S);
    }

    if (Cond.is_true()) {
        if (doSample) {
            auto end = std::chrono::high_resolution_clock::now();
            auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            S->metrics.totalEdgeFunctionTime += static_cast<uint64_t>(dur) * SampleRate;
        }
        return source;
    }

    auto cid = S->internExpr(Cond);
    S->CmpCondCache.emplace(ck, cid);
    l_t out = source.assume(S->exprOf(cid));

    if (doSample) {
        auto end = std::chrono::high_resolution_clock::now();
        auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        S->metrics.totalEdgeFunctionTime += static_cast<uint64_t>(dur) * SampleRate;
    }

    return out;
}



EF FeasibilityAssumeIcmpEF::compose(psr::EdgeFunctionRef<FeasibilityAssumeIcmpEF> self,
                                    const EF &second) {
    if (second.template isa<FeasibilityIdentityEF>() ||
        second.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(self);
        }
    if (second.template isa<FeasibilityAllTopEF>() ||
        llvm::isa<psr::AllTop<l_t>>(second)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
        }
    if (second.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(second)) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
        }

    // No SeqEF: drop the assume (sound, but loses pruning across composed effects)
    return second;
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

    // No JoinEF: widen
    return EF(std::in_place_type<FeasibilityAllTopEF>);
}



EF edgeIdentity() { return EF(std::in_place_type<FeasibilityIdentityEF>); }
EF edgeTop() { return EF(std::in_place_type<FeasibilityAllTopEF>); }
EF edgeBottom() { return EF(std::in_place_type<FeasibilityAllBottomEF>); }

} // namespace Feasibility
