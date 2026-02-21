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
constexpr bool FDBG = false;            // flip to false to disable
constexpr uint64_t FDBG_EVERY = 10000; // periodic heartbeat
constexpr double FDBG_SLOW_MS = 50.0;  // warn if a call exceeds this

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
static inline Feasibility::l_t mkTopLike(const Feasibility::l_t &src) {
  auto out = src;
  out.setKind(Feasibility::l_t::Kind::Top);
  out.setFormulaId(Feasibility::l_t::topId);
  out.setEnvId(0); // Top env forced to 0
  return out;
}

static inline Feasibility::l_t mkBottomLike(const Feasibility::l_t &src) {
  auto out = src;
  out.setKind(Feasibility::l_t::Kind::Bottom);
  out.setFormulaId(Feasibility::l_t::bottomId);
  out.setEnvId(0); // Bottom env forced to 0
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

static inline FeasibilityAnalysisManager *pickManager(FeasibilityAnalysisManager *fromEF,
                                                      const Feasibility::l_t &src) {
  if (fromEF)
    return fromEF;
  return src.getManager();
}
} // namespace

// =====================================================================================================================
// FeasibilityAllTopEF

l_t FeasibilityAllTopEF::computeTarget(const l_t &source) const {
  ScopedTimer T("AllTopEF::computeTarget");
  if constexpr (FDBG) dumpLatticeBrief("AllTop.in ", source);

  auto out = mkTopLike(source);
  if constexpr (FDBG) dumpLatticeBrief("AllTop.out", out);
  return out;
}

EF FeasibilityAllTopEF::compose(psr::EdgeFunctionRef<FeasibilityAllTopEF> /*thisFunc*/,
                                const EF & /*secondFunction*/) {
  ScopedTimer T("AllTopEF::compose");
  FDBG_LINE("AllTop ∘ g = AllTop");
  return EF(std::in_place_type<FeasibilityAllTopEF>);
}

EF FeasibilityAllTopEF::join(psr::EdgeFunctionRef<FeasibilityAllTopEF> /*thisFunc*/,
                             const psr::EdgeFunction<l_t> & /*otherFunc*/) {
  ScopedTimer T("AllTopEF::join");
  FDBG_LINE("AllTop ⊔ f = AllTop");
  return EF(std::in_place_type<FeasibilityAllTopEF>);
}

// =====================================================================================================================
// FeasibilityAllBottomEF

l_t FeasibilityAllBottomEF::computeTarget(const l_t &source) const {
  ScopedTimer T("AllBottomEF::computeTarget");
  if constexpr (FDBG) dumpLatticeBrief("AllBottom.in ", source);

  auto out = mkBottomLike(source);
  if constexpr (FDBG) dumpLatticeBrief("AllBottom.out", out);
  return out;
}

EF FeasibilityAllBottomEF::compose(
    psr::EdgeFunctionRef<FeasibilityAllBottomEF> /*thisFunc*/,
    const EF & /*secondFunction*/) {
  ScopedTimer T("AllBottomEF::compose");
  FDBG_LINE("AllBottom ∘ g = AllBottom");
  return EF(std::in_place_type<FeasibilityAllBottomEF>);
}

