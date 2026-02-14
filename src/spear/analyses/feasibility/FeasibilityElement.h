/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_FEASIBILITYELEMENT_H
#define SPEAR_FEASIBILITYELEMENT_H

#include <cstdint>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <z3++.h>

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

struct FeasibilityElement final {
  enum class Kind : std::uint8_t {
    IdeAbsorbing = 0,
    IdeNeutral   = 1,
    Bottom       = 2,
    Normal       = 3,
    Top          = 4
  };

  FeasibilityStateStore *store = nullptr;
  Kind kind = Kind::Top;
  std::uint32_t pcId = 0;
  std::uint32_t ssaId = 0;
  std::uint32_t memId = 0;

  static FeasibilityElement ideNeutral(FeasibilityStateStore *S) noexcept;
  static FeasibilityElement ideAbsorbing(FeasibilityStateStore *S) noexcept;

  static FeasibilityElement top(FeasibilityStateStore *S) noexcept;
  static FeasibilityElement bottom(FeasibilityStateStore *S) noexcept;
  static FeasibilityElement initial(FeasibilityStateStore *S) noexcept;

  [[nodiscard]] Kind getKind() const noexcept;

  [[nodiscard]] bool isIdeNeutral() const noexcept;
  [[nodiscard]] bool isIdeAbsorbing() const noexcept;

  [[nodiscard]] bool isTop() const noexcept;
  [[nodiscard]] bool isBottom() const noexcept;
  [[nodiscard]] bool isNormal() const noexcept;
  [[nodiscard]] FeasibilityStateStore *getStore() const noexcept;

  [[nodiscard]] FeasibilityElement assume(const z3::expr &cond) const;
  [[nodiscard]] FeasibilityElement clearPathConstraints() const;

  [[nodiscard]] FeasibilityElement join(const FeasibilityElement &other) const;

  [[nodiscard]] bool equal_to(const FeasibilityElement &other) const noexcept;

  friend bool operator==(const FeasibilityElement &A,
                         const FeasibilityElement &B) noexcept;

  friend bool operator!=(const FeasibilityElement &A,
                         const FeasibilityElement &B) noexcept;

  [[nodiscard]] bool isSatisfiable() const;
};



class FeasibilityStateStore final {
public:
  using id_t = std::uint32_t;

  FeasibilityStateStore();
  ~FeasibilityStateStore();

  FeasibilityStateStore(const FeasibilityStateStore &) = delete;
  FeasibilityStateStore &operator=(const FeasibilityStateStore &) = delete;

  [[nodiscard]] z3::context &ctx() noexcept;

  [[nodiscard]] id_t pcAssume(id_t pc, const z3::expr &cond);
  [[nodiscard]] id_t pcClear();

  [[nodiscard]] FeasibilityElement normalizeIdeKinds(const FeasibilityElement &E,
                                                    FeasibilityStateStore *S);

  [[nodiscard]] z3::expr getPathConstraint(id_t pcId) const;

  [[nodiscard]] FeasibilityElement join(const FeasibilityElement &A,
                                       const FeasibilityElement &B);

  [[nodiscard]] bool isSatisfiable(const FeasibilityElement &E);

  [[nodiscard]] bool isValid(const z3::expr &e);
  [[nodiscard]] bool isUnsat(const z3::expr &e);
  [[nodiscard]] bool isEquivalent(const z3::expr &A, const z3::expr &B);

  static bool isNotOf(const z3::expr &A, const z3::expr &B);
  static bool isAnd2(const z3::expr &E);
  static bool isOr2(const z3::expr &E);

  static z3::expr factor_or_and_not(const z3::expr &E);

private:
  z3::context context;
  z3::solver  solver;

  std::vector<z3::expr> baseConstraints;
  std::unordered_map<std::string, id_t> pathConditions;

  std::vector<int8_t> pcSatCache;
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const FeasibilityElement &E);

std::string toString(const std::optional<FeasibilityElement> &E);

std::ostream &operator<<(std::ostream &os, const std::optional<FeasibilityElement> &E);

} // namespace Feasibility

#endif // SPEAR_FEASIBILITYELEMENT_H
