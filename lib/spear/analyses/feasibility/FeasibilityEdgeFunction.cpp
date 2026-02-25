#include "analyses/feasibility/FeasibilityEdgeFunction.h"

#include "analyses/feasibility/FeasibilityAnalysisManager.h"

namespace Feasibility {

template <typename T, unsigned N>
static llvm::SmallVector<T, N> concatDedup(const llvm::SmallVector<T, N> &A,
                                           const llvm::SmallVector<T, N> &B) {
  llvm::SmallVector<T, N> Out;
  Out.append(A.begin(), A.end());
  Out.append(B.begin(), B.end());

  // dedup preserve order
  llvm::SmallVector<T, N> Dedup;
  for (const auto &x : Out) {
    if (!llvm::is_contained(Dedup, x))
      Dedup.push_back(x);
  }
  return Dedup;
}

template <typename T, unsigned N>
static llvm::SmallVector<T, N> intersectVec(const llvm::SmallVector<T, N> &A,
                                            const llvm::SmallVector<T, N> &B) {
  llvm::SmallVector<T, N> Out;
  for (const auto &x : A) {
    if (llvm::is_contained(B, x))
      Out.push_back(x);
  }
  // dedup preserve order
  llvm::SmallVector<T, N> Dedup;
  for (const auto &x : Out) {
    if (!llvm::is_contained(Dedup, x))
      Dedup.push_back(x);
  }
  return Dedup;
}

// ============================================================================
// FeasibilityAllBottomEF
// (optional: only meaningful if you really model a Bottom/false/error state)
// ============================================================================

l_t FeasibilityAllBottomEF::computeTarget(const l_t &source) const {
  auto *M = source.getManager();
  if (!M)
    return source;

  // If you keep Bottom in the lattice, it should remain Bottom.
  // env is kept (or you can reset; doesn't matter for "false").
  return l_t::createElement(M, l_t::bottomId, l_t::Kind::Bottom, source.getEnvId());
}

EF FeasibilityAllBottomEF::compose(psr::EdgeFunctionRef<FeasibilityAllBottomEF>,
                                   const EF & /*second*/) {
  // Bottom ∘ g = Bottom
  return EF(std::in_place_type<FeasibilityAllBottomEF>);
}

EF FeasibilityAllBottomEF::join(psr::EdgeFunctionRef<FeasibilityAllBottomEF>,
                                const psr::EdgeFunction<l_t> &otherFunc) {
  // pointwise join of transformers under MUST:
  // (⊥ ⊔ f)(x) = ⊥(x) ⊔ f(x)
  //
  // If Bottom is "false/error", then it is identity for intersection-join:
  //   false ∩ X = false   (absorbing)
  //
  // But many people use Bottom as "impossible" and treat it as identity for
  // reachability. Since you said "forget reachability", the safest choice is:
  // keep it absorbing for "false".
  //
  // If you don't need Bottom at all, delete this EF.
  (void)otherFunc;
  return EF(std::in_place_type<FeasibilityAllBottomEF>);
}

// ============================================================================
// FeasibilityAddAtomsEF  (Fork B, set-only semantics)
// ============================================================================

bool FeasibilityAddAtomsEF::operator==(const FeasibilityAddAtomsEF &o) const noexcept {
  if (atoms.size() != o.atoms.size())
    return false;
  for (size_t i = 0; i < atoms.size(); ++i) {
    if (!(atoms[i] == o.atoms[i]))
      return false;
  }
  return true;
}

l_t FeasibilityAddAtomsEF::computeTarget(const l_t &source) const {
  // Top (unknown) stays Top
  if (source.isTop())
    return source;

  // Bottom stays Bottom
  if (source.isBottom())
    return source;

  auto *M = Util::pickManager(manager, source);
  if (!M) return source;

  uint32_t env = source.getEnvId();
  uint32_t pc  = source.getFormulaId();

  for (const auto &A : atoms) {
    if (!A.icmp) continue;

    if (A.PredBB && A.SuccBB)
      env = manager->applyPhiPack(env, A.PredBB, A.SuccBB);

    z3::expr atom = Util::createConstraintFromICmp(M, A.icmp, A.TrueEdge, env);
    pc = manager->addAtom(pc, atom);
  }

  if (pc == l_t::topId)
    return l_t::createElement(M, l_t::topId, l_t::Kind::Empty, env);

  return l_t::createElement(M, pc, l_t::Kind::Normal, env);
}

EF FeasibilityAddAtomsEF::compose(psr::EdgeFunctionRef<FeasibilityAddAtomsEF> thisFunc,
                                  const EF &secondFunction) {
  // this ∘ g: apply g first, then apply this
  if (Util::isIdEF(secondFunction))
    return EF(thisFunc);

  if (Util::isAllBottomEF(secondFunction))
    return EF(std::in_place_type<FeasibilityAllBottomEF>);

  // AddAtoms ∘ AddAtoms = AddAtoms(concat atoms)
  if (auto *g = secondFunction.template dyn_cast<FeasibilityAddAtomsEF>()) {
    auto merged = concatDedup<LazyAtom, 4>(g->atoms, thisFunc->atoms);
    if (merged.empty())
      return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
    return EF(std::in_place_type<FeasibilityAddAtomsEF>, thisFunc->manager, std::move(merged));
  }

  // Unknown EF type:
  // In set-only MUST world, "guaranteed delta" of an unknown transformer is none.
  // Returning Identity is the safe "adds nothing" fallback.
  return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
}

EF FeasibilityAddAtomsEF::join(psr::EdgeFunctionRef<FeasibilityAddAtomsEF> thisFunc,
                               const psr::EdgeFunction<l_t> &otherFunc) {
  const EF B(otherFunc);

  // Join with Identity:
  // (add A) ⊔ (id) guarantees nothing in common => Identity
  if (Util::isIdEF(B))
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);

  if (Util::isAllBottomEF(B))
    return EF(std::in_place_type<FeasibilityAllBottomEF>);

  // AddAtoms ⊔ AddAtoms = AddAtoms(intersection of atoms)
  if (auto *g = otherFunc.template dyn_cast<FeasibilityAddAtomsEF>()) {
    auto inter = intersectVec<LazyAtom, 4>(thisFunc->atoms, g->atoms);
    if (inter.empty())
      return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
    return EF(std::in_place_type<FeasibilityAddAtomsEF>, thisFunc->manager, std::move(inter));
  }

  // Unknown other EF: DO NOT return "force Top".
  // "No guaranteed atoms" = Identity.
  return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
}

} // namespace Feasibility