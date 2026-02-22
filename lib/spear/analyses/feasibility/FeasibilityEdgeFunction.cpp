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
// Debug + timing
// ============================================================================
constexpr bool FDBG = true;             // flip to false to disable
constexpr uint64_t FDBG_EVERY = 10000;   // periodic heartbeat
constexpr double FDBG_SLOW_MS = 50.0;    // warn if a call exceeds this

static std::atomic<uint64_t> g_dbg_seq{0};

struct ScopedTimer {
  const char *Tag;
  uint64_t Seq;
  std::chrono::steady_clock::time_point Start;

  ScopedTimer(const char *T)
      : Tag(T), Seq(++g_dbg_seq), Start(std::chrono::steady_clock::now()) {
    if constexpr (FDBG) {
      if (Seq % FDBG_EVERY == 0) {
        llvm::errs() << "[FDBG] #" << Seq << " ENTER " << Tag << "\n";
      }
    }
  }

  ~ScopedTimer() {
    if constexpr (FDBG) {
      auto End = std::chrono::steady_clock::now();
      double Ms =
          std::chrono::duration<double, std::milli>(End - Start).count();
      if (Ms >= FDBG_SLOW_MS) {
        llvm::errs() << "[FDBG] #" << Seq << " SLOW  " << Tag << " took " << Ms
                     << "ms\n";
      }
    }
  }
};

#define FDBG_LINE(MSG)                                                          \
  do {                                                                          \
    if constexpr (FDBG) {                                                       \
      llvm::errs() << "[FDBG] " << MSG << "\n";                                 \
    }                                                                           \
  } while (0)

#define FDBG_RATE(MSG, N)                                                       \
  do {                                                                          \
    if constexpr (FDBG) {                                                       \
      static std::atomic<uint64_t> _c{0};                                       \
      if ((++_c % (N)) == 0)                                                    \
        llvm::errs() << "[FDBG] " << MSG << "\n";                               \
    }                                                                           \
  } while (0)

// ============================================================================
// Lattice helpers
// ============================================================================
static inline const char *kindStr(Feasibility::l_t::Kind K) {
  using Kt = Feasibility::l_t::Kind;
  switch (K) {
  case Kt::Top:
    return "Top";
  case Kt::Bottom:
    return "Bottom";
  case Kt::Normal:
    return "Normal";
  }
  return "?";
}

static inline void dumpLatticeBrief(const char *Pfx,
                                    const Feasibility::l_t &x) {
  llvm::errs() << "[FDBG] " << Pfx << " kind=" << kindStr(x.getKind())
               << " pc=" << x.getFormulaId() << " env=" << x.getEnvId() << "\n";
}

// --- New-API helpers: never touch l_t internals directly ---
// IMPORTANT: In this analysis, Top/Bottom are global truth constants.
// We intentionally reset env to 0 on Top/Bottom to avoid env-driven blowups.
static inline Feasibility::l_t mkTopLike(const Feasibility::l_t &src) {
  auto out = src;
  out.setKind(Feasibility::l_t::Kind::Top);
  out.setFormulaId(Feasibility::l_t::topId);
  out.setEnvId(src.getEnvId());
  return out;
}

static inline Feasibility::l_t mkBottomLike(const Feasibility::l_t &src) {
  auto out = src;
  out.setKind(Feasibility::l_t::Kind::Bottom);
  out.setFormulaId(Feasibility::l_t::bottomId);
  out.setEnvId(src.getEnvId());
  return out;
}

static inline Feasibility::l_t mkNormalLike(const Feasibility::l_t &src,
                                            uint32_t pc, uint32_t env) {
  auto out = src;
  out.setKind(Feasibility::l_t::Kind::Normal);
  out.setFormulaId(pc);
  out.setEnvId(env);
  return out;
}

static inline FeasibilityAnalysisManager *
pickManager(FeasibilityAnalysisManager *fromEF, const Feasibility::l_t &src) {
  if (fromEF)
    return fromEF;
  return src.getManager();
}

