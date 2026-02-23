/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityEdgeFunction.h"
#include "analyses/feasibility/util.h"

#include <atomic>
#include <chrono>

namespace Feasibility {

namespace {

// ============================================================================
// NEW SEMANTICS (critical):
//
//   l_t::Kind::Top    == UNREACHABLE  (no feasible path reaches here)
//   l_t::Kind::Normal == REACHABLE with info (pc/env tracked)
//   l_t::Kind::Bottom == REACHABLE baseline (pc/env reset; conservative)
//
// This matches PhASAR's IDE table initialization: default value is topElement(),
// therefore "not-yet-seen" == UNREACHABLE.
// ============================================================================

// ============================================================================
// Debug helpers (EF calculation trace)
// ============================================================================
// FDBG must be a compile-time constant for `if constexpr`.
// You can override via -DFDBG=1
#ifndef FDBG
#define FDBG 0
#endif

#define FDBG_LINE(MSG)                                                         \
  do {                                                                         \
    if constexpr (FDBG) {                                                      \
      llvm::errs() << "[FDBG] " << MSG << "\n";                                \
    }                                                                          \
  } while (0)

// ============================================================================
// Canonical constructors under NEW semantics
// ============================================================================

static inline l_t mkUnreached(const l_t &src) {
  // UNREACHABLE
  auto out = src;
  out.setKind(l_t::Kind::Top);
  out.setFormulaId(l_t::topId); // keep canonical
  out.setEnvId(0);
  return out;
}

static inline l_t mkReachedBaseline(const l_t &src) {
  // REACHABLE baseline (reset after joinCut)
  auto out = src;
  out.setKind(l_t::Kind::Bottom);
  out.setFormulaId(l_t::topId); // pc=true baseline (IMPORTANT)
  out.setEnvId(0);
  return out;
}

static inline l_t mkNormalLike(const l_t &src, uint32_t pc, uint32_t env) {
  // REACHABLE with tracked info
  auto out = src;
  out.setKind(l_t::Kind::Normal);
  out.setFormulaId(pc);
  out.setEnvId(env);
  return out;
}

// ============================================================================
// joinCutEF under NEW semantics:
//
// We store UNREACHABLE in the lattice.
// Merge rule for existence-of-feasible-path:
//
//   unreachable(out) = unreachable(lhs) AND unreachable(rhs)
//
// We cut the EF algebra at merges:
//   - if BOTH incoming EFs are provably constant-unreachable => keep unreachable
//   - else => reset to reachable baseline (forget old pc/env; keep future pruning)
// ============================================================================
static inline EF joinCutEF(const EF &lhs, const EF &rhs) {
  const bool LUnreach = isAllTopEF(lhs); // constant UNREACHABLE
  const bool RUnreach = isAllTopEF(rhs);

  if (LUnreach && RUnreach) {
    return EF(std::in_place_type<FeasibilityAllTopEF>); // UNREACHABLE
  }

  return EF(std::in_place_type<FeasibilityAllBottomEF>); // REACHABLE baseline
}

} // namespace

// =====================================================================================================================
// FeasibilityAllTopEF
//
// Constant UNREACHABLE edge function.
// =====================================================================================================================

l_t FeasibilityAllTopEF::computeTarget(const l_t &source) const {
  if constexpr (FDBG)
    dumpLatticeBrief("AllTop.in ", source);

  auto out = mkUnreached(source);

  if constexpr (FDBG)
    dumpLatticeBrief("AllTop.out", out);
  return out;
}

EF FeasibilityAllTopEF::compose(psr::EdgeFunctionRef<FeasibilityAllTopEF> /*thisFunc*/,
                                const EF &secondFunction) {
  // UNREACHABLE ∘ g = UNREACHABLE (absorbing)
  if constexpr (FDBG) {
    llvm::errs() << "[FDBG] compose AllTopEF ∘ " << efName(secondFunction)
                 << " -> AllTopEF\n";
  }
  return EF(std::in_place_type<FeasibilityAllTopEF>);
}

EF FeasibilityAllTopEF::join(psr::EdgeFunctionRef<FeasibilityAllTopEF> thisFunc,
                             const psr::EdgeFunction<l_t> &otherFunc) {
  const EF A(thisFunc);
  const EF B(otherFunc);
  EF out = joinCutEF(A, B);

  if constexpr (FDBG) {
    llvm::errs() << "[FDBG] join    AllTopEF ⊔ " << efName(B) << " -> "
                 << efName(out) << "\n";
  }
  return out;
}

// =====================================================================================================================
// FeasibilityAllBottomEF
//
// Constant REACHABLE-baseline edge function (reset pc/env).
// =====================================================================================================================

l_t FeasibilityAllBottomEF::computeTarget(const l_t &source) const {
  if constexpr (FDBG)
    dumpLatticeBrief("AllBottom.in ", source);

  // If already UNREACHABLE, keep it (UNREACHABLE dominates reachability).
  if (source.isTop()) {
    auto out = mkUnreached(source);
    if constexpr (FDBG)
      dumpLatticeBrief("AllBottom.out", out);
    return out;
  }

  auto out = mkReachedBaseline(source);

  if constexpr (FDBG)
    dumpLatticeBrief("AllBottom.out", out);
  return out;
}

EF FeasibilityAllBottomEF::compose(psr::EdgeFunctionRef<FeasibilityAllBottomEF> /*thisFunc*/,
                                   const EF &secondFunction) {
  // REACHABLE-baseline ∘ g = REACHABLE-baseline (constant reset)
  if constexpr (FDBG) {
    llvm::errs() << "[FDBG] compose AllBottomEF ∘ " << efName(secondFunction)
                 << " -> AllBottomEF\n";
  }
  return EF(std::in_place_type<FeasibilityAllBottomEF>);
}

EF FeasibilityAllBottomEF::join(psr::EdgeFunctionRef<FeasibilityAllBottomEF> thisFunc,
                                const psr::EdgeFunction<l_t> &otherFunc) {
  const EF A(thisFunc);
  const EF B(otherFunc);
  EF out = joinCutEF(A, B);

  if constexpr (FDBG) {
    llvm::errs() << "[FDBG] join    AllBottomEF ⊔ " << efName(B) << " -> "
                 << efName(out) << "\n";
  }
  return out;
}

// =====================================================================================================================
// FeasibilityPHITranslateEF
// =====================================================================================================================

l_t FeasibilityPHITranslateEF::computeTarget(const l_t &source) const {
  if constexpr (FDBG)
    dumpLatticeBrief("Phi.in ", source);

  FeasibilityElement cached;
  if (Memo.lookup(source, cached)) {
    if constexpr (FDBG)
      dumpLatticeBrief("Phi.memo", cached);
    return cached;
  }

  auto *M = pickManager(this->manager, source);

  // NEW: UNREACHABLE is absorbing (do not translate env)
  if (source.isTop()) {
    auto out = mkUnreached(source);
    Memo.store(source, out);
    if constexpr (FDBG)
      dumpLatticeBrief("Phi.out", out);
    return out;
  }

  if (!PredBB || !SuccBB) {
    Memo.store(source, source);
    if constexpr (FDBG)
      dumpLatticeBrief("Phi.out", source);
    return source;
  }

  const uint32_t incomingEnvId = source.getEnvId();
  const uint32_t outEnvId = M->applyPhiPack(incomingEnvId, PredBB, SuccBB);

  auto out = source;
  out.setEnvId(outEnvId);

  Memo.store(source, out);
  if constexpr (FDBG)
    dumpLatticeBrief("Phi.out", out);
  return out;
}

EF FeasibilityPHITranslateEF::compose(
    psr::EdgeFunctionRef<FeasibilityPHITranslateEF> thisFunc,
    const EF &secondFunction) {
  if constexpr (FDBG) {
    llvm::errs() << "[FDBG] compose PHITranslateEF ∘ " << efName(secondFunction)
                 << "\n";
  }

  if (isIdEF(secondFunction))
    return EF(thisFunc);

  // inner constant UNREACHABLE => UNREACHABLE
  if (isAllTopEF(secondFunction))
    return EF(std::in_place_type<FeasibilityAllTopEF>);

  auto *M = thisFunc->manager;
  const auto step = PhiStep(thisFunc->PredBB, thisFunc->SuccBB);

  if (auto *otherPhi =
          secondFunction.template dyn_cast<FeasibilityPHITranslateEF>()) {
    FeasibilityClause clause;
    clause.PhiChain.push_back(PhiStep(otherPhi->PredBB, otherPhi->SuccBB));
    clause.PhiChain.push_back(step);
    if constexpr (FDBG) {
      llvm::errs() << "[FDBG]  -> ANDFormulaEF (phi+phi): ";
      dumpClauseBrief(clause);
      llvm::errs() << "\n";
    }
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
  }

  if (auto *andEF =
          secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
    FeasibilityClause clause = andEF->Clause;
    clause.PhiChain.insert(clause.PhiChain.begin(), step);
    if constexpr (FDBG) {
      llvm::errs() << "[FDBG]  -> ANDFormulaEF (prepend phi): ";
      dumpClauseBrief(clause);
      llvm::errs() << "\n";
    }
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
  }

  if constexpr (FDBG) {
    llvm::errs() << "[FDBG]  -> ComposeEF (lazy)\n";
  }
  return EF(std::in_place_type<FeasibilityComposeEF>, M, EF(thisFunc),
            secondFunction);
}

EF FeasibilityPHITranslateEF::join(
    psr::EdgeFunctionRef<FeasibilityPHITranslateEF> thisFunc,
    const psr::EdgeFunction<l_t> &otherFunc) {
  const EF A(thisFunc);
  const EF B(otherFunc);
  EF out = joinCutEF(A, B);

  if constexpr (FDBG) {
    llvm::errs() << "[FDBG] join    PHITranslateEF ⊔ " << efName(B) << " -> "
                 << efName(out) << "\n";
  }
  return out;
}

// =====================================================================================================================
// FeasibilityANDFormulaEF
// =====================================================================================================================

l_t FeasibilityANDFormulaEF::computeTarget(const l_t &source) const {
  if constexpr (FDBG) {
    llvm::errs() << "[FDBG] AND.computeTarget clause: ";
    dumpClauseBrief(Clause);
    llvm::errs() << "\n";
    dumpLatticeBrief("AND.in  ", source);
  }

  FeasibilityElement cached;
  if (Memo.lookup(source, cached)) {
    if constexpr (FDBG)
      dumpLatticeBrief("AND.memo", cached);
    return cached;
  }

  auto *M = pickManager(manager, source);

  // NEW: UNREACHABLE is absorbing
  if (source.isTop()) {
    auto out = mkUnreached(source);
    Memo.store(source, out);
    if constexpr (FDBG)
      dumpLatticeBrief("AND.out ", out);
    return out;
  }

  const bool hasPhi = !Clause.PhiChain.empty();
  const bool hasConstr = !Clause.Constrs.empty();

  const uint32_t srcEnv = source.getEnvId();
  const uint32_t outEnv =
      hasPhi ? Util::applyPhiChain(M, srcEnv, Clause.PhiChain) : srcEnv;

  // env translation only: stay reachable (keep kind as-is)
  if (!hasConstr) {
    auto out = source;
    out.setEnvId(outEnv);
    Memo.store(source, out);
    if constexpr (FDBG)
      dumpLatticeBrief("AND.out ", out);
    return out;
  }

  uint32_t pc = source.getFormulaId();

  for (const auto &lazyConstr : Clause.Constrs) {
    if (!lazyConstr.I) {
      llvm::errs() << "ALARM in FeasibilityANDFormulaEF::computeTarget: LazyICmp "
                      "has null instruction\n";
      continue;
    }

    if constexpr (FDBG) {
      llvm::errs() << "[FDBG]   + " << (lazyConstr.TrueEdge ? "T" : "F")
                   << ": " << *lazyConstr.I << "\n";
    }

    z3::expr expr =
        Util::createConstraintFromICmp(M, lazyConstr.I, lazyConstr.TrueEdge,
                                       outEnv);
    const uint32_t cid = Util::findOrAddFormulaId(M, expr);

    pc = M->mkAnd(pc, cid);

    // NEW: UNSAT => UNREACHABLE (Top)
    if (!M->isSat(pc)) {
      auto out = mkUnreached(source);
      Memo.store(source, out);
      if constexpr (FDBG)
        dumpLatticeBrief("AND.out ", out);
      return out;
    }
  }

  // SAT => REACHABLE with info
  auto out = mkNormalLike(source, pc, outEnv);
  Memo.store(source, out);

  if constexpr (FDBG)
    dumpLatticeBrief("AND.out ", out);
  return out;
}

EF FeasibilityANDFormulaEF::compose(
    psr::EdgeFunctionRef<FeasibilityANDFormulaEF> thisFunc,
    const EF &secondFunction) {
  if constexpr (FDBG) {
    llvm::errs() << "[FDBG] compose ANDFormulaEF ∘ " << efName(secondFunction)
                 << "\n";
  }

  if (isIdEF(secondFunction))
    return EF(thisFunc);

  // inner constant UNREACHABLE => UNREACHABLE
  if (isAllTopEF(secondFunction))
    return EF(std::in_place_type<FeasibilityAllTopEF>);

  auto *M = thisFunc->manager;

  if (auto *phi =
          secondFunction.template dyn_cast<FeasibilityPHITranslateEF>()) {
    FeasibilityClause phiClause = Util::clauseFromPhi(phi->PredBB, phi->SuccBB);
    FeasibilityClause merged = Util::conjClauses(thisFunc->Clause, phiClause);
    if constexpr (FDBG) {
      llvm::errs() << "[FDBG]  -> ANDFormulaEF (conj with phi): ";
      dumpClauseBrief(merged);
      llvm::errs() << "\n";
    }
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(merged));
  }

