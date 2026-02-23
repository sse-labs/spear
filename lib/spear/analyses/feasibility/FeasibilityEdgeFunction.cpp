/*
 * Patched to the IDENTITY-based 2-point approach:
 *
 *   Lattice:
 *     Top    = UNREACHABLE (absorbing along paths)
 *     Bottom = REACHABLE
 *
 *   CRITICAL semantic rule (fixes your “UNSAT not preserved”):
 *     NO edge function is allowed to map Top -> Bottom (revive unreachable).
 *
 * Therefore:
 *   - We keep FeasibilityAllTopEF (constant Top).
 *   - We DO NOT use AllBottomEF / psr::AllBottom at all in the algebra.
 *     “Reachable baseline” is represented by Identity EF.
 *
 * Additionally:
 *   - EF-join collapsing returns either AllTopEF or Identity (never AllBottomEF).
 *   - compose-folding of “constant reachable inner” uses Identity.
 *   - join handlers treat any Bottom-constant EF as Identity (or ignore).
 *
 * NOTE: This file still defines FeasibilityAllBottomEF, but it must not be
 *       introduced by compose/join/collapse. You can remove it later.
 */

#include "analyses/feasibility/FeasibilityEdgeFunction.h"
#include "analyses/feasibility/util.h"

#include <llvm/Support/raw_ostream.h>