// ============================================================================
// EF category predicates
// ============================================================================
static inline bool isIdEF(const EF &ef) noexcept {
  return ef.template isa<psr::EdgeIdentity<l_t>>();
}

static inline bool isAllTopEF(const EF &ef) noexcept {
  return ef.template isa<FeasibilityAllTopEF>() ||
         ef.template isa<psr::AllTop<l_t>>();
}

static inline bool isAllBottomEF(const EF &ef) noexcept {
  return ef.template isa<FeasibilityAllBottomEF>() ||
         ef.template isa<psr::AllBottom<l_t>>();
}

// ============================================================================
// JOIN CUT POLICY
// ============================================================================
// You requested: "On each join we emit true. Just true. We only want to detect
// pruned paths."
//
// Therefore:
//   - Bottom is neutral for OR-join (false OR x = x)
//   - Top is absorbing (true OR x = true)
//   - Any non-trivial join (two different, non-bottom EFs) collapses to Top.
//
// This kills join chains and avoids OR explosion.
static inline EF cutJoinToTop(const EF &lhs, const EF &rhs) {
  if (isAllTopEF(lhs) || isAllTopEF(rhs))
    return EF(std::in_place_type<FeasibilityAllTopEF>);

  if (isAllBottomEF(lhs))
    return rhs;
  if (isAllBottomEF(rhs))
    return lhs;

  // Non-trivial merge => forget disjunction, keep "feasible".
  return EF(std::in_place_type<FeasibilityAllTopEF>);
}

} // namespace

// =====================================================================================================================
// FeasibilityAllTopEF
//
// Constant TRUE edge function (maps any input to lattice Top / tautology).
// This is the "cut" result used at joins.
// =====================================================================================================================

l_t FeasibilityAllTopEF::computeTarget(const l_t &source) const {
  return mkTopLike(source); // pc=true, env preserved
}

// IMPORTANT: make reset NOT dominate composition
EF FeasibilityAllTopEF::compose(psr::EdgeFunctionRef<FeasibilityAllTopEF> /*thisFunc*/, const EF &g) {
  // Reset ∘ g  => g   (so reset does not wipe later constraints)
  return EF(g);
}

EF FeasibilityAllTopEF::join(psr::EdgeFunctionRef<FeasibilityAllTopEF> /*thisFunc*/, const psr::EdgeFunction<l_t> &otherFunc) {
  return EF(std::in_place_type<FeasibilityAllTopEF>);
}

// =====================================================================================================================
// FeasibilityAllBottomEF
// =====================================================================================================================

l_t FeasibilityAllBottomEF::computeTarget(const l_t &source) const {
  ScopedTimer T("AllBottomEF::computeTarget");
  if constexpr (FDBG)
    dumpLatticeBrief("AllBottom.in ", source);

  auto out = mkBottomLike(source);
  if constexpr (FDBG)
    dumpLatticeBrief("AllBottom.out", out);
  return out;
}

EF FeasibilityAllBottomEF::compose(
    psr::EdgeFunctionRef<FeasibilityAllBottomEF> /*thisFunc*/,
    const EF & /*secondFunction*/) {
  // Bottom ∘ g = Bottom
  ScopedTimer T("AllBottomEF::compose");
  FDBG_RATE("AllBottom ∘ g = AllBottom", 100000);
  return EF(std::in_place_type<FeasibilityAllBottomEF>);
}

EF FeasibilityAllBottomEF::join(
    psr::EdgeFunctionRef<FeasibilityAllBottomEF> /*thisFunc*/,
    const psr::EdgeFunction<l_t> &otherFunc) {
  // false OR f = f
  ScopedTimer T("AllBottomEF::join");
  FDBG_RATE("AllBottom ⊔ f = f (Bottom neutral)", 100000);

  if (otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
  }
  if (otherFunc.template isa<FeasibilityAllTopEF>() ||
      otherFunc.template isa<psr::AllTop<l_t>>()) {
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }
  if (otherFunc.template isa<FeasibilityAllBottomEF>() ||
      otherFunc.template isa<psr::AllBottom<l_t>>()) {
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
  }
  return EF(otherFunc);
}

