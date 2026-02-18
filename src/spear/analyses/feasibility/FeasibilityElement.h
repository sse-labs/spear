/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_FEASIBILITYELEMENT_H
#define SPEAR_FEASIBILITYELEMENT_H

#include <atomic>
#include <cstdint>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <z3++.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/Hashing.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Instructions.h>

namespace z3 {
class context;
class expr;
} // namespace z3

namespace llvm {
class Value;
class raw_ostream;
} // namespace llvm

namespace Feasibility {

template <typename KeyT, typename ValueT, unsigned PoolReserve = 256>
class EnvPool final {
 public:
  using Env = llvm::DenseMap<KeyT, ValueT>;
  using EnvId = std::uint32_t;

  EnvPool() {
    pool.emplace_back(); // reserve ID 0 for the empty environment
  }

  /**
   * Return the environment associated with the given id.
   * The returned reference is valid as long as the pool is not modified.
   * @param id ID of the environment to retrieve
   * @return environment associated with the given id
   */
  [[nodiscard]] const Env &get(EnvId id) const {
    return pool[id];
  }

  /**
   * Dummy
   * @return
   */
  EnvId empty() const noexcept {
    return 0;
  }

  /**
   * Set the value of the given key to the given value in the environment associated with the given id.
   * @param base Environment ID to update
   * @param key Key to update
   * @param val Value to set for the given key
   * @return ID of the updated environment
   */
  EnvId set(EnvId base, KeyT key, ValueT val) {
    Env reffedEnv = pool[base];
    reffedEnv[key] = val;
    return insert(std::move(reffedEnv));
  }

  /**
   * Erase the given key from the environment associated with the given id.
   * @param base Environment ID to update
   * @param key Key to erase
   * @return ID of the updated environment
   */
  EnvId erase(EnvId base, KeyT key) {
    Env reffedEnv = pool[base];
    reffedEnv.erase(key);
    return insert(std::move(reffedEnv));
  }

  /**
   * Retrieve the value of the given key in the environment associated with the given id.
   * @param base Environment ID to query
   * @param key Key to query
   * @return Value of the given key in the environment associated with the given id,
   * or std::nullopt if the key is not present
   */
  std::optional<ValueT> getValue(EnvId base, KeyT key) {
    const Env &env = pool[base];
    auto It = env.find(key);
    if (It == env.end()) {
      return std::nullopt;
    }
    return It->second;
  }

  /**
   * Join the environments associated with the given ids by keeping only the key-value pairs that
   * are equal in both environments.
   * @param A Environment ID of the first environment to join
   * @param B Environment ID of the second environment to join
   * @return Join of the environments associated with the given ids,
   * containing only the key-value pairs that are equal in both environments
   */
  EnvId joinKeepEqual(EnvId A, EnvId B) {
    const Env &envA = pool[A];
    const Env &envB = pool[B];

    Env result;
    result.reserve(std::min(envA.size(), envB.size()));

    const Env *Small = &envA;
    const Env *Large = &envB;
    if (envA.size() > envB.size()) {
      Small = &envB;
      Large = &envA;
    }

    for (const auto &KV : *Small) {
      auto It = Large->find(KV.first);
      if (It != Large->end() && It->second == KV.second) {
        result.insert({KV.first, KV.second});
      }
    }

    return insert(std::move(result));
  }

  /**
   * Insert the given environment into the pool and return its ID.
   * @param env Environment to insert into the pool
   * @return Id of the inserted environment
   */
  EnvId insert(Env &&env) {
    const llvm::hash_code hash = hashEnv(env);
    auto &bucket = internalMap[hash];

    for (EnvId id : bucket) {
      if (envEqual(pool[id], env)) {
        return id;
      }
    }

    const EnvId newId = static_cast<EnvId>(pool.size());
    pool.push_back(env);
    bucket.push_back(newId);
    return newId;
  }

  [[nodiscard]] size_t poolSize() const { return pool.size(); }
  [[nodiscard]] size_t largestEnvSize() const {
    size_t max = 0;
    for (const auto &env : pool) {
      max = std::max(max, env.size());
    }
    return max;
  }

 private:
  llvm::SmallVector<Env, PoolReserve> pool;

  llvm::DenseMap<llvm::hash_code, llvm::SmallVector<EnvId, 2>> internalMap;

  /**
   * Compute the hash of the given environment.
   * @param env Environment to compute the hash for
   * @return hash of the given environment
   */
  static llvm::hash_code hashEnv(const Env &env) {
    llvm::hash_code hash = llvm::hash_combine(env.size());
    for (const auto &KV : env) {
      hash = llvm::hash_combine(hash, KV.first, KV.second);
    }
    return hash;
  }