EF FeasibilityAllBottomEF::join(
    psr::EdgeFunctionRef<FeasibilityAllBottomEF> /*thisFunc*/,
    const psr::EdgeFunction<l_t> &otherFunc) {
  ScopedTimer T("AllBottomEF::join");
  FDBG_LINE("AllBottom ⊔ f = f (Bottom neutral)");

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

l_t FeasibilityPHITranslateEF::computeTarget(const l_t &source) const {
  ScopedTimer T("PHITranslateEF::computeTarget");
  if constexpr (FDBG) dumpLatticeBrief("Phi.in ", source);

  FeasibilityElement cached;
  if (Memo.lookup(source, cached))
    return cached;

  auto *M = pickManager(this->manager, source);

  if (source.isBottom()) {
    FDBG_LINE("Phi.computeTarget: source is Bottom -> return Bottom");
    return mkBottomLike(source);
  }

  if (!PredBB || !SuccBB) {
    FDBG_LINE("Phi.computeTarget: missing PredBB/SuccBB -> Identity");
    return source;
  }

  const uint32_t incomingEnvId = source.getEnvId();
  const uint32_t outEnvId = M->applyPhiPack(incomingEnvId, PredBB, SuccBB);

  auto out = source;
  out.setEnvId(outEnvId);

  if constexpr (FDBG) dumpLatticeBrief("Phi.out", out);
  Memo.store(source, out);
  return out;
}

EF FeasibilityPHITranslateEF::compose(
    psr::EdgeFunctionRef<FeasibilityPHITranslateEF> thisFunc,
    const EF &secondFunction) {
  ScopedTimer T("PHITranslateEF::compose");

  if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
    FDBG_LINE("Phi ∘ Identity -> Phi");
    return EF(thisFunc);
  }
  if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
    FDBG_LINE("Phi ∘ Bottom -> Bottom");
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
  }
  if (secondFunction.template isa<FeasibilityAllTopEF>()) {
    FDBG_LINE("Phi ∘ Top -> Top");
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }

  auto *M = thisFunc->manager;
  const auto step = PhiStep(thisFunc->PredBB, thisFunc->SuccBB);

  if (auto *otherPhi =
          secondFunction.template dyn_cast<FeasibilityPHITranslateEF>()) {
    FDBG_LINE("Phi ∘ Phi -> ANDFormula(phiChain=[other, this])");
    FeasibilityClause clause;
    clause.PhiChain.push_back(PhiStep(otherPhi->PredBB, otherPhi->SuccBB));
    clause.PhiChain.push_back(step);
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
  }

  if (auto *addCons =
          secondFunction.template dyn_cast<FeasibilityAddConstrainEF>()) {
    FDBG_LINE("Phi ∘ Add -> ANDFormula(phiChain=[this], constr=[add])");
    FeasibilityClause clause;
    clause.PhiChain.push_back(step);
    clause.Constrs.push_back(
        LazyICmp(addCons->ConstraintInst, addCons->isTrueBranch));
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
  }

  if (auto *andEF =
          secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
    FDBG_LINE("Phi ∘ AND -> AND (prepend phi step)");
    FeasibilityClause clause = andEF->Clause;
    clause.PhiChain.insert(clause.PhiChain.begin(), step);
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
  }

  if (secondFunction.template isa<FeasibilityORFormulaEF>()) {
    FDBG_LINE("Phi ∘ OR -> lazy ComposeEF");
    return EF(std::in_place_type<FeasibilityComposeEF>, M, EF(thisFunc),
              secondFunction);
  }

  llvm::errs()
      << "ALARM in FeasibilityPHITranslateEF::compose: unsupported secondFunction\n";
  return secondFunction;
}

EF FeasibilityPHITranslateEF::join(
    psr::EdgeFunctionRef<FeasibilityPHITranslateEF> thisFunc,
    const psr::EdgeFunction<l_t> &otherFunc) {
  ScopedTimer T("PHITranslateEF::join");

  if (otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
    FDBG_LINE("Phi ⊔ Identity -> Identity (kept as before)");
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
  }
  if (otherFunc.template isa<FeasibilityAllTopEF>()) {
    FDBG_LINE("Phi ⊔ Top -> Top");
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }
  if (otherFunc.template isa<FeasibilityAllBottomEF>()) {
    FDBG_LINE("Phi ⊔ Bottom -> Phi");
    return EF(thisFunc);
  }

  auto *M = thisFunc->manager;

  FeasibilityClause thisClause;
  thisClause.PhiChain.push_back(PhiStep(thisFunc->PredBB, thisFunc->SuccBB));

  if (auto *otherPhi =
          otherFunc.template dyn_cast<FeasibilityPHITranslateEF>()) {
    FDBG_LINE("Phi ⊔ Phi -> ORFormula(2 phi-clauses)");
    FeasibilityClause otherClause;
    otherClause.PhiChain.push_back(PhiStep(otherPhi->PredBB, otherPhi->SuccBB));

    llvm::SmallVector<FeasibilityClause, 4> clauses;
    clauses.push_back(std::move(thisClause));
    clauses.push_back(std::move(otherClause));
    return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
  }

  if (auto *addCons =
          otherFunc.template dyn_cast<FeasibilityAddConstrainEF>()) {
    FDBG_LINE("Phi ⊔ Add -> ORFormula(phiClause, addClause)");
    FeasibilityClause clauseOfAdd =
        Util::clauseFromIcmp(addCons->ConstraintInst, addCons->isTrueBranch);

    llvm::SmallVector<FeasibilityClause, 4> clauses;
    clauses.push_back(std::move(thisClause));
    clauses.push_back(std::move(clauseOfAdd));
    return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
  }

  if (auto *andEF = otherFunc.template dyn_cast<FeasibilityANDFormulaEF>()) {
    FDBG_LINE("Phi ⊔ AND -> ORFormula(phiClause, andClause)");
    llvm::SmallVector<FeasibilityClause, 4> clauses;
    clauses.push_back(std::move(thisClause));
    clauses.push_back(andEF->Clause);
    return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
  }

  if (auto *orEF = otherFunc.template dyn_cast<FeasibilityORFormulaEF>()) {
    FDBG_LINE("Phi ⊔ OR -> OR (append phiClause)");
    auto clauses = orEF->Clauses;
    clauses.push_back(std::move(thisClause));
    return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
  }

  llvm::errs()
      << "ALARM in FeasibilityPHITranslateEF::join: unsupported otherFunc\n";
  return EF(otherFunc);
}