  if (auto *and2 =
          secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
    FeasibilityClause merged = Util::conjClauses(thisFunc->Clause, and2->Clause);
    if constexpr (FDBG) {
      llvm::errs() << "[FDBG]  -> ANDFormulaEF (conj with AND): ";
      dumpClauseBrief(merged);
      llvm::errs() << "\n";
    }
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(merged));
  }

  if constexpr (FDBG) {
    llvm::errs() << "[FDBG]  -> ComposeEF (lazy)\n";
  }
  return EF(std::in_place_type<FeasibilityComposeEF>, M, EF(thisFunc),
            secondFunction);
}

EF FeasibilityANDFormulaEF::join(
    psr::EdgeFunctionRef<FeasibilityANDFormulaEF> thisFunc,
    const psr::EdgeFunction<l_t> &otherFunc) {
  const EF A(thisFunc);
  const EF B(otherFunc);
  EF out = joinCutEF(A, B);

  if constexpr (FDBG) {
    llvm::errs() << "[FDBG] join    ANDFormulaEF ⊔ " << efName(B) << " -> "
                 << efName(out) << "\n";
  }
  return out;
}

// ============================================================================
// FeasibilityComposeEF
// ============================================================================

l_t FeasibilityComposeEF::computeTarget(const l_t &source) const {
  if constexpr (FDBG)
    dumpLatticeBrief("Compose.in ", source);

  FeasibilityElement cached;
  if (Memo.lookup(source, cached)) {
    if constexpr (FDBG)
      dumpLatticeBrief("Compose.memo", cached);
    return cached;
  }

  // NEW: UNREACHABLE is absorbing
  if (source.isTop()) {
    auto out = mkUnreached(source);
    Memo.store(source, out);
    if constexpr (FDBG)
      dumpLatticeBrief("Compose.out", out);
    return out;
  }

  const l_t mid = Second.computeTarget(source);
  if constexpr (FDBG)
    dumpLatticeBrief("Compose.mid", mid);

  if (mid.isTop()) {
    Memo.store(source, mid);
    if constexpr (FDBG)
      dumpLatticeBrief("Compose.out", mid);
    return mid;
  }

  const l_t out = First.computeTarget(mid);
  Memo.store(source, out);

  if constexpr (FDBG)
    dumpLatticeBrief("Compose.out", out);
  return out;
}

EF FeasibilityComposeEF::compose(psr::EdgeFunctionRef<FeasibilityComposeEF> thisFunc,
                                 const EF &secondFunction) {
  if constexpr (FDBG) {
    llvm::errs() << "[FDBG] compose ComposeEF ∘ " << efName(secondFunction)
                 << "\n";
  }

  if (isIdEF(secondFunction))
    return EF(thisFunc);

  if (isAllTopEF(secondFunction))
    return EF(std::in_place_type<FeasibilityAllTopEF>);

  auto *M = thisFunc->manager;
  return EF(std::in_place_type<FeasibilityComposeEF>, M, EF(thisFunc),
            secondFunction);
}

EF FeasibilityComposeEF::join(psr::EdgeFunctionRef<FeasibilityComposeEF> thisFunc,
                              const psr::EdgeFunction<l_t> &otherFunc) {
  const EF A(thisFunc);
  const EF B(otherFunc);
  EF out = joinCutEF(A, B);

  if constexpr (FDBG) {
    llvm::errs() << "[FDBG] join    ComposeEF ⊔ " << efName(B) << " -> "
                 << efName(out) << "\n";
  }
  return out;
}

} // namespace Feasibility