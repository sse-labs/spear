/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_FEASIBILITYANALYSISMANAGER_H
#define SPEAR_FEASIBILITYANALYSISMANAGER_H

#include "FeasibilityElement.h"
#include "FeasibilityEnvironment.h"

namespace Feasibility {

/**
 * Expression comparator for z3::expr, used for storing expressions in sorted containers (e.g., std::set).
 * The comparator uses the AST id of the expressions to determine their order.
 */
struct ExpressionComperator {
    bool operator()(const z3::expr &a, const z3::expr &b) const noexcept {
        // Both expr must come from the same context, so we can compare their AST ids directly.
        unsigned ia = Z3_get_ast_id(a.ctx(), a);
        unsigned ib = Z3_get_ast_id(b.ctx(), b);
        return ia < ib;
    }
};

/**
 * FeasibilityAnalysisManager is the central component for managing the state of the feasibility analysis, including
 * the storage of formulas (as sets of atomic formulas) and environments (variable bindings). It
 * provides methods for interning formula sets, extending environments, and applying PHI node effects on environments.
 * The manager also holds the Z3 context and solver used for formula manipulation and satisfiability checking.
 */
class FeasibilityAnalysisManager {
public:
    using l_t = Feasibility::FeasibilityElement;
    using EF = psr::EdgeFunction<l_t>;

    // Definition of the expression set type
    using ExprSet = std::set<z3::expr, ExpressionComperator>;

    /**
     * Default constructor for FeasibilityAnalysisManager.
     * Initializes the Z3 context and solver, and reserves initial IDs for the formula sets.
     * @param ctx Context for Z3 expressions, passed as a unique pointer to ensure proper ownership
     * and lifetime management.
     */
    explicit FeasibilityAnalysisManager(std::unique_ptr<z3::context> ctx);

    /**
     * Get the set of atomic formulas corresponding to the given ID.
     * @param id Id to query
     * @return Formula set corresponding to the given ID.
     */
    const ExprSet &getSet(const uint32_t id) const {
        return Sets.at(id);
    }

    /**
     * Get the Z3 context used by this manager. This is needed for creating and manipulating Z3 expressions.
     * @return Const reference to the Z3 context used by this manager.
     */
    const z3::context &getContext() const {
        return *Context;
    }

    /**
     * Get the Z3 solver used by this manager. This is needed for checking the satisfiability of formulas.
     * @return Const reference to the Z3 solver used by this manager.
     */
    const z3::solver &getSolver() const {
        return Solver;
    }

    /**
     * Add a new atomic formula to the set represented by baseId, and return the ID of the resulting set.
     * @param baseId Base ID representing the original set of formulas.
     * @param atom Atom to add to the set represented by baseId. This is a Z3 expression representing an atomic formula.
     * @return Unique ID representing the new set of formulas that includes the atom.
     * If the resulting set is already present, returns the existing ID for that set.
     */
    uint32_t addAtom(uint32_t baseId, const z3::expr &atom);

    /**
     * Calculate the intersection of the sets represented by aId and bId, and return the ID of the resulting set.
     * @param aId Id representing the first set of formulas.
     * @param bId Id representing the second set of formulas.
     * @return Id of the set representing the intersection of the sets represented by aId and bId.
     * If the resulting set is empty, returns topId.
     */
    uint32_t intersect(uint32_t aId, uint32_t bId);

    /**
     * Determine if the environment with the given ID exists in the environment storage.
     * @param id Id of the environment to check for existence.
     * @return true if the environment with the given ID exists, false otherwise.
     */
    bool hasEnv(uint32_t id) const noexcept;

    /**
     * Resolve the given LLVM value in the environment represented by envId, returning root value if found.
     * @param envId Environment ID representing the environment in which to resolve the value.
     * @param val Value to resolve in the environment. This is typically an LLVM value (e.g., a variable).
     * @return Root value corresponding to the given value in the environment represented by envId,
     * or the original value if no binding is found.
     */
    const llvm::Value *resolve(uint32_t envId, const llvm::Value *val) const;

    /**
     * Extend the environment represented by baseEnvId with a new binding from k to v, and return the Id
     * of the resulting environment.
     * @param baseEnvId Environment ID representing environment to extend.
     * @param key Key of the new binding to add to the environment.
     * @param val Value of the new binding to add to the environment.
     * @return Id of the new environment that extends the environment represented by
     * baseEnvId with the new binding from k to v. If an identical environment already exists, returns the existing Id
     * for that environment.
     */
    uint32_t extendEnv(uint32_t baseEnvId, const llvm::Value *key, const llvm::Value *val);