// =====================================================================================================================
// FeasibilityPHITranslateEF
// =====================================================================================================================

l_t FeasibilityPHITranslateEF::computeTarget(const l_t &source) const {
  ScopedTimer T("PHITranslateEF::computeTarget");
  if constexpr (FDBG)
    dumpLatticeBrief("Phi.in ", source);

  FeasibilityElement cached;
  if (Memo.lookup(source, cached))
    return cached;

  auto *M = pickManager(this->manager, source);

  if (source.isBottom()) {
    FDBG_RATE("Phi.computeTarget: source is Bottom -> return Bottom", 100000);
    return mkBottomLike(source);
  }

  if (!PredBB || !SuccBB) {
    FDBG_RATE("Phi.computeTarget: missing PredBB/SuccBB -> Identity", 100000);
    return source;
  }

  const uint32_t incomingEnvId = source.getEnvId();
  const uint32_t outEnvId = M->applyPhiPack(incomingEnvId, PredBB, SuccBB);

  auto out = source;
  out.setEnvId(outEnvId);

  if constexpr (FDBG)
    dumpLatticeBrief("Phi.out", out);
  Memo.store(source, out);
  return out;
}

EF FeasibilityPHITranslateEF::compose(
    psr::EdgeFunctionRef<FeasibilityPHITranslateEF> thisFunc,
    const EF &secondFunction) {
  ScopedTimer T("PHITranslateEF::compose");

  if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
    FDBG_RATE("Phi ∘ Identity -> Phi", 100000);
    return EF(thisFunc);
  }
  if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
    FDBG_RATE("Phi ∘ Bottom -> Bottom", 100000);
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
  }

  // DO NOT simplify Phi ∘ Top here:
  // Top is a constant lattice element and we still want later constraints to apply
  // correctly after a join-cut. Keep it lazy/canonical.
  auto *M = thisFunc->manager;
  const auto step = PhiStep(thisFunc->PredBB, thisFunc->SuccBB);

  if (auto *otherPhi =
          secondFunction.template dyn_cast<FeasibilityPHITranslateEF>()) {
    FDBG_RATE("Phi ∘ Phi -> ANDFormula(phiChain=[other, this])", 100000);
    FeasibilityClause clause;
    clause.PhiChain.push_back(PhiStep(otherPhi->PredBB, otherPhi->SuccBB));
    clause.PhiChain.push_back(step);
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M,
              std::move(clause));
  }

  if (auto *addCons =
          secondFunction.template dyn_cast<FeasibilityAddConstrainEF>()) {
    FDBG_RATE("Phi ∘ Add -> ANDFormula(phiChain=[this], constr=[add])", 100000);
    FeasibilityClause clause;
    clause.PhiChain.push_back(step);
    clause.Constrs.push_back(
        LazyICmp(addCons->ConstraintInst, addCons->isTrueBranch));
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M,
              std::move(clause));
  }

  if (auto *andEF =
          secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
    FDBG_RATE("Phi ∘ AND -> AND (prepend phi step)", 100000);
    FeasibilityClause clause = andEF->Clause;
    clause.PhiChain.insert(clause.PhiChain.begin(), step);
    Util::normalizeClause(clause);
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M,
              std::move(clause));
  }

  FDBG_RATE("Phi ∘ <nontrivial> -> internCompose (manager-canonical)", 10000);
  return M->internCompose(EF(thisFunc), secondFunction);
}

EF FeasibilityPHITranslateEF::join(
    psr::EdgeFunctionRef<FeasibilityPHITranslateEF> thisFunc,
    const psr::EdgeFunction<l_t> &otherFunc) {
  ScopedTimer T("PHITranslateEF::join");

  // Apply global cut policy (kills join chains)
  return cutJoinToTop(EF(thisFunc), EF(otherFunc));
}

// =====================================================================================================================
// FeasibilityAddConstrainEF
// =====================================================================================================================

