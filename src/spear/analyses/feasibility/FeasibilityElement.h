/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_FEASIBILITYELEMENT_H
#define SPEAR_FEASIBILITYELEMENT_H

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <type_traits>

namespace z3 {
class context;
class expr;
} // namespace z3

namespace llvm {
class Value;
class raw_ostream;
} // namespace llvm

namespace Feasibility {

class FeasibilityStateStore;

/**
 * Trivially copyable lattice element (HANDLE).
 *
 * Heavy data (PC constraints, SSA store, Mem store, z3 objects) lives inside
 * FeasibilityStateStore. This element only stores IDs + a raw store pointer.
 */
struct FeasibilityElement final {
  enum class Kind : std::uint8_t { Bottom = 0, Normal = 1, Top = 2 };

  // Raw pointer is trivially copyable. Store must outlive all elements.
  FeasibilityStateStore *store = nullptr;

  Kind kind = Kind::Top;

  // Interned handles into store. 0 means "empty": PC=true, SSA={}, Mem={}
  std::uint32_t pcId = 0;
  std::uint32_t ssaId = 0;
  std::uint32_t memId = 0;

  // ---- factories ----
  static FeasibilityElement top(FeasibilityStateStore *S) noexcept;
  static FeasibilityElement bottom(FeasibilityStateStore *S) noexcept;
  static FeasibilityElement initial(FeasibilityStateStore *S) noexcept;

  // ---- queries ----
  [[nodiscard]] Kind getKind() const noexcept;
  [[nodiscard]] bool isTop() const noexcept;
  [[nodiscard]] bool isBottom() const noexcept;
  [[nodiscard]] bool isNormal() const noexcept;
  [[nodiscard]] FeasibilityStateStore *getStore() const noexcept { return store; }

  // ---- updates (functional style) ----
  [[nodiscard]] FeasibilityElement assume(const z3::expr &cond) const;
  [[nodiscard]] FeasibilityElement setSSA(const llvm::Value *key,
                                          const z3::expr &expr) const;
  [[nodiscard]] FeasibilityElement setMem(const llvm::Value *loc,
                                          const z3::expr &expr) const;
  [[nodiscard]] FeasibilityElement forgetSSA(const llvm::Value *key) const;
  [[nodiscard]] FeasibilityElement forgetMem(const llvm::Value *loc) const;
  [[nodiscard]] FeasibilityElement clearPathConstraints() const;

  // ---- lattice operations ----
  [[nodiscard]] bool leq(const FeasibilityElement &other) const;
  [[nodiscard]] FeasibilityElement join(const FeasibilityElement &other) const;
  [[nodiscard]] FeasibilityElement meet(const FeasibilityElement &other) const;

  [[nodiscard]] bool equal_to(const FeasibilityElement &other) const noexcept;

  friend bool operator==(const FeasibilityElement &A,
                         const FeasibilityElement &B) noexcept {
    return A.equal_to(B);
  }

  friend bool operator!=(const FeasibilityElement &A,
                         const FeasibilityElement &B) noexcept {
    return !A.equal_to(B);
  }

  // ---- SMT helpers (delegate to store) ----
  [[nodiscard]] bool isSatisfiable() const;
  [[nodiscard]] bool isSatisfiableWith(const z3::expr &additionalConstraint) const;
};

// Enforce requirement
static_assert(std::is_trivially_copyable_v<FeasibilityElement>,
              "FeasibilityElement must be trivially copyable!");

// ---------------------------
// Store (heavy state container)
// ---------------------------

class FeasibilityStateStore final {
public:
  using id_t = std::uint32_t;

  FeasibilityStateStore();
  ~FeasibilityStateStore();

  FeasibilityStateStore(const FeasibilityStateStore &) = delete;
  FeasibilityStateStore &operator=(const FeasibilityStateStore &) = delete;

  // One shared Z3 context for all expr managed by this store.
  [[nodiscard]] z3::context &ctx() noexcept;

  // ---- updates on handles ----
  [[nodiscard]] id_t pcAssume(id_t pc, const z3::expr &cond);
  [[nodiscard]] id_t pcClear();

  [[nodiscard]] id_t ssaSet(id_t ssa, const llvm::Value *key, const z3::expr &expr);
  [[nodiscard]] id_t ssaForget(id_t ssa, const llvm::Value *key);

  [[nodiscard]] id_t memSet(id_t mem, const llvm::Value *loc, const z3::expr &expr);
  [[nodiscard]] id_t memForget(id_t mem, const llvm::Value *loc);

  // ---- lattice ops on whole elements ----
  [[nodiscard]] bool leq(const FeasibilityElement &A, const FeasibilityElement &B);
  [[nodiscard]] FeasibilityElement join(const FeasibilityElement &A, const FeasibilityElement &B);
  [[nodiscard]] FeasibilityElement meet(const FeasibilityElement &A, const FeasibilityElement &B);

  // ---- SMT ----
  [[nodiscard]] bool isSatisfiable(const FeasibilityElement &E);
  [[nodiscard]] bool isSatisfiableWith(const FeasibilityElement &E,
                                       const z3::expr &additionalConstraint);

private:
  struct Impl;
  Impl *PImpl = nullptr;
};

// ---------------------------
// Printing helpers
// ---------------------------

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const FeasibilityElement &E);

std::string toString(const std::optional<FeasibilityElement> &E);

std::ostream &operator<<(std::ostream &os, const std::optional<FeasibilityElement> &E);

} // namespace Feasibility

#endif // SPEAR_FEASIBILITYELEMENT_H
