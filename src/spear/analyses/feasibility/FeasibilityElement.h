/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_FEASIBILITYELEMENT_H
#define SPEAR_FEASIBILITYELEMENT_H

#include <vector>
#include <z3++.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>

namespace Feasibility {

struct Z3ExprHash {
    std::size_t operator()(const z3::expr& e) const noexcept {
        return static_cast<std::size_t>(
            Z3_get_ast_hash(e.ctx(), e)
        );
    }
};

struct Z3ExprEq {
    bool operator()(const z3::expr& a,
                    const z3::expr& b) const noexcept {
        return Z3_is_eq_ast(a.ctx(), a, b);
    }
};

class FeasibilityAnalysisManager {
 public:
    enum class SatTri : uint8_t { Unknown = 0, Sat = 1, Unsat = 2 };

    /**
     * Environment map that describes SSA renames as linked list
     */
    struct EnvNode {
        const EnvNode* parent;
        const llvm::Value* key;
        const llvm::Value* val;
    };

    explicit FeasibilityAnalysisManager(std::unique_ptr<z3::context> ctx);

    /**
     * ...
     */
    std::unique_ptr<z3::context> OwnedContext;

    /**
     * Z3 context to manage the lifetime of Z3 expressions and solvers. This is shared across all elements and ensures that all Z3 objects are properly managed and deallocated when the manager is destroyed.
     */
    z3::context *Context = nullptr;

    /**
     * As the lattice element has to be trivially copyable, we cannot store a pointer to the shared state store directly in the element.
     * Instead, we store a pointer to the manager, which can be used to access the shared state store and other shared resources.
     */
    std::vector<z3::expr> Formulas;

    /**
     * Sat-Cache to collect already used sat proves for known functions
     */
    std::vector<SatTri> SatCache;

    /**
     * Renamepool. Maps id -> Env renaming
     */
    std::vector<EnvNode> EnvPool;

    /**
     * Environment roots the pool elements map to
     */
    std::vector<const EnvNode*> EnvRoots;

    /**
     * Z3 Hashmap to make lookup O(1)
     */
    std::unordered_map<z3::expr, uint32_t, Z3ExprHash, Z3ExprEq> FormularsToId;

    /**
     * Solver instance to check satisfiability of path constraints. This is shared across all elements and can be used to efficiently check the feasibility of path constraints without needing to create a new solver instance for each element.
     */
    z3::solver Solver;

    /**
     * Return an expression
     * @param id
     * @return
     */
    const z3::expr& getExpression(uint32_t id) const;

    /**
     * Find the ID of a formula in the manager's formula list.
     * This can be used to check if a given formula already exists in the manager and to retrieve its ID for use in the lattice elements.
     * @param expr
     * @return
     */
    std::optional<uint32_t> findFormulaId(const z3::expr& expr) const;

    /**
     * Create new and formular
     * @param a
     * @param b
     * @return
     */
    uint32_t mkAnd(uint32_t aId, uint32_t bId);

    /**
     * Create new or formular
     * @param a
     * @param b
     * @return
     */
    uint32_t mkOr(uint32_t aId, uint32_t bId);

    bool isBoolTrue(const z3::expr &e);

    bool isBoolFalse(const z3::expr &e);

    bool isNotExpr(const z3::expr &e);

    bool isNotOf(const z3::expr &a, const z3::expr &b);

    void collectOrArgs(const z3::expr &e, std::vector<z3::expr> &out);

    void collectAndArgs(const z3::expr &e, std::vector<z3::expr> &out);

    z3::expr mkNaryOr(z3::context &ctx, const std::vector<z3::expr> &v);

    z3::expr mkNaryAnd(z3::context &ctx, const std::vector<z3::expr> &v);

    z3::expr mkOrSimplified(const z3::expr &a, const z3::expr &b);

    z3::expr mkAndSimplified(const z3::expr &a, const z3::expr &b);

    /**
     * Create new not formular
     * @param a
     * @return
     */
    uint32_t mkNot(z3::expr a);

    /**
     * Create new atomic formular
     * @param a
     * @return
     */
    uint32_t mkAtomic(z3::expr a);

    /**
     *
     * @param id
     * @return
     */
    bool isSat(uint32_t id);

    /**
     * Add a new renaming relationship to our mapping
     * @param baseEnvId
     * @param k
     * @param v
     * @return
     */
    uint32_t extendEnv(uint32_t baseEnvId, const llvm::Value *k, const llvm::Value *v);

    /**
     * Resolve a given renaming id and the given value to its root
     * @param envId
     * @param v
     * @return
     */
    const llvm::Value *resolve(uint32_t envId, const llvm::Value *v) const;

    /**
     * For the edge from pred to succ traverse the phis contained in succ and deal with phis
     * add a new translation to the translation environment
     * @param inEnvId
     * @param pred
     * @param succ
     * @return
     */
    uint32_t applyPhiPack(uint32_t inEnvId, const llvm::BasicBlock *pred, const llvm::BasicBlock *succ);