// =====================================================================================================================
// FeasibilityAddConstrainEF

l_t FeasibilityAddConstrainEF::computeTarget(const l_t &source) const {
  ScopedTimer T("AddConstrainEF::computeTarget");
  if constexpr (FDBG) dumpLatticeBrief("Add.in ", source);

  FeasibilityElement cached;
  if (Memo.lookup(source, cached))
    return cached;

  auto *M = pickManager(this->manager, source);

  if (source.isBottom()) {
    FDBG_LINE("Add.computeTarget: source is Bottom -> return Bottom");
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
    FDBG_LINE("Add.computeTarget: UNSAT -> Bottom");
    auto out = mkBottomLike(source);
    if constexpr (FDBG) dumpLatticeBrief("Add.out", out);
    Memo.store(source, out);
    return out;
  }

  auto out = source;
  out.setFormulaId(outPC);
  if (out.getKind() == l_t::Kind::Top) {
    out.setKind(l_t::Kind::Normal);
  }

  if constexpr (FDBG) dumpLatticeBrief("Add.out", out);
  Memo.store(source, out);
  return out;
}

EF FeasibilityAddConstrainEF::compose(
    psr::EdgeFunctionRef<FeasibilityAddConstrainEF> thisFunc,
    const EF &secondFunction) {
  ScopedTimer T("AddConstrainEF::compose");

  if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
    FDBG_LINE("Add ∘ Identity -> Add");
    return EF(thisFunc);
  }
  if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
    FDBG_LINE("Add ∘ Bottom -> Bottom");
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
  }
  if (secondFunction.template isa<FeasibilityAllTopEF>()) {
    FDBG_LINE("Add ∘ Top -> Top");
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }

  auto *M = thisFunc->manager;

  FeasibilityClause thisClause;
  thisClause.Constrs.push_back(
      LazyICmp(thisFunc->ConstraintInst, thisFunc->isTrueBranch));

  if (auto *otherPhi =
          secondFunction.template dyn_cast<FeasibilityPHITranslateEF>()) {
    FDBG_LINE("Add ∘ Phi -> ANDFormula(constr=[this], phiChain=[phi])");
    FeasibilityClause clause = thisClause;
    clause.PhiChain.push_back(PhiStep(otherPhi->PredBB, otherPhi->SuccBB));
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
  }

  if (auto *otherAdd =
          secondFunction.template dyn_cast<FeasibilityAddConstrainEF>()) {
    FDBG_LINE("Add ∘ Add -> ANDFormula(2 constrs)");
    FeasibilityClause clause = thisClause;
    clause.Constrs.push_back(
        LazyICmp(otherAdd->ConstraintInst, otherAdd->isTrueBranch));
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
  }

  if (auto *otherAnd =
          secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
    FDBG_LINE("Add ∘ AND -> AND (append constr)");
    FeasibilityClause clause = otherAnd->Clause;
    clause.Constrs.append(thisClause.Constrs.begin(), thisClause.Constrs.end());
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
  }

  if (secondFunction.template isa<FeasibilityORFormulaEF>()) {
    FDBG_LINE("Add ∘ OR -> lazy ComposeEF");
    return EF(std::in_place_type<FeasibilityComposeEF>, M, EF(thisFunc),
              secondFunction);
  }

  llvm::errs()
      << "ALARM in FeasibilityAddConstrainEF::compose: unsupported secondFunction\n";
  return secondFunction;
}