  /**
   * Compare the given environments for equality.
   * @param A First environment to compare
   * @param B Second environment to compare
   * @return true if the given environments are equal, false otherwise
   */
  static bool envEqual(const Env &A, const Env &B) {
    if (A.size() != B.size()) {
      return false;
    }
    for (const auto &KV : A) {
      auto It = B.find(KV.first);
      if (It == B.end() || It->second != KV.second) {
        return false;
      }
    }
    return true;
  }

};


class FeasibilityStateStore;

struct FeasibilityElement final {
  enum class Kind : std::uint8_t {
    IdeNeutral   = 1,
    Bottom       = 2,
    Normal       = 3,
    Top          = 4
  };

  FeasibilityStateStore *store = nullptr;

  bool isSAT = true;

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
  using ExprId = std::uint32_t;
  using EnvKey = const llvm::Value *;

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

  static bool isNotOf(const z3::expr &A, const z3::expr &B);
  static bool isAnd2(const z3::expr &E);
  static bool isOr2(const z3::expr &E);

  static z3::expr factor_or_and_not(const z3::expr &E);

  [[nodiscard]] ExprId internExpr(const z3::expr &E);
  [[nodiscard]] const z3::expr &exprOf(ExprId Id) const;


  [[nodiscard]] ExprId getOrCreateSym(const llvm::Value *V, unsigned Bw, llvm::StringRef Prefix);

  llvm::DenseMap<const llvm::Value *, ExprId> SymCache;

  EnvPool<EnvKey, ExprId> Mem;
  EnvPool<EnvKey, ExprId> Ssa;

  [[nodiscard]] size_t getPathConstraintCount() const { return baseConstraints.size(); }
  [[nodiscard]] uint64_t computeAstDepth(const z3::expr &E) const;
  [[nodiscard]] uint64_t computeAstNodes(const z3::expr &E) const;

  z3::context context;
  z3::solver  solver;

  std::vector<z3::expr> baseConstraints;
  std::unordered_map<ExprId, id_t> pathConditions;
  std::vector<int8_t> pcSatCache;


  llvm::SmallVector<z3::expr, 1024> ExprTable;
  llvm::DenseMap<unsigned, llvm::SmallVector<ExprId, 2>> ExprIntern;

  struct ResolveKey {
    id_t ssaId;
    id_t memId;
    const llvm::Value *V;

    bool operator==(const ResolveKey &O) const noexcept {
      return ssaId == O.ssaId && memId == O.memId && V == O.V;
    }
  };

  struct ResolveKeyHash {
    size_t operator()(const ResolveKey &K) const noexcept {
      // Cheap pointer+ids hash
      size_t h = 0xcbf29ce484222325ULL;
      auto mix = [&](size_t x) {
        h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      };
      mix(static_cast<size_t>(K.ssaId));
      mix(static_cast<size_t>(K.memId));
      mix(reinterpret_cast<size_t>(K.V));
      return h;
    }
  };

  // Cache: (ssaId, memId, llvm::Value*) -> ExprId
  std::unordered_map<ResolveKey, ExprId, ResolveKeyHash> ResolveCache;

  // Optional metrics
  std::atomic<uint64_t> resolveCalls{0};
  std::atomic<uint64_t> resolveHits{0};
  std::atomic<uint64_t> resolveMisses{0};

  struct CmpCondKey {
    id_t ssaId;
    id_t memId;
    const llvm::ICmpInst *Cmp;
    bool TakeTrueEdge;

    bool operator==(const CmpCondKey &o) const noexcept {
      return ssaId == o.ssaId && memId == o.memId && Cmp == o.Cmp && TakeTrueEdge == o.TakeTrueEdge;
    }
  };

  struct CmpCondKeyHash {
    size_t operator()(const CmpCondKey &k) const noexcept {
      size_t h = 0;
      h ^= (size_t)k.ssaId + 0x9e3779b97f4a7c15ULL + (h<<6) + (h>>2);
      h ^= (size_t)k.memId + 0x9e3779b97f4a7c15ULL + (h<<6) + (h>>2);
      h ^= (size_t)k.Cmp   + 0x9e3779b97f4a7c15ULL + (h<<6) + (h>>2);
      h ^= (size_t)k.TakeTrueEdge + 0x9e3779b97f4a7c15ULL + (h<<6) + (h>>2);
      return h;
    }
  };

  std::unordered_map<CmpCondKey, ExprId, CmpCondKeyHash> CmpCondCache;
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const FeasibilityElement &E);

std::string toString(const std::optional<FeasibilityElement> &E);

std::ostream &operator<<(std::ostream &os, const std::optional<FeasibilityElement> &E);

} // namespace Feasibility

#endif // SPEAR_FEASIBILITYELEMENT_H
