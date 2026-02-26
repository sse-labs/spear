//
// Created by mkrebs on 25.02.26.
//

#include "analyses/feasibility/FeasibilityAnalysisManager.h"

namespace Feasibility {

FeasibilityAnalysisManager::FeasibilityAnalysisManager(std::unique_ptr<z3::context> ctx)
: Context(std::move(ctx)), Solver(*Context) {
  // Default the top element to the empty set of formulas, which is represented by an empty set in our representation.
  Sets.clear();
  Sets.resize(2);
}

std::vector<z3::expr> FeasibilityAnalysisManager::getPureSet(uint32_t id) const {
  // Check if the id is in bounds
  if (id >= Sets.size()) {
    return {};
  }

  // Get the set of formulas corresponding to the given ID and return it as a vector.
  const ExprSet &S = Sets[id];

  // Convert the set to a vector and return it.
  return std::vector<z3::expr>(S.begin(), S.end());
}

std::size_t FeasibilityAnalysisManager::hashSet(const ExprSet &S) {
  // Hash by AST ids in sorted order
  std::size_t h = 0x9e3779b97f4a7c15ULL;
  for (const auto &e : S) {
    unsigned id = Z3_get_ast_id(e.ctx(), e);
    h ^= std::hash<unsigned>{}(id) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  }
  return h;
}

uint32_t FeasibilityAnalysisManager::internSet(const ExprSet &S) {
  // If the set is empty, we return the reserved ID for the top element, which is the empty set of formulas
  if (S.empty()) {
    return FeasibilityElement::topId;
  }

  // Otherwise calculate the hash
  const std::size_t h = hashSet(S);

  // Query the cache for the bucket containing candidates with the same hash
  std::lock_guard<std::mutex> lock(SetsMutex);
  auto &bucket = SetsCache[h];

  // Check existing candidates for equality to avoid hash collision issues.
  for (uint32_t cand : bucket) {
    auto set = getSet(cand);

    if (set.size() != S.size()) {
      continue;
    }

    // If we find a valid return the set id
    if (set == S) {
      return cand;
    }
  }

  // If we did not find a valid candidate, we need to add the new set to the manager and cache it.
  // We add it to the end of the set storage and return the new ID.
  const uint32_t newId = static_cast<uint32_t>(Sets.size());
  Sets.push_back(S);
  bucket.push_back(newId);
  return newId;
}

uint32_t FeasibilityAnalysisManager::addAtom(uint32_t baseId, const z3::expr &atom) {
  // Ignore bottom
  if (baseId == FeasibilityElement::bottomId) {
    return baseId;
  }

  // Otherwise get the set corresponding to the baseId, add the atom and intern the new set to get the resulting ID.
  ExprSet S = getSet(baseId);
  S.insert(atom);
  return internSet(S);
}

uint32_t FeasibilityAnalysisManager::intersect(uint32_t aId, uint32_t bId) {
  // If either set is top (empty set of formulas), the result is the other set, as top is neutral for intersection.
  if (aId == FeasibilityElement::topId || bId == FeasibilityElement::topId) {
    return FeasibilityElement::topId;
  }

  // Get both sets
  const ExprSet &A = getSet(aId);
  const ExprSet &B = getSet(bId);

  // Calculate the intersection of the two sets and intern the result to get the resulting ID.
  ExprSet Out;
  std::set_intersection(A.begin(), A.end(), B.begin(), B.end(),
                        std::inserter(Out, Out.begin()), ExpressionComperator{});

  return internSet(Out);
}

void FeasibilityAnalysisManager::ensureEnvZeroInitialized() {
  if (!EnvRoots.empty()) {
    return;
  }

  EnvRoots.push_back(nullptr);
}

bool FeasibilityAnalysisManager::hasEnv(uint32_t id) const noexcept {
  return id < EnvRoots.size();
}

const llvm::Value *FeasibilityAnalysisManager::lookupEnv(uint32_t envId, const llvm::Value *key) const {
  // If key is null, we cannot have a valid binding, so we return nullptr immediately.
  if (!key) {
    return nullptr;
  }

  // If envId is out of bounds, we return nullptr to indicate that no binding was found.
  // This also handles the case of envId 0, which represents the empty environment with no bindings.
  if (envId == 0 || envId >= EnvRoots.size()) {
    return nullptr;
  }

  // Start at the env root corresponding to envId and iterate up the parent chain to find a binding for the given key.
  for (auto *n = EnvRoots[envId]; n; n = n->parent) {
    if (n->key == key) {
      return n->val;
    }
  }

  // If the iteration completes without finding a binding for the key,
  // we return nullptr to indicate that no binding was found in this environment.
  return nullptr;
}

const llvm::Value *FeasibilityAnalysisManager::resolve(uint32_t envId, const llvm::Value *val) const {
  // If no value is given, we cannot resolve anything, so we return nullptr immediately.
  if (!val) {
    return val;
  }

  // If envId is out of bounds, we return the original value to indicate that no resolution could be performed.
  // This also handles the case of envId 0, which represents the empty environment with no bindings,
  // so we return the original value in this case as well.
  if (envId == 0 || envId >= EnvRoots.size()) {
    return val;
  }

  // One-step resolution is usually enough for PHI substitution, but we do a
  // small fixed-point to handle chains safely (a -> b -> c).
  const llvm::Value *cur = val;

  while (true) {
    const llvm::Value *next = lookupEnv(envId, cur);

    if (!next || next == cur) {
      return cur;
    }

    cur = next;
  }
}

uint32_t FeasibilityAnalysisManager::extendEnv(uint32_t baseEnvId, const llvm::Value *key, const llvm::Value *val) {
  // If key or value is null, we cannot have a valid binding, so we return the original environment ID immediately.
  if (!key || !val) {
    return baseEnvId;
  }

  // Make sure the empty environment is initialized and handle out-of-bounds envId by treating it as the empty
  // environment.
  ensureEnvZeroInitialized();

  // If the baseEnvId is out of bounds, we treat it as the empty environment (envId 0)
  // to ensure that we have a valid base environment to extend from.
  if (baseEnvId >= EnvRoots.size()) {
    baseEnvId = 0;
  }

  // Avoid pointless self-bindings
  if (key == val) {
    return baseEnvId;
  }

  // Cache to prevent envId explosion
  EnvKey ek{baseEnvId, key, val};

  std::lock_guard<std::mutex> L(EnvInternMu);

  // Search for existing cached value
  if (auto it = EnvCache.find(ek); it != EnvCache.end()) {
    return it->second;
  }

  // Add a new environment node to the environment storage, which extends the environment represented by
  // baseEnvId with the new binding from key to val.
  EnvPool.push_back(EnvNode{EnvRoots[baseEnvId], key, val});
  EnvRoots.push_back(&EnvPool.back());

  // Get the newest id
  const uint32_t newId = static_cast<uint32_t>(EnvRoots.size() - 1);

  // Add the new environment to the cache and return the new ID.
  EnvCache.emplace(ek, newId);
  return newId;
}

uint32_t FeasibilityAnalysisManager::applyPhiPack(uint32_t inEnvId,
                                                  const llvm::BasicBlock *pred,
                                                  const llvm::BasicBlock *succ) {
  // Make sure the empty environment is initialized and handle out-of-bounds envId by treating it as the empty
  // environment.
  ensureEnvZeroInitialized();

  // If the input environment ID is out of bounds, we treat it as the empty environment (envId 0)
  if (inEnvId >= EnvRoots.size()) {
    inEnvId = 0;
  }

  // If we do not have predecessor or successor blocks, we cannot apply any PHI node effects,
  // so we return the original environment ID immediately.
  if (!pred || !succ) {
    return inEnvId;
  }

  // If the predecessor and successor blocks are the same, we are in a degenerate case where we cannot have
  // any PHI node effects,
  if (pred == succ) {
    return inEnvId;
  }

  uint32_t env = inEnvId;

  // Iterate PHIs at top of succ
  for (auto &I : *succ) {
    // Get the phi node
    auto *phi = llvm::dyn_cast<llvm::PHINode>(&I);
    if (!phi) {
      break;
    }

    // Get the incoming value for the predecessor block
    const int idx = phi->getBasicBlockIndex(pred);
    if (idx < 0) {
      continue;
    }

    // Resolve the incoming value in the current environment to apply any existing bindings.
    const llvm::Value *incoming = phi->getIncomingValue(idx);
    // Resole the incoming value in the current environment to apply any existing bindings.
    // This is important to ensure that we correctly handle cases where the incoming value is itself
    // defined by a PHI node or has bindings in the current environment that need to be applied.
    incoming = resolve(env, incoming);

    // Avoid phi -> phi cycles
    if (incoming == phi) {
      continue;
    }

    // If already bound to same incoming, skip
    if (const llvm::Value *existing = lookupEnv(env, phi)) {
      if (existing == incoming) {
        continue;
      }
    }

    // Otherwise extend the environment with the new binding from the phi node to the incoming value and update
    // the current environment ID.
    env = extendEnv(env, phi, incoming);
  }

  return env;
}

}