l_t FeasibilityAddConstrainEF::computeTarget(const l_t &source) const {
  ScopedTimer T("AddConstrainEF::computeTarget");
  if constexpr (FDBG)
    dumpLatticeBrief("Add.in ", source);

  FeasibilityElement cached;
  if (Memo.lookup(source, cached))
    return cached;

  auto *M = pickManager(this->manager, source);

  if (source.isBottom()) {
    FDBG_RATE("Add.computeTarget: source is Bottom -> return Bottom", 100000);
    return mkBottomLike(source);
  }

  if (!ConstraintInst) {
    llvm::errs()
        << "ALARM in FeasibilityAddConstrainEF::computeTarget: ConstraintInst is null\n";
    return source;
  }

  const uint32_t incomingPC = source.getFormulaId();
  const uint32_t env = source.getEnvId();

  z3::expr newConstraint =
      Util::createConstraintFromICmp(M, ConstraintInst, isTrueBranch, env);
  const uint32_t constraintId = Util::findOrAddFormulaId(M, newConstraint);

  const uint32_t outPC = M->mkAnd(incomingPC, constraintId);

  if (!M->isSat(outPC)) {
    FDBG_RATE("Add.computeTarget: UNSAT -> Bottom", 100000);
    auto out = mkBottomLike(source);
    if constexpr (FDBG)
      dumpLatticeBrief("Add.out", out);
    Memo.store(source, out);
    return out;
  }

  auto out = source;
  out.setFormulaId(outPC);
  if (out.getKind() == l_t::Kind::Top) {
    out.setKind(l_t::Kind::Normal);
  }

  if constexpr (FDBG)
    dumpLatticeBrief("Add.out", out);
  Memo.store(source, out);
  return out;
}

EF FeasibilityAddConstrainEF::compose(
    psr::EdgeFunctionRef<FeasibilityAddConstrainEF> thisFunc,
    const EF &secondFunction) {
  ScopedTimer T("AddConstrainEF::compose");

  if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
    FDBG_RATE("Add ∘ Identity -> Add", 100000);
    return EF(thisFunc);
  }
  if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
    FDBG_RATE("Add ∘ Bottom -> Bottom", 100000);
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
  }

  // DO NOT simplify Add ∘ Top here (see note above).
  auto *M = thisFunc->manager;

  FeasibilityClause thisClause;
  thisClause.Constrs.push_back(
      LazyICmp(thisFunc->ConstraintInst, thisFunc->isTrueBranch));

  if (auto *otherPhi =
          secondFunction.template dyn_cast<FeasibilityPHITranslateEF>()) {
    FDBG_RATE("Add ∘ Phi -> ANDFormula(constr=[this], phiChain=[phi])", 100000);
    FeasibilityClause clause = thisClause;
    clause.PhiChain.push_back(PhiStep(otherPhi->PredBB, otherPhi->SuccBB));
    Util::normalizeClause(clause);
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M,
              std::move(clause));
  }

  if (auto *otherAdd =
          secondFunction.template dyn_cast<FeasibilityAddConstrainEF>()) {
    FDBG_RATE("Add ∘ Add -> ANDFormula(2 constrs)", 100000);
    FeasibilityClause clause = thisClause;
    clause.Constrs.push_back(
        LazyICmp(otherAdd->ConstraintInst, otherAdd->isTrueBranch));
    Util::normalizeClause(clause);
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M,
              std::move(clause));
  }

  if (auto *otherAnd =
          secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
    FDBG_RATE("Add ∘ AND -> AND (append constr)", 100000);
    FeasibilityClause clause = Util::conjClauses(otherAnd->Clause, thisClause);
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
  }

  FDBG_RATE("Add ∘ <nontrivial> -> internCompose (manager-canonical)", 10000);
  return M->internCompose(EF(thisFunc), secondFunction);
}

EF FeasibilityAddConstrainEF::join(
    psr::EdgeFunctionRef<FeasibilityAddConstrainEF> thisFunc,
    const psr::EdgeFunction<l_t> &secondFunction) {
  ScopedTimer T("AddConstrainEF::join");

  // Apply global cut policy (kills join chains)
  return cutJoinToTop(EF(thisFunc), EF(secondFunction));
}