    z3::expr simplify(z3::expr input);
};

class FeasibilityElement {
 public:
    enum class Kind : std::uint8_t {
        Bottom       = 1,  // Bottom represents an infeasible path constraint
        Normal       = 2,  // Normal represents a normal path constraint element
        Top          = 3  // TOP reprents "No information" and is the absorbing element of the lattice (join with TOP always yields TOP)
    };

    static constexpr uint32_t topId = 0;
    static constexpr uint32_t bottomId = 1;
    static constexpr uint32_t initId = 2;

    uint32_t formularID = topId;

    uint32_t environmentID = 0;

    Kind kind = Kind::Top;

    FeasibilityElement() = default;

    /**
     * Constructor for FeasibilityElement. Initializes the element with the given kind, formula ID, and a reference to the manager.
     * The formula ID is used to identify the path constraint associated with this element in the manager's formula list.
     * The manager reference allows the element to access shared resources like the state store and formula management.
     * @param k The kind of the element (Bottom, Normal, or Top).
     * @param id The ID of the formula associated with this element in the manager's formula list.
     */
    FeasibilityElement(Kind k, uint32_t id, FeasibilityAnalysisManager *man) : kind(k), formularID(id), manager(man) {}

    /**
     * Reference to the manager to access shared resources like the state store and formula management.
     * This is a pointer to avoid making the element non-trivially copyable, which is a requirement for the lattice elements in PhASAR's IDE framework.
     */
    FeasibilityAnalysisManager *manager = nullptr;

    /**
     * Join this element with another element, returning a new element that represents the least upper bound of the two elements in the lattice. The join operation should be commutative, associative, and idempotent.
     * @param other
     * @return
     */
    FeasibilityElement join(FeasibilityElement &other) const;

    /**
     *
     * @return
     */
    bool isBottom() const;

    /**
     *
     * @return
     */
    bool isTop() const;

    /**
     * Get the kind of this element (Bottom, Normal, or Top).
     * @return
     */
    Kind getKind();

    /**
     * Get the manager associated with this element to access shared resources.
     * @return
     */
    FeasibilityAnalysisManager* getManager();

    /**
     * Static factory method to create a new FeasibilityElement with the given manager, formula ID, and kind.
     * @param manager
     * @param formulaId
     * @param type
     * @return
     */
    static FeasibilityElement createElement(FeasibilityAnalysisManager *manager, uint32_t formulaId, Kind type);

    /**
     * Convert this element to a string representation for debugging purposes.
     * This can include the kind of the element and a human-readable representation of the associated formula (if it's a normal element).
     * @return
     */
    std::string toString() const;

    /**
     * Comparison operator ==
     * @param other Other FeasibilityElement to compare with
     * @return True if equal, false otherwise
     */
    bool operator==(const FeasibilityElement &other) const;

    /**
     * Comparison operator !=
     * @param other Other FeasibilityElement to compare with
     * @return True if unequal, false otherwise
     */
    bool operator!=(const FeasibilityElement &other) const;
};

/**
 * Operator overload to print a FeasibilityElement to an LLVM raw_ostream.
 * This can be used for debugging and logging purposes to easily visualize the state of the analysis.
 * @param OS
 * @param E
 * @return
 */
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const FeasibilityElement::Kind &K);

/**
 * Operator overload to print an optional FeasibilityElement to an LLVM raw_ostream.
 * @param os
 * @param E
 * @return
 */
std::ostream &operator<<(std::ostream &os, const std::optional<FeasibilityElement::Kind> &K);

/**
 * Operator overload to print a FeasibilityElement to an LLVM raw_ostream.
 * This can be used for debugging and logging purposes to easily visualize the state of the analysis.
 * @param OS
 * @param E
 * @return
 */
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const FeasibilityElement &E);

/**
 * Convert an optional FeasibilityElement to a string representation for debugging purposes.
 * This can handle the case where the optional does not contain a value (std::nullopt) and provide a suitable string representation for that case as well.
 * @param E
 * @return
 */
std::string toString(const std::optional<FeasibilityElement> &E);

/**
 * Convert an optional FeasibilityElement to a string representation for debugging purposes.
 * This can handle the case where the optional does not contain a value (std::nullopt) and provide a suitable string representation for that case as well.
 * @param E
 * @return
 */
std::string toString(const std::optional<FeasibilityElement::Kind> &K);

/**
 * Operator overload to print an optional FeasibilityElement to an LLVM raw_ostream.
 * @param os
 * @param E
 * @return
 */
std::ostream &operator<<(std::ostream &os, const std::optional<FeasibilityElement> &E);

}


#endif // SPEAR_FEASIBILITYELEMENT_H