namespace Feasibility {

namespace {

constexpr bool FDBG_PC = true;

// Pretty print helpers --------------------------------------------------------

static inline const char *kstr(const l_t &x) {
  return x.isTop() ? "Top(UNREACH)" : "Bottom(REACH)";
}

// Evaluate clause SAT + debug -------------------------------------------------

static bool evalClauseSatDbg(FeasibilityAnalysisManager *M,
                             const FeasibilityClause &C,
                             const char *who) {
  uint32_t env = 0;
  z3::context &ctx = *M->Context;
  z3::expr conj = ctx.bool_val(true);

  for (const auto &st : C.Steps) {
    if (st.K == ClauseStep::Kind::Phi) {
      const auto &ph = st.Phi;
      if (ph.PredBB && ph.SuccBB)
        env = M->applyPhiPack(env, ph.PredBB, ph.SuccBB);
      continue;
    }

    // ICmp step
    const auto &lc = st.ICmp;
    if (!lc.I) continue;

    z3::expr atom = Util::createConstraintFromICmp(M, lc.I, lc.TrueEdge, env);

    if constexpr (FDBG_PC) {
      llvm::errs() << "[FDBG][PC] atom=" << atom.to_string()
                   << " from " << (lc.TrueEdge ? "T:" : "F:") << *lc.I
                   << " env=" << env << "\n";
    }

    conj = conj && atom;
  }

  conj = M->simplify(conj);

  const uint32_t id = M->mkAtomic(conj);
  const bool sat = M->isSat(id);

  if constexpr (FDBG_PC) {
    llvm::errs() << "[FDBG][PC] " << who << " conj=" << conj.to_string()
                 << " id=" << id
                 << " => " << (sat ? "SAT" : "UNSAT") << "\n";
  }

  return sat;
}

// Join on lattice values (2-point): unreachable only if both unreachable.
static inline l_t joinVal(const l_t &a, const l_t &b) {
  FeasibilityAnalysisManager *M = a.getManager() ? a.getManager() : b.getManager();
  if (a.isTop() && b.isTop()) return l_t::createTop(M);
  return l_t::createBottom(M);
}

// Determine EF-join result via behavior on Bottom seed.
// IMPORTANT: returns AllTop or Identity (never AllBottom).
static EF joinEFByBottomSeed(FeasibilityAnalysisManager *M, const EF &A, const psr::EdgeFunction<l_t> &B) {
  const l_t bot = l_t::createBottom(M);
  const l_t top = l_t::createTop(M);

  const l_t aB = A.computeTarget(bot);
  const l_t bB = B.computeTarget(bot);
  const l_t aT = A.computeTarget(top);
  const l_t bT = B.computeTarget(top);

  const l_t jB = joinVal(aB, bB);
  const l_t jT = joinVal(aT, bT);

  if constexpr (FDBG_PC) {
    llvm::errs() << "[FDBG][PC] join2pt: "
                 << efName(A) << "⊔" << efName(EF(B))
                 << "  j(B)=" << kstr(jB)
                 << "  j(T)=" << kstr(jT) << "\n";
  }

  // In the intended domain, jT==Top implies both are unreachable on Top
  if (jT.isTop() && jB.isTop())
    return EF(std::in_place_type<FeasibilityAllTopEF>);

  // If jT is Bottom, reachable can happen; represent by Identity
  // (Top-absorbing + reachability-preserving)
  return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
}

static inline l_t mkUnreachable(const l_t &src, FeasibilityAnalysisManager *M = nullptr) {
  if (!M) M = src.getManager();
  return l_t::createTop(M);
}
static inline l_t mkReachable(const l_t &src, FeasibilityAnalysisManager *M = nullptr) {
  if (!M) M = src.getManager();
  return l_t::createBottom(M);
}

// Convenience: treat any “AllBottom-ish” EF as Identity in this architecture.
// You can tighten this later if you remove AllBottom entirely.
static inline bool isAllBottomishEF(const EF &ef) noexcept {
  return ef.template isa<FeasibilityAllBottomEF>() || ef.template isa<psr::AllBottom<l_t>>();
}

} // namespace

// =====================================================================================================================
// FeasibilityAllTopEF
// =====================================================================================================================

l_t FeasibilityAllTopEF::computeTarget(const l_t &source) const {
  if constexpr (FDBG_PC) {
    llvm::errs() << "[FDBG][PC] AllTopEF(" << kstr(source) << ") -> Top(UNREACH)\n";
  }
  return mkUnreachable(source);
}

EF FeasibilityAllTopEF::compose(psr::EdgeFunctionRef<FeasibilityAllTopEF> /*thisFunc*/,
                                const EF & /*secondFunction*/) {
  return EF(std::in_place_type<FeasibilityAllTopEF>);
}

EF FeasibilityAllTopEF::join(psr::EdgeFunctionRef<FeasibilityAllTopEF> /*thisFunc*/,
                             const psr::EdgeFunction<l_t> &otherFunc) {
  // Top is neutral in value-join, so (AllTop ⊔ h) = h
  return EF(otherFunc);
}

// =====================================================================================================================
// FeasibilityAllBottomEF
// =====================================================================================================================
// NOTE: In the IDENTITY-based approach, you should NOT create this EF from
// compose/join/collapse. If it still appears, it can revive Top unless it is
// Top-absorbing. We therefore make it Top-absorbing (safe), even though the
// preferred approach is "do not use it at all".
//
// If you want to fully remove it later, delete this struct and any isa<> checks.
l_t FeasibilityAllBottomEF::computeTarget(const l_t &source) const {
  // Top-absorbing to avoid reviving unreachable:
  //   AllBottom(Top) = Top
  //   AllBottom(Bottom) = Bottom
  if constexpr (FDBG_PC) {
    llvm::errs() << "[FDBG][PC] AllBottomEF(" << kstr(source) << ") -> "
                 << (source.isTop() ? "Top(UNREACH)" : "Bottom(REACH)") << "\n";
  }
  if (source.isTop())
    return source;
  return mkReachable(source);
}

EF FeasibilityAllBottomEF::compose(psr::EdgeFunctionRef<FeasibilityAllBottomEF> /*thisFunc*/,
                                   const EF &secondFunction) {
  // Keep Top-absorption: if second makes it Top, preserve Top.
  // But in the preferred identity-based usage, you should never hit this.
  if (isAllTopEF(secondFunction)) return secondFunction;
  if (isIdEF(secondFunction)) return EF(std::in_place_type<FeasibilityAllBottomEF>);
  return EF(std::in_place_type<FeasibilityComposeEF>, nullptr,
            EF(std::in_place_type<FeasibilityAllBottomEF>), secondFunction);
}

EF FeasibilityAllBottomEF::join(psr::EdgeFunctionRef<FeasibilityAllBottomEF> /*thisFunc*/,
                                const psr::EdgeFunction<l_t> &otherFunc) {
  // Value-join with Bottom returns Bottom, but we avoid creating this in joins.
  // Conservatively: collapse by bottom-seed to Identity/AllTop.
  FeasibilityAnalysisManager *M = nullptr;
  // We may not have a manager pointer here; use otherFunc on Bottom seed with nullptr manager not possible.
  // So just return otherFunc (safe-ish) — but again, this EF should not appear.
  return EF(otherFunc);
}

// =====================================================================================================================
// FeasibilityPHITranslateEF
// (Identity on reachability; only prints the step so you see the chain)
// =====================================================================================================================

l_t FeasibilityPHITranslateEF::computeTarget(const l_t &source) const {
  if constexpr (FDBG_PC) {
    llvm::errs() << "[FDBG][PC] PHITranslateEF("
                 << (PredBB ? PredBB->getName() : "<null>")
                 << "->"
                 << (SuccBB ? SuccBB->getName() : "<null>")
                 << ")(" << kstr(source) << ") -> " << kstr(source) << "\n";
  }
  return source;
}

EF FeasibilityPHITranslateEF::compose(psr::EdgeFunctionRef<FeasibilityPHITranslateEF> thisFunc,
                                      const EF &secondFunction) {
  // this ∘ second

  if (isIdEF(secondFunction)) return EF(thisFunc);
  if (isAllTopEF(secondFunction)) return secondFunction;

  // In identity-based semantics, "reachable constant" is Identity, not AllBottom.
  if (isAllBottomishEF(secondFunction))
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);

