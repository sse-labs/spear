// FeasibilityElement.h
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_FEASIBILITYELEMENT_H
#define SPEAR_FEASIBILITYELEMENT_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <llvm/ADT/Hashing.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>
#include <z3++.h>

#include <phasar/DataFlow/IfdsIde/EdgeFunction.h>

namespace Feasibility {

struct Z3ExprHash {
  std::size_t operator()(const z3::expr &e) const noexcept {
    return static_cast<std::size_t>(Z3_get_ast_hash(e.ctx(), e));
  }
};

struct Z3ExprEq {
  bool operator()(const z3::expr &a, const z3::expr &b) const noexcept {
    return Z3_is_eq_ast(a.ctx(), a, b);
  }
};

class FeasibilityAnalysisManager;

class FeasibilityElement {
public:
  enum class Kind : uint8_t { Top = 0, Bottom = 1, Normal = 2 };

  // IMPORTANT: keep these consistent with your manager initialization.
  static constexpr uint32_t topId = 0;     // true
  static constexpr uint32_t bottomId = 1;  // false

  FeasibilityElement() noexcept
      : kind(Kind::Top), formularID(0), envId(0), manager(nullptr) {}

  static FeasibilityElement createElement(FeasibilityAnalysisManager *man,
                                          uint32_t formulaId, Kind type,
                                          uint32_t envId = 0);

  Kind getKind() const { return kind; }
  bool isTop() const { return kind == Kind::Top; }
  bool isBottom() const { return kind == Kind::Bottom; }
  bool isNormal() const { return kind == Kind::Normal; }

  void setKind(Kind k) { this->kind = k; }

  uint32_t getFormulaId() const { return formularID; }
  uint32_t getEnvId() const { return envId; }
  FeasibilityAnalysisManager *getManager() const { return manager; }

  void setFormulaId(uint32_t id) { this->formularID = id; }
  void setEnvId(uint32_t id) { this->envId = id; }

  FeasibilityElement join(FeasibilityElement &other) const;

  std::string toString() const;

  bool operator==(const FeasibilityElement &other) const {
    return kind == other.kind && formularID == other.formularID &&
           envId == other.envId && manager == other.manager;
  }
  bool operator!=(const FeasibilityElement &other) const {
    return !(*this == other);
  }

private:
  friend class FeasibilityAnalysisManager;

  // Canonicalization rule:
  //  - Bottom must always have envId==0
  //  - (recommended) Top must always have envId==0
  static inline uint32_t canonicalizeEnv(Kind k, uint32_t env) {
    if (k == Kind::Bottom) return 0;
    if (k == Kind::Top) return 0;
    return env;
  }

  FeasibilityElement(Kind k, uint32_t fid, FeasibilityAnalysisManager *m,
                     uint32_t e)
      : kind(k), formularID(fid), manager(m), envId(canonicalizeEnv(k, e)) {}

  Kind kind{Kind::Top};
  uint32_t formularID{topId};
  FeasibilityAnalysisManager *manager{nullptr};
  uint32_t envId{0};
};

class FeasibilityAnalysisManager {
public:
  enum class SatTri : uint8_t { Unknown = 0, Sat = 1, Unsat = 2 };

  using l_t = FeasibilityElement;
  using EF  = psr::EdgeFunction<l_t>;

  using EFStableId = uint64_t;

  explicit FeasibilityAnalysisManager(std::unique_ptr<z3::context> ctx);

  // ---- canonical singleton EFs (MUST be reused everywhere) ----
  const EF &identityEF() const noexcept { return IdentityEF; }
  const EF &allTopEF() const noexcept { return AllTopEF; }
  const EF &allBottomEF() const noexcept { return AllBottomEF; }

  struct EnvNode {
    const EnvNode *parent;
    const llvm::Value *key;
    const llvm::Value *val;
  };

  struct EnvKey {
    uint32_t Base;
    const llvm::Value *K;
    const llvm::Value *V;

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

  struct PhiKey {
    uint32_t In;
    const llvm::BasicBlock *Pred;
    const llvm::BasicBlock *Succ;

    bool operator==(const PhiKey &o) const noexcept {
      return In == o.In && Pred == o.Pred && Succ == o.Succ;
    }
  };

  struct PhiKeyHash {
    std::size_t operator()(const PhiKey &k) const noexcept {
      std::size_t h = std::hash<uint32_t>{}(k.In);
      auto mix = [&](std::size_t x) {
        h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      };
      mix(std::hash<const void *>{}(static_cast<const void *>(k.Pred)));
      mix(std::hash<const void *>{}(static_cast<const void *>(k.Succ)));
      return h;
    }
  };

  // ===== resolve(envId, v) cache =====
  struct ResolveKey {
    uint32_t Env;
    const llvm::Value *V;

    bool operator==(const ResolveKey &o) const noexcept {
      return Env == o.Env && V == o.V;
    }
  };