// =====================================================================================================================
// FeasibilityANDFormulaEF
// =====================================================================================================================

l_t FeasibilityANDFormulaEF::computeTarget(const l_t &source) const {
  ScopedTimer T("ANDFormulaEF::computeTarget");
  if constexpr (FDBG)
    dumpLatticeBrief("AND.in ", source);

  FeasibilityElement cached;
  if (Memo.lookup(source, cached))
    return cached;

  auto *M = pickManager(manager, source);

  if (source.isBottom()) {
    FDBG_RATE("AND.computeTarget: source is Bottom -> return Bottom", 100000);
    return mkBottomLike(source);
  }

  const bool hasPhi = !Clause.PhiChain.empty();
  const bool hasConstr = !Clause.Constrs.empty();

  const uint32_t srcEnv = source.getEnvId();
  const uint32_t outEnv =
      hasPhi ? Util::applyPhiChain(M, srcEnv, Clause.PhiChain) : srcEnv;

  if (!hasConstr) {
    auto out = source;
    out.setEnvId(outEnv);
    Memo.store(source, out);
    return out;
  }

  uint32_t pc = source.getFormulaId();

  for (const auto &lazyConstr : Clause.Constrs) {
    if (!lazyConstr.I) {
      llvm::errs()
          << "ALARM in FeasibilityANDFormulaEF::computeTarget: LazyICmp has null instruction\n";
      continue;
    }

    z3::expr expr =
        Util::createConstraintFromICmp(M, lazyConstr.I, lazyConstr.TrueEdge,
                                       outEnv);
    const uint32_t cid = Util::findOrAddFormulaId(M, expr);

    pc = M->mkAnd(pc, cid);

    if (!M->isSat(pc)) {
      FDBG_RATE("AND.computeTarget: UNSAT -> Bottom", 100000);
      auto out = mkBottomLike(source);
      out.setEnvId(0);
      if constexpr (FDBG)
        dumpLatticeBrief("AND.out", out);
      Memo.store(source, out);
      return out;
    }
  }

  auto out = source;
  out.setFormulaId(pc);
  out.setEnvId(outEnv);
  if (out.getKind() == l_t::Kind::Top) {
    out.setKind(l_t::Kind::Normal);
  }

  if constexpr (FDBG)
    dumpLatticeBrief("AND.out", out);
  Memo.store(source, out);
  return out;
}

EF FeasibilityANDFormulaEF::compose(
    psr::EdgeFunctionRef<FeasibilityANDFormulaEF> thisFunc,
    const EF &secondFunction) {
  ScopedTimer T("ANDFormulaEF::compose");

  if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
    FDBG_RATE("AND ∘ Identity -> AND", 100000);
    return EF(thisFunc);
  }
  if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
    FDBG_RATE("AND ∘ Bottom -> Bottom", 100000);
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
  }

  auto *M = thisFunc->manager;

  if (auto *phi =
          secondFunction.template dyn_cast<FeasibilityPHITranslateEF>()) {
    FDBG_RATE("AND ∘ Phi -> AND(conjClauses)", 100000);
    FeasibilityClause phiClause =
        Util::clauseFromPhi(phi->PredBB, phi->SuccBB);
    FeasibilityClause merged = Util::conjClauses(thisFunc->Clause, phiClause);
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M,
              std::move(merged));
  }

  if (auto *add =
          secondFunction.template dyn_cast<FeasibilityAddConstrainEF>()) {
    FDBG_RATE("AND ∘ Add -> AND(conjClauses)", 100000);
    FeasibilityClause addClause =
        Util::clauseFromIcmp(add->ConstraintInst, add->isTrueBranch);
    FeasibilityClause merged = Util::conjClauses(thisFunc->Clause, addClause);
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M,
              std::move(merged));
  }

  if (auto *and2 =
          secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
    FDBG_RATE("AND ∘ AND -> AND(conjClauses)", 100000);
    FeasibilityClause merged = Util::conjClauses(thisFunc->Clause, and2->Clause);
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M,
              std::move(merged));
  }

  // PRUNE-ONLY MODE: never distribute over OR (causes blowups).
  // Keep lazy/canonical; OR should be rare anyway due to cut-joins.
  FDBG_RATE("AND ∘ <nontrivial> -> internCompose (manager-canonical)", 10000);
  return M->internCompose(EF(thisFunc), secondFunction);
}

