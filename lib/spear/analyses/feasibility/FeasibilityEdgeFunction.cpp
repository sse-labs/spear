/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <utility>

#include "analyses/feasibility/FeasibilityEdgeFunction.h"
#include "analyses/feasibility/FeasibilityAnalysisManager.h"

namespace Feasibility {

/**
 * Utility function to concat two SmallVectors and deduplicate the result while preserving order.
 * This is used to merge the atom lists of two AddAtoms edge functions during composition.
 * @tparam T Smallvector element type
 * @tparam N Number of inline elements in the SmallVector
 * @param A First SmallVector to concatenate
 * @param B Second SmallVector to concatenate
 * @return Concatenation of A and B, with duplicates removed and original order preserved
 */
template <typename T, unsigned N>
static llvm::SmallVector<T, N> concatDedup(const llvm::SmallVector<T, N> &A, const llvm::SmallVector<T, N> &B) {
  // Concat the elements of A and B into a new vector
  llvm::SmallVector<T, N> Out;
  Out.append(A.begin(), A.end());
  Out.append(B.begin(), B.end());

  // Create an instance of llvm::SmallVector to hold the deduplicated result
  llvm::SmallVector<T, N> Dedup;
  for (const auto &x : Out) {
    if (!llvm::is_contained(Dedup, x))
      Dedup.push_back(x);
  }

  return Dedup;
}

/**
 * Utility function to compute the intersection of two SmallVectors while preserving order and deduplicating the result.
 * @tparam T Smallvector element type
 * @tparam N Number of inline elements in the SmallVector
 * @param A First SmallVector to intersect
 * @param B Second SmallVector to intersect
 * @return Intersection of A and B, with duplicates removed and original order preserved
 */
template <typename T, unsigned N>
static llvm::SmallVector<T, N> intersectVec(const llvm::SmallVector<T, N> &A, const llvm::SmallVector<T, N> &B) {
  // Compute the intersection of A and B by checking which elements of A are contained in B
  llvm::SmallVector<T, N> Out;
  for (const auto &x : A) {
    if (llvm::is_contained(B, x))
      Out.push_back(x);
  }

  // Deduplicate
  llvm::SmallVector<T, N> Dedup;
  for (const auto &x : Out) {
    if (!llvm::is_contained(Dedup, x))
      Dedup.push_back(x);
  }

  return Dedup;
}

/**
 * FeasibilityAllBottomEF
 */

l_t FeasibilityAllBottomEF::computeTarget(const l_t &source) const {
  auto *manager = source.getManager();
  if (!manager) {
    return source;
  }

  // Bottom stays bottom
  return l_t::createElement(manager, l_t::bottomId, l_t::Kind::Bottom, source.getEnvId());
}

EF FeasibilityAllBottomEF::compose(psr::EdgeFunctionRef<FeasibilityAllBottomEF>, const EF & /*second*/) {
  // Bottom ∘ g = Bottom
  return EF(std::in_place_type<FeasibilityAllBottomEF>);
}

EF FeasibilityAllBottomEF::join(psr::EdgeFunctionRef<FeasibilityAllBottomEF>, const psr::EdgeFunction<l_t> &otherFunc) {
  // Bottom ⊔ g = Bottom
  return EF(std::in_place_type<FeasibilityAllBottomEF>);
}

/**
 * FeasibilityAddAtomsEF
 */

l_t FeasibilityAddAtomsEF::computeTarget(const l_t &source) const {
  // Bottom stays Bottom
  if (source.isBottom()) {
    return source;
  }

  // Pick the right manager instance
  auto *localmanager = Util::pickManager(manager, source);
  if (!localmanager) {
    return source;
  }

  // Query env and pc from the source element, which we will update with the new atoms
  uint32_t env = source.getEnvId();
  uint32_t pc  = source.getFormulaId();

  // Iterate over the atoms
  for (const auto &singleAtom : atoms) {
    // If the atom does not have an associated ICmp instruction, we cannot create a constraint for it, so we skip it.
    if (!singleAtom.icmp) {
      // This case should never occur
      continue;
    }

    // If we have predecessor and successor blocks, we need to apply phi packing to the environment before
    // creating the constraint.
    if (singleAtom.PredBB && singleAtom.SuccBB) {
      env = localmanager->applyPhiPack(env, singleAtom.PredBB, singleAtom.SuccBB);
    }

    // Create a new atomic expression
    z3::expr atom = Util::createConstraintFromICmp(manager, singleAtom.icmp, singleAtom.TrueEdge, env);
    pc = manager->addAtom(pc, atom);
  }

  // If the resulting element is the same as the Top element (empty set of formulas), we return an
  // explicit Empty element to represent this.
  if (pc == l_t::topId) {
    return l_t::createElement(manager, l_t::topId, l_t::Kind::Empty, env);
  }

  // In any other case return the actual Normal element with the new formula ID and environment ID.
  return l_t::createElement(manager, pc, l_t::Kind::Normal, env);
}

EF FeasibilityAddAtomsEF::compose(psr::EdgeFunctionRef<FeasibilityAddAtomsEF> thisFunc, const EF &secondFunction) {
  // this ∘ g: apply g first, then apply this

  // ignore identity
  if (Util::isIdEF(secondFunction)) {
    return EF(thisFunc);
  }

  // Bottom dominates
  if (Util::isAllBottomEF(secondFunction)) {
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
  }

  // AddAtoms ∘ AddAtoms = AddAtoms(concat atoms)
  if (auto *g = secondFunction.template dyn_cast<FeasibilityAddAtomsEF>()) {
    // Concat the two vectors
    auto merged = concatDedup<LazyAtom, 4>(g->atoms, thisFunc->atoms);
    // Check if the resulting vector is empty, which means the resulting function is Identity (adds no atoms)
    if (merged.empty()) {
      // This can only happen if both this and g have empty atom lists, which means they are both Identity functions.
      return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
    }

    // Otherwise, return a new AddAtoms function with the merged atom list.
    return EF(std::in_place_type<FeasibilityAddAtomsEF>, thisFunc->manager, std::move(merged));
  }

  // Unknown EF type:
  // Returning Identity is the safe "adds nothing" fallback.
  return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
}

EF FeasibilityAddAtomsEF::join(psr::EdgeFunctionRef<FeasibilityAddAtomsEF> thisFunc,
                               const psr::EdgeFunction<l_t> &otherFunc) {
  // Join with Identity does nothing
  if (Util::isIdEF(otherFunc)) {
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
  }

  // Bottom dominates the join
  if (Util::isAllBottomEF(otherFunc)) {
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
  }

  // AddAtoms ⊔ AddAtoms = AddAtoms(intersection of atoms)
  if (auto *g = otherFunc.template dyn_cast<FeasibilityAddAtomsEF>()) {
    // Calculate intersection of sets
    auto inter = intersectVec<LazyAtom, 4>(thisFunc->atoms, g->atoms);
    if (inter.empty()) {
      // If the intersection is empty, the resulting function adds no atoms, which means it is the Identity function.
      return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
    }
    return EF(std::in_place_type<FeasibilityAddAtomsEF>, thisFunc->manager, std::move(inter));
  }

  // Unknown other EF
  return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
}

bool FeasibilityAddAtomsEF::operator==(const FeasibilityAddAtomsEF &o) const noexcept {
  // If both atoms sizes differ they cannot be equal
  if (atoms.size() != o.atoms.size()) {
    return false;
  }

  // Otherwise check componentwise
  for (size_t i = 0; i < atoms.size(); ++i) {
    if (!(atoms[i] == o.atoms[i])) {
      return false;
    }
  }

  return true;
}

}  // namespace Feasibility