  struct ResolveKeyHash {
    std::size_t operator()(const ResolveKey &k) const noexcept {
      std::size_t h = std::hash<uint32_t>{}(k.Env);
      h ^= std::hash<const void *>{}(static_cast<const void *>(k.V)) +
           0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      return h;
    }
  };

  // ===== compose/join interning keys (KEYED BY STABLE IDS) =====
  struct EFPairKey {
    EFStableId A;
    EFStableId B;
    bool operator==(const EFPairKey &o) const noexcept { return A==o.A && B==o.B; }
  };

  struct EFPairHash {
    size_t operator()(const EFPairKey &k) const noexcept {
      size_t h = std::hash<EFStableId>{}(k.A);
      h ^= std::hash<EFStableId>{}(k.B) + 0x9e3779b97f4a7c15ULL + (h<<6) + (h>>2);
      return h;
    }
  };

  struct EFVecKey {
    llvm::SmallVector<EFStableId, 4> ids;
    bool operator==(const EFVecKey &o) const noexcept { return ids == o.ids; }
  };

  struct EFVecKeyHash {
    size_t operator()(const EFVecKey &k) const noexcept {
      return (size_t)llvm::hash_combine_range(k.ids.begin(), k.ids.end());
    }
  };

  // ---------- formulas ----------
  const z3::expr &getExpression(uint32_t id) const;
  std::optional<uint32_t> findFormulaId(const z3::expr &expr) const;

  uint32_t mkAnd(uint32_t aId, uint32_t bId);
  uint32_t mkNot(z3::expr a);
  uint32_t mkAtomic(z3::expr a);

  bool isBoolTrue(const z3::expr &e);
  bool isBoolFalse(const z3::expr &e);
  bool isNotExpr(const z3::expr &e);
  bool isNotOf(const z3::expr &a, const z3::expr &b);

  void collectAndArgs(const z3::expr &e, std::vector<z3::expr> &out);

  z3::expr mkNaryAnd(z3::context &ctx, const std::vector<z3::expr> &v);

  z3::expr mkAndSimplified(const z3::expr &a, const z3::expr &b);

  // SAT depends ONLY on formula id.
  bool isSat(uint32_t id);

  // ---------- environments ----------
  bool hasEnv(uint32_t id) const;

  const llvm::Value *lookupEnv(uint32_t envId, const llvm::Value *k) const;
  const llvm::Value *lookupBinding(uint32_t envId, const llvm::Value *key) const;

  const llvm::Value *resolve(uint32_t envId, const llvm::Value *v);

  uint32_t extendEnv(uint32_t baseEnvId, const llvm::Value *k, const llvm::Value *v);

  uint32_t applyPhiPack(uint32_t inEnvId, const llvm::BasicBlock *pred, const llvm::BasicBlock *succ);

  z3::expr simplify(z3::expr input);

  // ---------- state ----------
  std::unique_ptr<z3::context> OwnedContext;
  z3::context *Context = nullptr;

  std::vector<z3::expr> Formulas;
  std::vector<SatTri> SatCache;
  std::unordered_map<z3::expr, uint32_t, Z3ExprHash, Z3ExprEq> FormularsToId;

  std::deque<EnvNode> EnvPool;
  std::vector<const EnvNode *> EnvRoots;
  std::unordered_map<EnvKey, uint32_t, EnvKeyHash> EnvIntern;

  std::unordered_map<PhiKey, uint32_t, PhiKeyHash> PhiCache;
  mutable std::mutex PhiCacheMu;
  uint64_t PhiCacheHits = 0;
  uint64_t PhiCacheMiss = 0;
  uint64_t PhiCacheInserts = 0;

  mutable std::unordered_map<ResolveKey, const llvm::Value *, ResolveKeyHash> ResolveCache;
  mutable std::mutex ResolveCacheMu;
  uint64_t ResolveCacheHits = 0;
  uint64_t ResolveCacheMiss = 0;
  uint64_t ResolveCacheInserts = 0;

  // ===== compose/join interning =====
  std::unordered_map<EFPairKey, EF, EFPairHash> ComposeIntern;
  mutable std::mutex ComposeInternMu;

  uint64_t ComposeInternHits = 0;
  uint64_t ComposeInternMiss = 0;
  uint64_t ComposeInternInserts = 0;

  z3::solver Solver;

private:
  // Canonical singleton EFs (manager-owned, constructed once)
  EF IdentityEF;
  EF AllTopEF;
  EF AllBottomEF;


  void collectComposeChainLeft(const EF &E, llvm::SmallVectorImpl<EF> &out) const;
  void normalizeComposeChain(llvm::SmallVectorImpl<EF> &chain) const;

  EF internComposeById(EFStableId aId, const EF &A, EFStableId bId, const EF &B);
};

std::string toString(const std::optional<FeasibilityElement> &E);
std::ostream &operator<<(std::ostream &os,
                         const std::optional<FeasibilityElement> &E);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const FeasibilityElement &E);

} // namespace Feasibility

#endif // SPEAR_FEASIBILITYELEMENT_H