EF FeasibilityAddConstrainEF::join(
    psr::EdgeFunctionRef<FeasibilityAddConstrainEF> thisFunc,
    const psr::EdgeFunction<l_t> &secondFunction) {
  ScopedTimer T("AddConstrainEF::join");

  if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
    FDBG_LINE("Add ⊔ Identity -> Identity (kept as before)");
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
  }
  if (secondFunction.template isa<FeasibilityAllTopEF>()) {
    FDBG_LINE("Add ⊔ Top -> Top");
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }
  if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
    FDBG_LINE("Add ⊔ Bottom -> Add");
    return EF{thisFunc};
  }

  auto *M = thisFunc->manager;

  FeasibilityClause thisClause;
  thisClause.Constrs.push_back(
      LazyICmp(thisFunc->ConstraintInst, thisFunc->isTrueBranch));

  if (auto *otherPhi =
          secondFunction.template dyn_cast<FeasibilityPHITranslateEF>()) {
    FDBG_LINE("Add ⊔ Phi -> ORFormula(addClause, phiClause)");
    FeasibilityClause phiClause;
    phiClause.PhiChain.push_back(PhiStep(otherPhi->PredBB, otherPhi->SuccBB));

    llvm::SmallVector<FeasibilityClause, 4> clauses;
    clauses.push_back(std::move(thisClause));
    clauses.push_back(std::move(phiClause));
    return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
  }

  if (auto *otherAdd =
          secondFunction.template dyn_cast<FeasibilityAddConstrainEF>()) {
    FDBG_LINE("Add ⊔ Add -> ORFormula(2 addClauses)");
    FeasibilityClause addClause;
    addClause.Constrs.push_back(
        LazyICmp(otherAdd->ConstraintInst, otherAdd->isTrueBranch));

    llvm::SmallVector<FeasibilityClause, 4> clauses;
    clauses.push_back(std::move(thisClause));
    clauses.push_back(std::move(addClause));
    return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
  }

  if (auto *otherAnd =
          secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
    FDBG_LINE("Add ⊔ AND -> ORFormula(addClause, andClause)");
    llvm::SmallVector<FeasibilityClause, 4> clauses;
    clauses.push_back(std::move(thisClause));
    clauses.push_back(otherAnd->Clause);
    return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
  }

  if (auto *otherOr =
          secondFunction.template dyn_cast<FeasibilityORFormulaEF>()) {
    FDBG_LINE("Add ⊔ OR -> OR (append addClause)");
    auto clauses = otherOr->Clauses;
    clauses.push_back(std::move(thisClause));
    return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
  }

  llvm::errs()
      << "ALARM in FeasibilityAddConstrainEF::join: unsupported secondFunction\n";
  return EF(secondFunction);
}

// =====================================================================================================================
// FeasibilityANDFormulaEF

