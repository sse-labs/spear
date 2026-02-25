//
// Created by mkrebs on 25.02.26.
//

#include "analyses/feasibility/FeasibilityAnalysisManager.h"

namespace Feasibility {

FeasibilityAnalysisManager::FeasibilityAnalysisManager(
    std::unique_ptr<z3::context> ctx)
    : Context(std::move(ctx)),
      Solver(*Context) {

  // Reserve Set IDs:
  // 0 -> Top == empty set
  // 1 -> bottomId reserved (placeholder)
  Sets.clear();
  Sets.resize(2);
  // Sets[0] is default-constructed empty set (true)
  // Sets[1] keep empty as placeholder; Bottom is represented by Kind::Bottom.
}

std::vector<z3::expr> FeasibilityAnalysisManager::getPureSet(uint32_t id) const {
  if (id >= Sets.size())
    return {};  // or throw std::out_of_range("invalid set id");

  const ExprSet &S = Sets[id];
  return std::vector<z3::expr>(S.begin(), S.end());
}

std::size_t FeasibilityAnalysisManager::hashSet(const ExprSet &S) {
  // Hash by AST ids in sorted order (set already sorted).
  std::size_t h = 0x9e3779b97f4a7c15ULL;
  for (const auto &e : S) {
    unsigned id = Z3_get_ast_id(e.ctx(), e);
    // combine
    h ^= std::hash<unsigned>{}(id) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  }
  return h;
}

uint32_t FeasibilityAnalysisManager::internSet(const ExprSet &S) {
  if (S.empty()) {
    return FeasibilityElement::topId;
  }

  const std::size_t h = hashSet(S);

  std::lock_guard<std::mutex> lock(SetsMutex);
  auto &bucket = SetsCache[h];

  // Check existing candidates for equality to avoid hash collision issues.
  for (uint32_t cand : bucket) {
    auto set = getSet(cand);
    if (set.size() != S.size()) continue;
    if (set == S) return cand;
  }

  const uint32_t newId = static_cast<uint32_t>(Sets.size());
  Sets.push_back(S);
  bucket.push_back(newId);
  return newId;
}

uint32_t FeasibilityAnalysisManager::addAtom(uint32_t baseId,
                                             const z3::expr &atom) {
  if (baseId == FeasibilityElement::bottomId) {
    // Not used in our representation; Bottom is Kind::Bottom.
    return baseId;
  }

  ExprSet S = getSet(baseId);
  S.insert(atom);
  return internSet(S);
}

uint32_t FeasibilityAnalysisManager::intersect(uint32_t aId, uint32_t bId) {
  // Intersection with Top (empty) yields Top.
  if (aId == FeasibilityElement::topId || bId == FeasibilityElement::topId)
    return FeasibilityElement::topId;

  const ExprSet &A = getSet(aId);
  const ExprSet &B = getSet(bId);

  ExprSet Out;
  std::set_intersection(A.begin(), A.end(), B.begin(), B.end(),
                        std::inserter(Out, Out.begin()), ExpressionComperator{});

  return internSet(Out);
}

std::optional<uint32_t> FeasibilityAnalysisManager::findSingletonId(const z3::expr &atom) const {
  ExprSet S;
  S.insert(atom);
  const std::size_t h = hashSet(S);

  std::lock_guard<std::mutex> lock(SetsMutex);
  auto it = SetsCache.find(h);
  if (it == SetsCache.end()) return std::nullopt;

  for (uint32_t cand : it->second) {
    auto set = getSet(cand);
    if (set.size() == 1 && *set.begin() == atom) return cand;
  }
  return std::nullopt;
}

void FeasibilityAnalysisManager::ensureEnvZeroInitialized() {
  if (!EnvRoots.empty()) return;
  EnvRoots.push_back(nullptr); // envId 0: empty environment
}

bool FeasibilityAnalysisManager::hasEnv(uint32_t id) const noexcept {
  return id < EnvRoots.size();
}

const llvm::Value *FeasibilityAnalysisManager::lookupEnv(uint32_t envId, const llvm::Value *k) const {
  if (!k) {
    return nullptr;
  }

  if (envId == 0 || envId >= EnvRoots.size()) {
    return nullptr;
  }

  for (auto *n = EnvRoots[envId]; n; n = n->parent) {
    if (n->key == k)
      return n->val;
  }
  return nullptr;
}

const llvm::Value *FeasibilityAnalysisManager::resolve(
    uint32_t envId, const llvm::Value *v) const {
  if (!v) return v;
  if (envId == 0 || envId >= EnvRoots.size()) return v;

  // One-step resolution is usually enough for PHI substitution, but we do a
  // small fixed-point to handle chains safely (a -> b -> c).
  const llvm::Value *cur = v;
  for (;;) {
    const llvm::Value *next = lookupEnv(envId, cur);
    if (!next || next == cur) return cur;
    cur = next;
  }
}

uint32_t FeasibilityAnalysisManager::extendEnv(uint32_t baseEnvId,
                                               const llvm::Value *k,
                                               const llvm::Value *v) {
  if (!k || !v) return baseEnvId;

  ensureEnvZeroInitialized();
  if (baseEnvId >= EnvRoots.size()) baseEnvId = 0;

  // Avoid pointless self-bindings
  if (k == v) return baseEnvId;

  // Intern to prevent envId explosion
  EnvKey ek{baseEnvId, k, v};

  std::lock_guard<std::mutex> L(EnvInternMu);
  if (auto it = EnvCache.find(ek); it != EnvCache.end()) {
    return it->second;
  }

  EnvPool.push_back(EnvNode{EnvRoots[baseEnvId], k, v});
  EnvRoots.push_back(&EnvPool.back());
  const uint32_t newId = static_cast<uint32_t>(EnvRoots.size() - 1);

  EnvCache.emplace(ek, newId);
  return newId;
}

uint32_t FeasibilityAnalysisManager::applyPhiPack(uint32_t inEnvId, const llvm::BasicBlock *pred,
const llvm::BasicBlock *succ) {
  ensureEnvZeroInitialized();

  if (inEnvId >= EnvRoots.size()) inEnvId = 0;
  if (!pred || !succ) return inEnvId;
  if (pred == succ) return inEnvId; // ignore self-edge

  uint32_t env = inEnvId;

  // Iterate PHIs at top of succ
  for (auto &I : *succ) {
    auto *phi = llvm::dyn_cast<llvm::PHINode>(&I);
    if (!phi) break;

    const int idx = phi->getBasicBlockIndex(pred);
    if (idx < 0) continue;

    const llvm::Value *incoming = phi->getIncomingValue(idx);
    incoming = resolve(env, incoming);

    // Avoid phi -> phi cycles
    if (incoming == phi) continue;

    // If already bound to same incoming, skip
    if (const llvm::Value *existing = lookupEnv(env, phi)) {
      if (existing == incoming) continue;
    }

    env = extendEnv(env, phi, incoming);
  }

  return env;
}

}