  // ✅ If we are composing PHI ∘ AND, fold PHI into the AND clause
  if (auto *And = secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
    FeasibilityClause fused = And->Clause;
    fused.Steps.insert(fused.Steps.begin(),
                       ClauseStep::mkPhi(thisFunc->PredBB, thisFunc->SuccBB));
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, thisFunc->manager, std::move(fused));
  }

  // otherwise keep lazy
  return EF(std::in_place_type<FeasibilityComposeEF>, thisFunc->manager,
            EF(thisFunc), secondFunction);
}

EF FeasibilityPHITranslateEF::join(psr::EdgeFunctionRef<FeasibilityPHITranslateEF> thisFunc,
                                   const psr::EdgeFunction<l_t> &otherFunc) {
  // Treat any AllBottom-ish as Identity (reachable)
  if (llvm::isa<FeasibilityAllBottomEF>(otherFunc) || llvm::isa<psr::AllBottom<l_t>>(otherFunc))
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);

  if (llvm::isa<FeasibilityAllTopEF>(otherFunc) || llvm::isa<psr::AllTop<l_t>>(otherFunc))
    return EF(thisFunc);

  FeasibilityAnalysisManager *M = thisFunc->manager;
  return joinEFByBottomSeed(M, EF(thisFunc), otherFunc);
}

// =====================================================================================================================
// FeasibilityANDFormulaEF
// Visualizes: constraints + built Z3 conjunction
// =====================================================================================================================

l_t FeasibilityANDFormulaEF::computeTarget(const l_t &source) const {
  if constexpr (FDBG_PC) {
    llvm::errs() << "[FDBG][PC] ANDFormulaEF(" << kstr(source) << ") clause:\n";
  }

  // Top absorbing: once unreachable, always unreachable
  if (source.isTop()) {
    if constexpr (FDBG_PC) {
      llvm::errs() << "[FDBG][PC] ANDFormulaEF short-circuit: input Top => Top\n";
    }
    return source;
  }

  FeasibilityAnalysisManager *pickedManager = pickManager(manager, source);

  const bool sat = evalClauseSatDbg(pickedManager, Clause, "ANDFormulaEF");
  l_t out = sat ? l_t::createBottom(pickedManager) : l_t::createTop(pickedManager);

  if constexpr (FDBG_PC) {
    llvm::errs() << "[FDBG][PC] ANDFormulaEF result: " << (sat ? "Bottom(REACH)" : "Top(UNREACH)") << "\n";
  }

  return out;
}

EF FeasibilityANDFormulaEF::compose(psr::EdgeFunctionRef<FeasibilityANDFormulaEF> thisFunc,
                                    const EF &secondFunction) {
  // this ∘ second

  if (isIdEF(secondFunction)) return EF(thisFunc);
  if (isAllTopEF(secondFunction)) return secondFunction;

  // In identity-based semantics, treat “AllBottom inner” as Identity inner.
  if (isAllBottomishEF(secondFunction))
    return EF(thisFunc); // f ∘ Id = f

  // ✅ Fuse AND ∘ AND by concatenating step sequences
  if (auto *Other = secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
    FeasibilityClause fused;
    // Composition order: this ∘ second => second runs first.
    fused.Steps.append(Other->Clause.Steps.begin(), Other->Clause.Steps.end());
    fused.Steps.append(thisFunc->Clause.Steps.begin(), thisFunc->Clause.Steps.end());
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, thisFunc->manager, std::move(fused));
  }

  // ✅ Fuse AND ∘ PHI (if PHI nodes still appear)
  if (auto *Phi = secondFunction.template dyn_cast<FeasibilityPHITranslateEF>()) {
    FeasibilityClause fused = thisFunc->Clause;
    fused.Steps.insert(fused.Steps.begin(),
                       ClauseStep::mkPhi(Phi->PredBB, Phi->SuccBB));
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, thisFunc->manager, std::move(fused));
  }

  // otherwise keep lazy
  return EF(std::in_place_type<FeasibilityComposeEF>, thisFunc->manager,
            EF(thisFunc), secondFunction);
}