    /**
     * Apply the effects of PHI nodes at the successor block (succ) on the environment represented by inEnvId,
     * and return the Id of the resulting environment.
     * @param inEnvId Environment ID representing the input environment before applying PHI node effects.
     * @param pred Predecessor BasicBlock from which the control flow is coming.
     * This is used to determine the incoming values for PHI nodes in the successor block.
     * @param succ Successor BasicBlock to which the control flow is going.
     * This is used to determine the PHI nodes whose effects need to be applied.
     * @return Environment ID representing the new environment after applying the effects of PHI nodes at the successor
     */
    uint32_t applyPhiPack(uint32_t inEnvId, const llvm::BasicBlock *pred, const llvm::BasicBlock *succ);

    /**
     * Get the set of atomic formulas corresponding to the given ID.
     * This is a wrapper around getSet that returns a vector instead of a set.
     * @param id Id to query
     * @return Vector of atomic formulas corresponding to the given ID.
     * The order of the formulas in the vector is not guaranteed.
     */
    std::vector<z3::expr> getPureSet(uint32_t id) const;

private:
    /**
     * Internal storage for the Z3 context used by this manager.
     */
    std::unique_ptr<z3::context> Context;

    /**
     * Internal storage for the Z3 solver used by this manager.
     */
    z3::solver Solver;

    /**
     * Vector of sets of atomic formulas, where each set is represented as a sorted set of Z3 expressions.
     * The index of each set in the vector serves as its unique ID.
     */
    std::vector<ExprSet> Sets;

    /**
     * Environment storage. Each environment is a EnvNode representing a single variable binding,
     * and environments are represented as linked lists of EnvNodes inside the data structure.
     */
    std::deque<EnvNode> EnvPool;

    /**
     * Environment roots. Each environment is represented as a linked list of EnvNodes, and this vector stores
     * pointers to the root nodes of these linked lists for easier access.
     */
    std::vector<const EnvNode *> EnvRoots;

    /**
     * Environment cache map. This map is used to efficiently check for the existence of environments and to avoid
     * creating duplicate environments. The key is an EnvKey, which represents a single variable binding
     * (key-value pair) in the environment, and the value is the ID of the environment that contains this binding.
     */
    std::unordered_map<EnvKey, uint32_t, EnvKeyHash> EnvCache;

    /**
     * Mutex to ensures mutual exclusion when accessing the environment cache
     */
    mutable std::mutex EnvInternMu;

    /**
     * Set cache map. This map is used to efficiently check for the existence of sets of atomic formulas and
     * to avoid creating duplicate sets. The key is a hash of the set of atomic formulas, and the value is a
     * vector of IDs of sets that have the same hash.
     */
    std::unordered_map<std::size_t, std::vector<uint32_t>> SetsCache;

    /**
     * Mutex to ensure mutual exclusion when accessing the set cache
     */
    mutable std::mutex SetsMutex;

    /**
     * Cache the given set of atomic formulas, and return a unique ID representing this set.
     * If an identical set is already present, returns the existing ID for that set.
     * @param set Set of atomic formulas to cache, represented as a sorted set of Z3 expressions.
     * @return Id of the set representing the given set of atomic formulas.
     * If an identical set is already present, returns the existing ID for that set.
     */
    uint32_t internSet(const ExprSet &set);

    /**
     * Iterate over the environment represented by envId to find the root value corresponding to the given key k.
     * @param envId Environment ID representing the environment in which to look up the key.
     * @param key Key to look up in the environment. This is typically an LLVM value (e.g., a variable).
     * @return Root value corresponding to the given key in the environment represented by envId,
     * or nullptr if no binding is found.
     */
    const llvm::Value *lookupEnv(uint32_t envId, const llvm::Value *key) const;

    /**
     * Helper method to make sure that the empty environment (envId 0) is initialized in the environment storage.
     * This is important to ensure that we have a valid representation for the empty environment,
     * which is used as the default environment for elements that have no variable bindings.
     */
    void ensureEnvZeroInitialized();

    /**
    * Compute a hash value for the given set of atomic formulas. This is used for caching sets in the set cache map.
    * The hash is computed based on the AST ids of the Z3 expressions in the set, as these uniquely identify the expressions.
    * @param set Set of atomic formulas to hash, represented as a sorted set of Z3 expressions.
    * @return Hash value for the given set of atomic formulas.
    */
    static std::size_t hashSet(const ExprSet &set);
};

} // namespace Feasibility

#endif // SPEAR_FEASIBILITYANALYSISMANAGER_H