/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_FEASIBILITYANALYSISMANAGER_H
#define SPEAR_FEASIBILITYANALYSISMANAGER_H

#include "FeasibilityElement.h"
#include "FeasibilityEnvironment.h"

namespace Feasibility {

/// Comparator for z3::expr so we can store them in std::set.
/// We order by Z3 AST id, which is stable within a context.
struct ExprLess {
    bool operator()(const z3::expr &a, const z3::expr &b) const noexcept {
        // Both expr must come from the same context (true for one manager).
        unsigned ia = Z3_get_ast_id(a.ctx(), a);
        unsigned ib = Z3_get_ast_id(b.ctx(), b);
        return ia < ib;
    }
};

// ============================================================================
// FeasibilityAnalysisManager
// ============================================================================
class FeasibilityAnalysisManager {
public:
    using l_t = Feasibility::FeasibilityElement;
    using EF = psr::EdgeFunction<l_t>;
    using EFStableId = std::uint64_t;

    using ExprSet = std::set<z3::expr, ExprLess>;

    explicit FeasibilityAnalysisManager(std::unique_ptr<z3::context> ctx);

    // --------------------------------------------------------------------------
    // Formula storage (conjunction sets)
    // --------------------------------------------------------------------------
    const ExprSet &getSet(uint32_t id) const;

    /// Intern a set, return its ID.
    uint32_t internSet(const ExprSet &S);

    /// Create ID for (Set[id] ∪ {atom})
    uint32_t addAtom(uint32_t baseId, const z3::expr &atom);

    /// Create ID for (Set[a] ∩ Set[b])
    uint32_t intersect(uint32_t aId, uint32_t bId);

    /// Optional helper (slow path): find a singleton set's id
    std::optional<uint32_t> findSingletonId(const z3::expr &atom) const;

    std::vector<z3::expr> getPureSet(uint32_t id) const;

    bool hasEnv(uint32_t id) const noexcept;

    /// Direct lookup: returns bound value if present, else nullptr
    const llvm::Value *lookupEnv(uint32_t envId, const llvm::Value *k) const;

    /// Resolves a value through the environment chain (v -> binding -> binding...)
    const llvm::Value *resolve(uint32_t envId, const llvm::Value *v) const;

    /// Create/return envId for baseEnvId extended with (k -> v)
    uint32_t extendEnv(uint32_t baseEnvId,
                       const llvm::Value *k,
                       const llvm::Value *v);

    /// Apply all PHIs in 'succ' for the incoming edge pred->succ
    uint32_t applyPhiPack(uint32_t inEnvId,
                          const llvm::BasicBlock *pred,
                          const llvm::BasicBlock *succ);

    std::unique_ptr<z3::context> Context;
    z3::solver Solver;

private:
    // Sets[0] = empty set == true (Top)
    // Sets[1] = reserved (bottomId) (we keep empty set as placeholder)
    std::vector<ExprSet> Sets;

    void ensureEnvZeroInitialized();

    std::deque<EnvNode> EnvPool;
    std::vector<const EnvNode *> EnvRoots; // EnvRoots[0] == nullptr

    // Optional, but recommended: env interning avoids creating duplicate env nodes
    std::unordered_map<EnvKey, uint32_t, EnvKeyHash> EnvIntern;
    mutable std::mutex EnvInternMu;

    // Interning table: hash(set) -> id. We still validate equality on collision.
    mutable std::mutex SetsMutex;
    std::unordered_map<std::size_t, std::vector<uint32_t>> InternBuckets;

    static std::size_t hashSet(const ExprSet &S);
};

} // namespace Feasibility

#endif // SPEAR_FEASIBILITYANALYSISMANAGER_H