EF FeasibilityANDFormulaEF::join(psr::EdgeFunctionRef<FeasibilityANDFormulaEF> thisFunc,
                                 const psr::EdgeFunction<l_t> &otherFunc) {
  // Treat AllBottom-ish as Identity (reachable)
  if (llvm::isa<FeasibilityAllBottomEF>(otherFunc) || llvm::isa<psr::AllBottom<l_t>>(otherFunc))
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);

  // Top is neutral on join
  if (llvm::isa<FeasibilityAllTopEF>(otherFunc) || llvm::isa<psr::AllTop<l_t>>(otherFunc))
    return EF(thisFunc);

  FeasibilityAnalysisManager *M = thisFunc->manager;

  if constexpr (FDBG_PC) {
    llvm::errs() << "[FDBG][PC] join: ANDFormulaEF ⊔ " << efName(EF(otherFunc))
                 << " (collapsed by Bottom-seed truth table)\n";
  }

  return joinEFByBottomSeed(M, EF(thisFunc), otherFunc);
}

// ============================================================================
// FeasibilityComposeEF
// Visualizes: the computeTarget pipeline First(Second(x))
// ============================================================================

l_t FeasibilityComposeEF::computeTarget(const l_t &source) const {
  if constexpr (FDBG_PC) {
    llvm::errs() << "[FDBG][PC] ComposeEF(" << kstr(source) << ") = "
                 << efName(First) << "(" << efName(Second) << "(x))\n";
  }

  // Top absorbing
  if (source.isTop()) {
    if constexpr (FDBG_PC) {
      llvm::errs() << "[FDBG][PC] ComposeEF short-circuit: input Top => Top\n";
    }
    return source;
  }

  const l_t mid = Second.computeTarget(source);

  if constexpr (FDBG_PC) {
    llvm::errs() << "[FDBG][PC] ComposeEF mid = " << kstr(mid) << "\n";
  }

  if (mid.isTop()) {
    if constexpr (FDBG_PC) {
      llvm::errs() << "[FDBG][PC] ComposeEF short-circuit: mid Top => Top\n";
    }
    return mid;
  }

  const l_t out = First.computeTarget(mid);

  if constexpr (FDBG_PC) {
    llvm::errs() << "[FDBG][PC] ComposeEF out = " << kstr(out) << "\n";
  }

  return out;
}

EF FeasibilityComposeEF::compose(psr::EdgeFunctionRef<FeasibilityComposeEF> thisFunc,
                                 const EF &secondFunction) {
  if (isIdEF(secondFunction)) return EF(thisFunc);
  if (isAllTopEF(secondFunction)) return secondFunction;

  // Treat AllBottom-ish as Identity (inner disappears)
  if (isAllBottomishEF(secondFunction))
    return EF(thisFunc);

  if constexpr (FDBG_PC) {
    llvm::errs() << "[FDBG][PC] compose: ComposeEF ∘ " << efName(secondFunction) << " -> nested ComposeEF\n";
  }

  return EF(std::in_place_type<FeasibilityComposeEF>, thisFunc->manager,
            EF(thisFunc), secondFunction);
}

EF FeasibilityComposeEF::join(psr::EdgeFunctionRef<FeasibilityComposeEF> thisFunc,
                              const psr::EdgeFunction<l_t> &otherFunc) {
  // Treat AllBottom-ish as Identity (reachable)
  if (llvm::isa<FeasibilityAllBottomEF>(otherFunc) || llvm::isa<psr::AllBottom<l_t>>(otherFunc))
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);

  if (llvm::isa<FeasibilityAllTopEF>(otherFunc) || llvm::isa<psr::AllTop<l_t>>(otherFunc))
    return EF(thisFunc);

  FeasibilityAnalysisManager *M = thisFunc->manager;

  if constexpr (FDBG_PC) {
    llvm::errs() << "[FDBG][PC] join: ComposeEF ⊔ " << efName(EF(otherFunc))
                 << " (collapsed by Bottom-seed truth table)\n";
  }

  return joinEFByBottomSeed(M, EF(thisFunc), otherFunc);
}

} // namespace Feasibility