l_t FeasibilityANDFormulaEF::computeTarget(const l_t &source) const {
  ScopedTimer T("ANDFormulaEF::computeTarget");
  if constexpr (FDBG) dumpLatticeBrief("AND.in ", source);

  FeasibilityElement cached;
  if (Memo.lookup(source, cached))
    return cached;

  auto *M = pickManager(manager, source);

  if (source.isBottom()) {
    FDBG_LINE("AND.computeTarget: source is Bottom -> return Bottom");
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

    z3::expr expr = Util::createConstraintFromICmp(
        M, lazyConstr.I, lazyConstr.TrueEdge, outEnv);
    const uint32_t cid = Util::findOrAddFormulaId(M, expr);

    pc = M->mkAnd(pc, cid);

    if (!M->isSat(pc)) {
      FDBG_LINE("AND.computeTarget: UNSAT -> Bottom");
      auto out = mkBottomLike(source);
      out.setEnvId(0); // Bottom env forced to 0
      if constexpr (FDBG) dumpLatticeBrief("AND.out", out);
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

  if constexpr (FDBG) dumpLatticeBrief("AND.out", out);
  Memo.store(source, out);
  return out;
}

EF FeasibilityANDFormulaEF::compose(
    psr::EdgeFunctionRef<FeasibilityANDFormulaEF> thisFunc,
    const EF &secondFunction) {
  ScopedTimer T("ANDFormulaEF::compose");

  if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
    FDBG_LINE("AND ∘ Identity -> AND");
    return EF(thisFunc);
  }
  if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
    FDBG_LINE("AND ∘ Bottom -> Bottom");
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
  }
  if (secondFunction.template isa<FeasibilityAllTopEF>()) {
    FDBG_LINE("AND ∘ Top -> Top");
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }

  auto *M = thisFunc->manager;

  if (auto *phi =
          secondFunction.template dyn_cast<FeasibilityPHITranslateEF>()) {
    FDBG_LINE("AND ∘ Phi -> AND(conjClauses)");
    FeasibilityClause phiClause = Util::clauseFromPhi(phi->PredBB, phi->SuccBB);
    FeasibilityClause merged = Util::conjClauses(thisFunc->Clause, phiClause);
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(merged));
  }

  if (auto *add =
          secondFunction.template dyn_cast<FeasibilityAddConstrainEF>()) {
    FDBG_LINE("AND ∘ Add -> AND(conjClauses)");
    FeasibilityClause addClause =
        Util::clauseFromIcmp(add->ConstraintInst, add->isTrueBranch);
    FeasibilityClause merged = Util::conjClauses(thisFunc->Clause, addClause);
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(merged));
  }

  if (auto *and2 =
          secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
    FDBG_LINE("AND ∘ AND -> AND(conjClauses)");
    FeasibilityClause merged =
        Util::conjClauses(thisFunc->Clause, and2->Clause);
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(merged));
  }

  if (auto *orEF =
          secondFunction.template dyn_cast<FeasibilityORFormulaEF>()) {
    FDBG_LINE("AND ∘ OR -> OR(distribute conj over each clause)");
    llvm::SmallVector<FeasibilityClause, 4> mergedClauses;
    mergedClauses.reserve(orEF->Clauses.size());
    for (const auto &c : orEF->Clauses) {
      mergedClauses.push_back(Util::conjClauses(thisFunc->Clause, c));
    }
    return EF(std::in_place_type<FeasibilityORFormulaEF>, M,
              std::move(mergedClauses));
  }

  llvm::errs()
      << "ALARM in FeasibilityANDFormulaEF::compose: unsupported secondFunction\n";
  return EF(secondFunction);
}