EF FeasibilityANDFormulaEF::join(
    psr::EdgeFunctionRef<FeasibilityANDFormulaEF> thisFunc,
    const psr::EdgeFunction<l_t> &otherFunc) {
  ScopedTimer T("ANDFormulaEF::join");

  // Apply global cut policy (kills join chains)
  return cutJoinToTop(EF(thisFunc), EF(otherFunc));
}

// =====================================================================================================================
// FeasibilityORFormulaEF
// =====================================================================================================================

l_t FeasibilityORFormulaEF::computeTarget(const l_t &source) const {
  ScopedTimer T("ORFormulaEF::computeTarget");
  if constexpr (FDBG)
    dumpLatticeBrief("OR.in ", source);

  FeasibilityElement cached;
  if (Memo.lookup(source, cached))
    return cached;

  auto *M = pickManager(manager, source);

  if (source.isBottom()) {
    FDBG_RATE("OR.computeTarget: source is Bottom -> return Bottom", 100000);
    return mkBottomLike(source);
  }

  uint32_t accPC = l_t::bottomId;
  bool anySat = false;

  for (const auto &clause : Clauses) {
    FeasibilityANDFormulaEF andEF(M, clause);
    l_t out = andEF.computeTarget(source);

    if (out.isBottom()) {
      continue;
    }

    accPC = M->mkOr(accPC, out.getFormulaId());
    anySat = true;
  }

  if (!anySat) {
    FDBG_RATE("OR.computeTarget: all clauses UNSAT -> Bottom", 100000);
    auto out = mkBottomLike(source);
    if constexpr (FDBG)
      dumpLatticeBrief("OR.out", out);
    Memo.store(source, out);
    return out;
  }

  auto out = source;
  out.setFormulaId(accPC);
  if (out.getKind() == l_t::Kind::Top) {
    out.setKind(l_t::Kind::Normal);
  }

  if constexpr (FDBG)
    dumpLatticeBrief("OR.out", out);
  Memo.store(source, out);
  return out;
}

EF FeasibilityORFormulaEF::compose(
    psr::EdgeFunctionRef<FeasibilityORFormulaEF> thisFunc,
    const EF &secondFunction) {
  ScopedTimer T("ORFormulaEF::compose");

  if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
    FDBG_RATE("OR ∘ Identity -> OR", 100000);
    return EF(thisFunc);
  }

  if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
    FDBG_RATE("OR ∘ Bottom -> Bottom", 100000);
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
  }

  // Keep lazy compose; manager interns/canonicalizes.
  FDBG_RATE("OR ∘ <nontrivial> -> internCompose (manager-canonical)", 10000);
  return thisFunc->manager->internCompose(EF(thisFunc), secondFunction);
}

EF FeasibilityORFormulaEF::join(
    psr::EdgeFunctionRef<FeasibilityORFormulaEF> thisFunc,
    const psr::EdgeFunction<l_t> &otherFunc) {
  ScopedTimer T("ORFormulaEF::join");

  // Apply global cut policy (kills join chains)
  return cutJoinToTop(EF(thisFunc), EF(otherFunc));
}

// ============================================================================
// FeasibilityComposeEF
// ============================================================================

