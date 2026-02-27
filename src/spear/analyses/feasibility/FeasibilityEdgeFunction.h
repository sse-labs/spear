/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYEDGEFUNCTION_H_
#define SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYEDGEFUNCTION_H_

#include <phasar/DataFlow/IfdsIde/EdgeFunction.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>

#include <utility>

#include "FeasibilityElement.h"
#include "analyses/feasibility/util.h"

namespace Feasibility {

using l_t = FeasibilityElement;
using EF = psr::EdgeFunction<l_t>;

/**
 * AllBottomEF realizes the unreachable error state of the analysis
 */
struct FeasibilityAllBottomEF {
    using l_t = FeasibilityElement;

    /**
     * Update the lattice element
     * @param source Current state of the lattice element
     * @return Updates state of the lattice element
     */
    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    /**
     * Combine two edge functions along a path creating h(x) = f(x) o g(x). f(x) being this function
     * @param secondFunction g(x)
     * @return h(x)
     */
    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAllBottomEF>, const EF &secondFunction);

    /**
     * Combines two EFs using the join operation h(x) = f(x) ⊔ g(x). f(x) being this function
     * @param otherFunc g(x)
     * @return h(x)
     */
    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAllBottomEF>, const psr::EdgeFunction<l_t> &otherFunc);

    /**
     * Compare this EF with another one for equality. Since this EF is a singleton, we can just check the type.
     * @return true if equal, false otherwise
     */
    bool operator==(const FeasibilityAllBottomEF &) const = default;

    /**
     * Constant EFs are those that always return the same value regardless of the input.
     * Since this EF always returns Bottom, it is a constant EF.
     * @return true as this is a constant EF.
     */
    bool isConstant() const noexcept { return true; }
};

/**
 * FeasibilityAddAtomsEF implements LazyAtom handling along paths
 */
struct FeasibilityAddAtomsEF {
    using l_t = FeasibilityElement;

    /**
     * Local representation of the manager
     */
    FeasibilityAnalysisManager *manager = nullptr;

    /**
     * Internal atom storage. Stores at least 4 atoms inside the object itself. All other elements are
     * allocated normally on the heap
     */
    llvm::SmallVector<LazyAtom, 4> atoms;

    /*
     * Dummy default constructor
     */
    FeasibilityAddAtomsEF() = default;

    /**
     * Data initializing constructor
     * @param manager FeasibilityAnalysisManager instance the analysis is refering to
     * @param atoms List of atoms available to the EF
     */
    FeasibilityAddAtomsEF(FeasibilityAnalysisManager *manager, llvm::SmallVector<LazyAtom, 4> atoms)
    : manager(manager), atoms(std::move(atoms)) {}

    /**
     * Create the EF with a single new atom created from the passed edge information.
     * @param manager Manager the EF operates on
     * @param predecessor Predecessor BasicBlock of the EF
     * @param sucessor Successor BasicBlock o the EF
     * @param icmp ICMP instruction the edge is based upon
     * @param areWeOnTheTrueEdge Flag to signal whether this is the true or the false branch
     */
    FeasibilityAddAtomsEF(FeasibilityAnalysisManager *manager,
                          const llvm::BasicBlock *predecessor,
                          const llvm::BasicBlock *sucessor,
                          const llvm::ICmpInst *icmp,
                          bool areWeOnTheTrueEdge)
    : manager(manager) {
        atoms.emplace_back(predecessor, sucessor, icmp, areWeOnTheTrueEdge);
    }

    /**
     * Update the lattice element
     * @param source Current state of the lattice element
     * @return Updates state of the lattice element
     */
    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    /**
     * Combine two edge functions along a path creating h(x) = f(x) o g(x). f(x) being this function
     * @param secondFunction g(x)
     * @return h(x)
     */
    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAddAtomsEF> thisFunc, const EF &secondFunction);

    /**
     * Compare this EF with another one for equality. Since this EF is a singleton, we can just check the type.
     * @return true if equal, false otherwise
     */
    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAddAtomsEF> thisFunc,
                               const psr::EdgeFunction<l_t> &otherFunc);

    /**
     * Compare this EF with another one for equality
     * @return true if equal, false otherwise
     */
    bool operator==(const FeasibilityAddAtomsEF &) const noexcept;

    /**
     * Constant EFs are those that always return the same value regardless of the input.
     * Since the values represented by this EF change depending on the input (the source lattice element),
     * it is not a constant EF.
     * @return true as this is a constant EF.
     */
    bool isConstant() const noexcept { return false; }
};


}  // namespace Feasibility

#endif  // SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYEDGEFUNCTION_H_