EF FeasibilityANDFormulaEF::join(
    psr::EdgeFunctionRef<FeasibilityANDFormulaEF> thisFunc,
    const psr::EdgeFunction<l_t> &otherFunc) {
  ScopedTimer T("ANDFormulaEF::join");

  if (otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
    FDBG_LINE("AND ⊔ Identity -> Identity");
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
  }
  if (otherFunc.template isa<FeasibilityAllTopEF>() ||
      otherFunc.template isa<psr::AllTop<l_t>>()) {
    FDBG_LINE("AND ⊔ Top -> Top");
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }
  if (otherFunc.template isa<FeasibilityAllBottomEF>() ||
      otherFunc.template isa<psr::AllBottom<l_t>>()) {
    FDBG_LINE("AND ⊔ Bottom -> AND");
    return EF(thisFunc);
  }

  auto *M = thisFunc->manager;

  if (otherFunc.template isa<FeasibilityJoinEF>() ||
      otherFunc.template isa<FeasibilityComposeEF>()) {
    FDBG_LINE("AND ⊔ (Join/Compose) -> JoinEF(lazy pointwise)");
    return EF(std::in_place_type<FeasibilityJoinEF>, M, EF(thisFunc),
              EF(otherFunc));
  }

  if (auto *phi =
          otherFunc.template dyn_cast<FeasibilityPHITranslateEF>()) {
    FDBG_LINE("AND ⊔ Phi -> ORFormula(andClause, phiClause)");
    llvm::SmallVector<FeasibilityClause, 4> clauses;
    clauses.push_back(thisFunc->Clause);
    clauses.push_back(Util::clauseFromPhi(phi->PredBB, phi->SuccBB));
    return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
  }

  if (auto *add =
          otherFunc.template dyn_cast<FeasibilityAddConstrainEF>()) {
    FDBG_LINE("AND ⊔ Add -> ORFormula(andClause, addClause)");
    llvm::SmallVector<FeasibilityClause, 4> clauses;
    clauses.push_back(thisFunc->Clause);
    clauses.push_back(
        Util::clauseFromIcmp(add->ConstraintInst, add->isTrueBranch));
    return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
  }

  if (auto *and2 = otherFunc.template dyn_cast<FeasibilityANDFormulaEF>()) {
    FDBG_LINE("AND ⊔ AND -> ORFormula(2 andClauses)");
    llvm::SmallVector<FeasibilityClause, 4> clauses;
    clauses.push_back(thisFunc->Clause);
    clauses.push_back(and2->Clause);
    return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
  }

  if (auto *orEF = otherFunc.template dyn_cast<FeasibilityORFormulaEF>()) {
    FDBG_LINE("AND ⊔ OR -> OR(append andClause)");
    auto clauses = orEF->Clauses;
    clauses.push_back(thisFunc->Clause);
    return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
  }

  FDBG_LINE("AND ⊔ <unknown> -> JoinEF(lazy pointwise)");
  return EF(std::in_place_type<FeasibilityJoinEF>, M, EF(thisFunc),
            EF(otherFunc));
}

// =====================================================================================================================
// FeasibilityORFormulaEF

l_t FeasibilityORFormulaEF::computeTarget(const l_t &source) const {
  ScopedTimer T("ORFormulaEF::computeTarget");
  if constexpr (FDBG) dumpLatticeBrief("OR.in ", source);

  FeasibilityElement cached;
  if (Memo.lookup(source, cached))
    return cached;

  auto *M = pickManager(manager, source);

  if (source.isBottom()) {
    FDBG_LINE("OR.computeTarget: source is Bottom -> return Bottom");
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
    FDBG_LINE("OR.computeTarget: all clauses UNSAT -> Bottom");
    auto out = mkBottomLike(source);
    if constexpr (FDBG) dumpLatticeBrief("OR.out", out);
    Memo.store(source, out);
    return out;
  }

  auto out = source;
  out.setFormulaId(accPC);
  if (out.getKind() == l_t::Kind::Top) {
    out.setKind(l_t::Kind::Normal);
  }

  if constexpr (FDBG) dumpLatticeBrief("OR.out", out);
  Memo.store(source, out);
  return out;
}

EF FeasibilityORFormulaEF::compose(psr::EdgeFunctionRef<FeasibilityORFormulaEF> thisFunc,
                                  const EF &secondFunction) {
  ScopedTimer T("ORFormulaEF::compose");

  if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
    if constexpr (FDBG) {
      FDBG_LINE("OR ∘ Identity -> OR");
    }
    return EF(thisFunc);
  }

  if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
    if constexpr (FDBG) {
      FDBG_LINE("OR ∘ Bottom -> Bottom");
    }
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
  }
  if (secondFunction.template isa<FeasibilityAllTopEF>()) {
    if constexpr (FDBG) {
      FDBG_LINE("OR ∘ Top -> Top");
    }
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }

  if constexpr (FDBG) {
    FDBG_LINE("OR ∘ <nontrivial> -> ComposeEF(lazy)");
  }

  auto *M = thisFunc->manager;
  return EF(std::in_place_type<FeasibilityComposeEF>, M, EF(thisFunc),
            secondFunction);
}

