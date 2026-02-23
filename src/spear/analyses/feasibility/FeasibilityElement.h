// FeasibilityElement.h
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_FEASIBILITYELEMENT_H
#define SPEAR_FEASIBILITYELEMENT_H

#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

#include <llvm/Support/raw_ostream.h>
#include <z3++.h>
#include <llvm/IR/Value.h>

#include <phasar/DataFlow/IfdsIde/EdgeFunction.h>

namespace Feasibility {

class FeasibilityAnalysisManager;

// ============================================================================
// FeasibilityElement
// ============================================================================
class FeasibilityElement {
public:
  enum class Kind : uint8_t { Top = 0, Bottom = 1, Normal = 2, Empty = 3 };

  static constexpr uint32_t topId    = 0; // empty set == true
  static constexpr uint32_t bottomId = 1; // reserved (unused for sets)

  FeasibilityElement() noexcept
      : kind(Kind::Top), formularID(topId), manager(nullptr), envId(0) {}

  static FeasibilityElement createElement(FeasibilityAnalysisManager *man,
                                          uint32_t formulaId, Kind type,
                                          uint32_t envId = 0) noexcept;

  bool isTop() const noexcept { return kind == Kind::Top; }
  bool isBottom() const noexcept { return kind == Kind::Bottom; }
  bool isNormal() const noexcept { return kind == Kind::Normal; }
  bool isEmpty() const noexcept { return kind == Kind::Empty; }

  FeasibilityAnalysisManager *getManager() const noexcept { return manager; }
  Kind getKind() const noexcept { return kind; }
  uint32_t getFormulaId() const noexcept { return formularID; }
  uint32_t getEnvId() const noexcept { return envId; }

  void setFormulaId(uint32_t id) noexcept { formularID = id; }
  void setEnvId(uint32_t id) noexcept { envId = id; }
  void setKind(Kind k) noexcept { kind = k; }

  /// MUST-join (intersection) with reachability semantics:
  /// ⊥ ⊔ x = x
  /// x ⊔ ⊥ = x
  /// Top = empty set (true), absorbing for intersection.
  FeasibilityElement join(const FeasibilityElement &other) const;

  std::string toString() const;

  bool operator==(const FeasibilityElement &other) const noexcept {
    return kind == other.kind && formularID == other.formularID &&
           envId == other.envId && manager == other.manager;
  }
  bool operator!=(const FeasibilityElement &other) const noexcept {
    return !(*this == other);
  }

private:
  friend class FeasibilityAnalysisManager;

  FeasibilityElement(Kind k, uint32_t fid, FeasibilityAnalysisManager *m,
                     uint32_t e) noexcept
      : kind(k), formularID(fid), manager(m), envId(e) {}

  Kind kind{Kind::Top};
  uint32_t formularID{topId};
  FeasibilityAnalysisManager *manager{nullptr};
  uint32_t envId{0};
};

// ============================================================================
// FeasibilityAnalysisManager
// ============================================================================
class FeasibilityAnalysisManager {
public:
  using l_t = FeasibilityElement;
  using EF  = psr::EdgeFunction<l_t>;
  using EFStableId = uint64_t;

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

  // Optional helper (slow path): find a singleton set's id
  std::optional<uint32_t> findSingletonId(const z3::expr &atom) const;

  std::vector<z3::expr> getPureSet(uint32_t id) const;

  struct EnvNode {
    const EnvNode *parent = nullptr;
    const llvm::Value *key = nullptr;
    const llvm::Value *val = nullptr;
  };

  struct EnvKey {
    uint32_t Base = 0;
    const llvm::Value *K = nullptr;
    const llvm::Value *V = nullptr;

    bool operator==(const EnvKey &o) const noexcept {
      return Base == o.Base && K == o.K && V == o.V;
    }
  };

  struct EnvKeyHash {
    std::size_t operator()(const EnvKey &k) const noexcept {
      std::size_t h = std::hash<uint32_t>{}(k.Base);
      auto mix = [&](std::size_t x) {
        h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      };
      mix(std::hash<const void *>{}(static_cast<const void *>(k.K)));
      mix(std::hash<const void *>{}(static_cast<const void *>(k.V)));
      return h;
    }
  };

  bool hasEnv(uint32_t id) const noexcept;

  /// Direct lookup: returns bound value if present, else nullptr
  const llvm::Value *lookupEnv(uint32_t envId, const llvm::Value *k) const;

  /// Resolves a value through the environment chain (v -> binding -> binding...)
  const llvm::Value *resolve(uint32_t envId, const llvm::Value *v) const;

  /// Create/return envId for baseEnvId extended with (k -> v)
  uint32_t extendEnv(uint32_t baseEnvId, const llvm::Value *k,
                     const llvm::Value *v);

  /// Apply all PHIs in 'succ' for the incoming edge pred->succ
  uint32_t applyPhiPack(uint32_t inEnvId, const llvm::BasicBlock *pred,
                        const llvm::BasicBlock *succ);


  // --------------------------------------------------------------------------
  // Owned Z3
  // --------------------------------------------------------------------------
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

std::string toString(const std::optional<FeasibilityElement> &E);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const FeasibilityElement &E);

} // namespace Feasibility

#endif // SPEAR_FEASIBILITYELEMENT_H