l_t FeasibilityComposeEF::computeTarget(const l_t &source) const {
  ScopedTimer T("ComposeEF::computeTarget");

  FeasibilityElement cached;
  if (Memo.lookup(source, cached))
    return cached;

  if constexpr (FDBG)
    dumpLatticeBrief("Compose.in ", source);

  // Bottom is absorbing
  if (source.isBottom()) {
    Memo.store(source, source);
    return source;
  }

  // --------- Algebraic short-circuits (no evaluation) ---------
  if (isAllBottomEF(Second)) {
    auto out = mkBottomLike(source);
    Memo.store(source, out);
    return out;
  }

  if (isIdEF(Second)) {
    const l_t out = First.computeTarget(source);
    Memo.store(source, out);
    return out;
  }
  if (isIdEF(First)) {
    const l_t out = Second.computeTarget(source);
    Memo.store(source, out);
    return out;
  }
  if (isAllBottomEF(First)) {
    auto out = mkBottomLike(source);
    Memo.store(source, out);
    return out;
  }
  // ------------------------------------------------------------

  const l_t mid = Second.computeTarget(source);
  if constexpr (FDBG)
    dumpLatticeBrief("Compose.mid", mid);

  if (mid.isBottom()) {
    Memo.store(source, mid);
    return mid;
  }

  const l_t out = First.computeTarget(mid);
  if constexpr (FDBG)
    dumpLatticeBrief("Compose.out", out);
  Memo.store(source, out);
  return out;
}

EF FeasibilityComposeEF::compose(
    psr::EdgeFunctionRef<FeasibilityComposeEF> thisFunc,
    const EF &secondFunction) {
  ScopedTimer T("ComposeEF::compose");

  if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
    FDBG_RATE("Compose ∘ Identity -> Compose", 100000);
    return EF(thisFunc);
  }
  if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
    FDBG_RATE("Compose ∘ Bottom -> Bottom", 100000);
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
  }

  // Keep lazy; manager canonicalizes/interns compose chain.
  FDBG_RATE("Compose ∘ h -> internCompose (manager-canonical)", 10000);
  return thisFunc->manager->internCompose(EF(thisFunc), secondFunction);
}

EF FeasibilityComposeEF::join(
    psr::EdgeFunctionRef<FeasibilityComposeEF> thisFunc,
    const psr::EdgeFunction<l_t> &otherFunc) {
  ScopedTimer T("ComposeEF::join");

  // Apply global cut policy (kills join chains)
  return cutJoinToTop(EF(thisFunc), EF(otherFunc));
}

// ============================================================================
// FeasibilityJoinEF
// ============================================================================
//
// NOTE: With the cut-join policy, solver-level joins should almost never need
// to materialize JoinEF nodes. We keep it as a safety net, but it still
// collapses to TRUE unless both sides are Bottom.
// ============================================================================

l_t FeasibilityJoinEF::computeTarget(const l_t &source) const {
  ScopedTimer T("JoinEF::computeTarget");

  FeasibilityElement cached;
  if (Memo.lookup(source, cached))
    return cached;

  if constexpr (FDBG)
    dumpLatticeBrief("Join.in ", source);

  if (source.isBottom()) {
    Memo.store(source, source);
    return source;
  }

  // Evaluate both sides
  const l_t L = Left.computeTarget(source);
  const l_t R = Right.computeTarget(source);

  // If both infeasible => Bottom, otherwise cut to Top.
  if (L.isBottom() && R.isBottom()) {
    auto out = mkBottomLike(source);
    Memo.store(source, out);
    return out;
  }

  auto out = mkTopLike(source);
  Memo.store(source, out);
  return out;
}

EF FeasibilityJoinEF::compose(psr::EdgeFunctionRef<FeasibilityJoinEF> thisFunc,
                              const EF &secondFunction) {
  ScopedTimer T("JoinEF::compose");

  if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
    FDBG_RATE("Join ∘ Identity -> Join", 100000);
    return EF(thisFunc);
  }
  if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
    FDBG_RATE("Join ∘ Bottom -> Bottom", 100000);
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
  }

  // Keep lazy; manager canonicalizes/interns compose chain.
  FDBG_RATE("Join ∘ h -> internCompose (manager-canonical)", 10000);
  return thisFunc->manager->internCompose(EF(thisFunc), secondFunction);
}

EF FeasibilityJoinEF::join(psr::EdgeFunctionRef<FeasibilityJoinEF> thisFunc,
                           const psr::EdgeFunction<l_t> &otherFunc) {
  ScopedTimer T("JoinEF::join");

  // Apply global cut policy (kills join chains)
  return cutJoinToTop(EF(thisFunc), EF(otherFunc));
}

} // namespace Feasibility