EF FeasibilityORFormulaEF::join(psr::EdgeFunctionRef<FeasibilityORFormulaEF> thisFunc,
                               const psr::EdgeFunction<l_t> &otherFunc) {
  ScopedTimer T("ORFormulaEF::join");

  if (otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
    if constexpr (FDBG) {
      FDBG_LINE("OR ⊔ Identity -> JoinEF(lazy)");
    }
    return EF(std::in_place_type<FeasibilityJoinEF>, thisFunc->manager,
              EF(thisFunc), EF(otherFunc));
  }

  if (otherFunc.template isa<FeasibilityAllTopEF>() ||
      otherFunc.template isa<psr::AllTop<l_t>>()) {
    if constexpr (FDBG) {
      FDBG_LINE("OR ⊔ Top -> Top");
    }
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }

  if (otherFunc.template isa<FeasibilityAllBottomEF>() ||
      otherFunc.template isa<psr::AllBottom<l_t>>()) {
    if constexpr (FDBG) {
      FDBG_LINE("OR ⊔ Bottom -> OR");
    }
    return EF(thisFunc);
  }

  if constexpr (FDBG) {
    if (auto *otherOr = otherFunc.template dyn_cast<FeasibilityORFormulaEF>()) {
      llvm::errs() << "[FDBG] OR ⊔ OR -> JoinEF(lazy)\n";
      llvm::errs() << "[FDBG] OR sizes: this=" << thisFunc->Clauses.size()
                   << " other=" << otherOr->Clauses.size() << "\n";
    } else {
      FDBG_LINE("OR ⊔ <nontrivial> -> JoinEF(lazy)");
    }
  }

  return EF(std::in_place_type<FeasibilityJoinEF>, thisFunc->manager,
            EF(thisFunc), EF(otherFunc));
}

// ============================================================================
// FeasibilityComposeEF

l_t FeasibilityComposeEF::computeTarget(const l_t &source) const {
  ScopedTimer T("ComposeEF::computeTarget");
  if (FDBG) dumpLatticeBrief("Compose.in ", source);

  FeasibilityElement cached;
  if (Memo.lookup(source, cached))
    return cached;

  // ✅ Critical: Bottom is absorbing -> don't evaluate anything
  if (source.isBottom()) {
    FDBG_LINE("Compose.computeTarget: source is Bottom -> return Bottom");
    return source; // already env=0 enforced elsewhere
  }

  const l_t mid = Second.computeTarget(source);
  if (FDBG) dumpLatticeBrief("Compose.mid", mid);

  // ✅ Also short-circuit if mid became Bottom (very common)
  if (mid.isBottom()) {
    FDBG_LINE("Compose.computeTarget: mid is Bottom -> return Bottom");
    Memo.store(source, mid);
    return mid;
  }

  const l_t out = First.computeTarget(mid);
  if (FDBG) dumpLatticeBrief("Compose.out", out);
  Memo.store(source, out);
  return out;
}

EF FeasibilityComposeEF::compose(psr::EdgeFunctionRef<FeasibilityComposeEF> thisFunc,
                                 const EF &secondFunction) {
  ScopedTimer T("ComposeEF::compose");

  if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
    FDBG_LINE("Compose ∘ Identity -> Compose");
    return EF(thisFunc);
  }
  if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
    FDBG_LINE("Compose ∘ Bottom -> Bottom");
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
  }
  if (secondFunction.template isa<FeasibilityAllTopEF>()) {
    FDBG_LINE("Compose ∘ Top -> Top");
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }

  auto *M = thisFunc->manager;
  FDBG_LINE("Compose ∘ h -> new ComposeEF (nest)");
  return EF(std::in_place_type<FeasibilityComposeEF>, M, EF(thisFunc),
            secondFunction);
}

EF FeasibilityComposeEF::join(psr::EdgeFunctionRef<FeasibilityComposeEF> thisFunc,
                              const psr::EdgeFunction<l_t> &otherFunc) {
  ScopedTimer T("ComposeEF::join");

  if (otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
    FDBG_LINE("Compose ⊔ Identity -> lazy JoinEF");
    return EF(std::in_place_type<FeasibilityJoinEF>, thisFunc->manager,
              EF(thisFunc), EF(otherFunc));
  }
  if (otherFunc.template isa<FeasibilityAllTopEF>() ||
      otherFunc.template isa<psr::AllTop<l_t>>()) {
    FDBG_LINE("Compose ⊔ Top -> Top");
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }
  if (otherFunc.template isa<FeasibilityAllBottomEF>() ||
      otherFunc.template isa<psr::AllBottom<l_t>>()) {
    FDBG_LINE("Compose ⊔ Bottom -> Compose");
    return EF(thisFunc);
  }

  FDBG_LINE("Compose ⊔ other -> lazy JoinEF");
  return EF(std::in_place_type<FeasibilityJoinEF>, thisFunc->manager,
            EF(thisFunc), EF(otherFunc));
}

// ============================================================================
// FeasibilityJoinEF

l_t FeasibilityJoinEF::computeTarget(const l_t &source) const {
  ScopedTimer T("JoinEF::computeTarget");
  if (FDBG) dumpLatticeBrief("Join.in ", source);

  FeasibilityElement cached;
  if (Memo.lookup(source, cached))
    return cached;

  // ✅ Critical: Bottom in -> Bottom out (your EFs already assume this)
  if (source.isBottom()) {
    FDBG_LINE("Join.computeTarget: source is Bottom -> return Bottom");
    return source;
  }

  auto *M = manager ? manager : source.getManager();

  const l_t L = Left.computeTarget(source);
  if (FDBG) dumpLatticeBrief("Join.L", L);
  if (L.isTop()) return mkTopLike(L);

  const l_t R = Right.computeTarget(source);
  if (FDBG) dumpLatticeBrief("Join.R", R);
  if (R.isTop()) return mkTopLike(R);

  if (L.isBottom()) return R;
  if (R.isBottom()) return L;

  const uint32_t outPC = M->mkOr(L.getFormulaId(), R.getFormulaId());
  auto out = source;
  out.setKind(l_t::Kind::Normal);
  out.setFormulaId(outPC);
  out.setEnvId(source.getEnvId());
  if (FDBG) dumpLatticeBrief("Join.out", out);
  Memo.store(source, out);
  return out;
}

EF FeasibilityJoinEF::compose(psr::EdgeFunctionRef<FeasibilityJoinEF> thisFunc,
                              const EF &secondFunction) {
  ScopedTimer T("JoinEF::compose");

  if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
    FDBG_LINE("Join ∘ Identity -> Join");
    return EF(thisFunc);
  }
  if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
    FDBG_LINE("Join ∘ Bottom -> Bottom");
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
  }
  if (secondFunction.template isa<FeasibilityAllTopEF>()) {
    FDBG_LINE("Join ∘ Top -> Top");
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }

  auto *M = thisFunc->manager;

  FDBG_LINE("Join ∘ h -> distribute lazily via ComposeEF");
  EF leftComposed =
      EF(std::in_place_type<FeasibilityComposeEF>, M, thisFunc->Left,
         secondFunction);
  EF rightComposed =
      EF(std::in_place_type<FeasibilityComposeEF>, M, thisFunc->Right,
         secondFunction);

  return EF(std::in_place_type<FeasibilityJoinEF>, M, std::move(leftComposed),
            std::move(rightComposed));
}

EF FeasibilityJoinEF::join(psr::EdgeFunctionRef<FeasibilityJoinEF> thisFunc,
                           const psr::EdgeFunction<l_t> &otherFunc) {
  ScopedTimer T("JoinEF::join");

  if (otherFunc.template isa<FeasibilityAllTopEF>() ||
      otherFunc.template isa<psr::AllTop<l_t>>()) {
    FDBG_LINE("Join ⊔ Top -> Top");
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }
  if (otherFunc.template isa<FeasibilityAllBottomEF>() ||
      otherFunc.template isa<psr::AllBottom<l_t>>()) {
    FDBG_LINE("Join ⊔ Bottom -> Join");
    return EF(thisFunc);
  }

  FDBG_LINE("Join ⊔ other -> flatten (new JoinEF)");
  return EF(std::in_place_type<FeasibilityJoinEF>, thisFunc->manager,
            EF(thisFunc), EF(otherFunc));
}

} // namespace Feasibility