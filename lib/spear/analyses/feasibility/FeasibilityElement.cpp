// FeasibilityElement.cpp
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityElement.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/Instructions.h> // PHINode
#include <llvm/Support/raw_ostream.h>

#include "analyses/feasibility/util.h"

// Needed for in_place_type<FeasibilityComposeEF / FeasibilityJoinEF>
#include "analyses/feasibility/FeasibilityEdgeFunction.h"

namespace Feasibility {

// ============================================================================
// FeasibilityAnalysisManager ctor
// ============================================================================

FeasibilityAnalysisManager::FeasibilityAnalysisManager(
    std::unique_ptr<z3::context> ctx)
    : OwnedContext(std::move(ctx)),
      Solver(*OwnedContext),
      IdentityEF(std::in_place_type<psr::EdgeIdentity<l_t>>),
      AllTopEF(std::in_place_type<FeasibilityAllTopEF>),
      AllBottomEF(std::in_place_type<FeasibilityAllBottomEF>) {

  Context = OwnedContext.get();
  Formulas = std::vector<z3::expr>();

  // 0: true (Top)
  Formulas.push_back(Context->bool_val(true));
  // 1: false (Bottom)
  Formulas.push_back(Context->bool_val(false));
  // 2: initial true for normal values (your convention)
  Formulas.push_back(Context->bool_val(true));

  // IMPORTANT: intern these too, otherwise findFormulaId(true/false) misses.
  FormularsToId.emplace(Formulas[0], 0);
  FormularsToId.emplace(Formulas[1], 1);
  FormularsToId.emplace(Formulas[2], 2);

  SatCache.push_back(SatTri::Sat);    // id 0
  SatCache.push_back(SatTri::Unsat);  // id 1
  SatCache.push_back(SatTri::Sat);    // id 2

  // envId 0 is "empty env"
  EnvRoots.push_back(nullptr);
}


// ============================================================================
// Formula creation / simplification (unchanged)
// ============================================================================

uint32_t FeasibilityAnalysisManager::mkAtomic(z3::expr a) {
  if (auto pid = findFormulaId(a))
    return *pid;
  Formulas.push_back(a);
  uint32_t id = (uint32_t)Formulas.size() - 1;
  FormularsToId.emplace(a, id);
  SatCache.push_back(SatTri::Unknown);
  return id;
}

uint32_t FeasibilityAnalysisManager::mkAnd(uint32_t aId, uint32_t bId) {
  if (aId == FeasibilityElement::bottomId ||
      bId == FeasibilityElement::bottomId)
    return FeasibilityElement::bottomId;
  if (aId == FeasibilityElement::topId)
    return bId;
  if (bId == FeasibilityElement::topId)
    return aId;
  if (aId == bId)
    return aId;

  if (aId > bId)
    std::swap(aId, bId);

  auto a = getExpression(aId);
  auto b = getExpression(bId);

  z3::expr res = mkAndSimplified(a, b);

  if (auto pid = findFormulaId(res))
    return *pid;

  Formulas.push_back(res);
  uint32_t id = (uint32_t)Formulas.size() - 1;
  FormularsToId.emplace(res, id);
  SatCache.push_back(SatTri::Unknown);
  return id;
}

bool FeasibilityAnalysisManager::isBoolTrue(const z3::expr &e) {
  return e.is_true();
}
bool FeasibilityAnalysisManager::isBoolFalse(const z3::expr &e) {
  return e.is_false();
}

bool FeasibilityAnalysisManager::isNotExpr(const z3::expr &e) {
  return e.is_app() && e.decl().decl_kind() == Z3_OP_NOT && e.num_args() == 1;
}
bool FeasibilityAnalysisManager::isNotOf(const z3::expr &a, const z3::expr &b) {
  return isNotExpr(a) && Z3_is_eq_ast(a.ctx(), a.arg(0), b);
}


void FeasibilityAnalysisManager::collectAndArgs(const z3::expr &e,
                                                std::vector<z3::expr> &out) {
  if (e.is_app() && e.decl().decl_kind() == Z3_OP_AND) {
    for (unsigned i = 0; i < e.num_args(); ++i)
      collectAndArgs(e.arg(i), out);
  } else {
    out.push_back(e);
  }
}

z3::expr FeasibilityAnalysisManager::mkNaryAnd(
    z3::context &ctx, const std::vector<z3::expr> &v) {
  std::vector<Z3_ast> asts;
  asts.reserve(v.size());
  for (auto &e : v)
    asts.push_back(e);
  return z3::expr(ctx, Z3_mk_and(ctx, (unsigned)asts.size(), asts.data()));
}

z3::expr FeasibilityAnalysisManager::mkAndSimplified(const z3::expr &a,
                                                     const z3::expr &b) {
  z3::context &ctx = a.ctx();

  std::vector<z3::expr> args;
  args.reserve(8);
  collectAndArgs(a, args);
  collectAndArgs(b, args);

  std::unordered_set<z3::expr, Z3ExprHash, Z3ExprEq> seen;
  seen.reserve(args.size());

  std::unordered_set<z3::expr, Z3ExprHash, Z3ExprEq> pos, neg;
  pos.reserve(args.size());
  neg.reserve(args.size());

  std::vector<z3::expr> uniq;
  uniq.reserve(args.size());

  for (auto &x : args) {
    if (isBoolFalse(x))
      return ctx.bool_val(false);
    if (isBoolTrue(x))
      continue;

    if (isNotExpr(x)) {
      z3::expr base = x.arg(0);
      if (pos.find(base) != pos.end())
        return ctx.bool_val(false);
      neg.insert(base);
    } else {
      if (neg.find(x) != neg.end())
        return ctx.bool_val(false);
      pos.insert(x);
    }

    if (seen.insert(x).second)
      uniq.push_back(x);
  }

  if (uniq.empty())
    return ctx.bool_val(true);
  if (uniq.size() == 1)
    return uniq[0];

  std::sort(uniq.begin(), uniq.end(),
            [](const z3::expr &lhs, const z3::expr &rhs) {
              auto hl = Z3_get_ast_hash(lhs.ctx(), lhs);
              auto hr = Z3_get_ast_hash(rhs.ctx(), rhs);
              if (hl != hr) return hl < hr;
              if (Z3_is_eq_ast(lhs.ctx(), lhs, rhs)) return false;
              return Z3_ast(lhs) < Z3_ast(rhs);
            });

  return mkNaryAnd(ctx, uniq);
}

z3::expr FeasibilityAnalysisManager::simplify(z3::expr input) {
  if (input.is_true())
    return Context->bool_val(true);
  if (input.is_false())
    return Context->bool_val(false);
  return input;
}

uint32_t FeasibilityAnalysisManager::mkNot(z3::expr a) {
  if (a.is_true())
    return FeasibilityElement::bottomId;
  if (a.is_false())
    return FeasibilityElement::topId;

  if (isNotExpr(a)) {
    z3::expr inner = a.arg(0);
    if (auto pid = findFormulaId(inner))
      return *pid;
    Formulas.push_back(inner);
    uint32_t id = (uint32_t)Formulas.size() - 1;
    FormularsToId.emplace(inner, id);
    SatCache.push_back(SatTri::Unknown);
    return id;
  }

  z3::expr f = !a;
  if (auto pid = findFormulaId(f))
    return *pid;
  Formulas.push_back(f);
  uint32_t id = (uint32_t)Formulas.size() - 1;
  FormularsToId.emplace(f, id);
  SatCache.push_back(SatTri::Unknown);
  return id;
}

const z3::expr &FeasibilityAnalysisManager::getExpression(uint32_t id) const {
  return Formulas[id];
}

std::optional<uint32_t>
FeasibilityAnalysisManager::findFormulaId(const z3::expr &expr) const {
  auto it = FormularsToId.find(expr);
  if (it == FormularsToId.end())
    return std::nullopt;
  return it->second;
}

bool FeasibilityAnalysisManager::isSat(uint32_t id) {
  if (id >= SatCache.size())
    return false;

  auto &c = SatCache[id];
  if (c == SatTri::Sat)
    return true;
  if (c == SatTri::Unsat)
    return false;

  const z3::expr e = getExpression(id);

  Solver.push();
  Solver.add(e);
  const bool sat = (Solver.check() == z3::sat);
  Solver.pop();

  c = sat ? SatTri::Sat : SatTri::Unsat;
  return sat;
}

void FeasibilityAnalysisManager::collectComposeChainLeft(
    const EF &E, llvm::SmallVectorImpl<EF> &out) const {
  if (auto *C = E.template dyn_cast<FeasibilityComposeEF>()) {
    collectComposeChainLeft(C->First, out);
    out.push_back(C->Second);
    return;
  }
  out.push_back(E);
}

void FeasibilityAnalysisManager::normalizeComposeChain(
    llvm::SmallVectorImpl<EF> &chain) const {
  llvm::SmallVector<EF, 16> tmp;
  tmp.reserve(chain.size());

  for (auto &E : chain) {
    if (E.template isa<psr::EdgeIdentity<l_t>>()) {
      continue;
    }

    // UNREACHABLE is absorbing
    if (E.template isa<FeasibilityAllTopEF>()) {
      chain.clear();
      chain.push_back(AllTopEF);
      return;
    }

    // IMPORTANT under NEW semantics:
    // AllBottomEF is *not* absorbing; do NOT clear the chain.
    // It represents "reachable baseline reset" and must not erase later constraints.

    tmp.push_back(E);
  }

  chain = std::move(tmp);
  if (chain.empty()) {
    chain.push_back(IdentityEF);
  }
}

// ============================================================================
// Interning: Compose (keyed by stable ids)
// ============================================================================

FeasibilityAnalysisManager::EF
FeasibilityAnalysisManager::internComposeById(EFStableId aId, const EF &A,
                                              EFStableId bId, const EF &B) {
  // cheap canonical rules
  if (A.template isa<psr::EdgeIdentity<l_t>>()) return B;
  if (B.template isa<psr::EdgeIdentity<l_t>>()) return A;

  // UNREACHABLE (Top) is absorbing
  if (A.template isa<FeasibilityAllTopEF>()) return A;
  if (B.template isa<FeasibilityAllTopEF>()) return B;

  // IMPORTANT under NEW semantics:
  // AllBottomEF is NOT absorbing. Do NOT early-return here.

  EFPairKey key{aId, bId};
  {
    std::lock_guard<std::mutex> L(ComposeInternMu);
    if (auto it = ComposeIntern.find(key); it != ComposeIntern.end()) {
      ++ComposeInternHits;
      return it->second;
    }
  }
  ++ComposeInternMiss;

  EF composed(std::in_place_type<FeasibilityComposeEF>, this, A, B);

  {
    std::lock_guard<std::mutex> L(ComposeInternMu);
    auto [it, inserted] = ComposeIntern.emplace(key, composed);
    if (inserted) ++ComposeInternInserts;
    return it->second;
  }
}


// ============================================================================
// Environments (unchanged)
// ============================================================================

bool FeasibilityAnalysisManager::hasEnv(uint32_t id) const {
  return id < EnvRoots.size();
}

const llvm::Value *FeasibilityAnalysisManager::lookupEnv(
    uint32_t envId, const llvm::Value *k) const {
  if (envId == 0 || !hasEnv(envId))
    return nullptr;
  for (auto *n = EnvRoots[envId]; n; n = n->parent) {
    if (n->key == k)
      return n->val;
  }
  return nullptr;
}

const llvm::Value *FeasibilityAnalysisManager::lookupBinding(
    uint32_t envId, const llvm::Value *key) const {
  return lookupEnv(envId, key);
}

const llvm::Value *FeasibilityAnalysisManager::resolve(
    uint32_t envId, const llvm::Value *v) {
  if (!v)
    return v;
  if (envId == 0 || envId >= EnvRoots.size())
    return v;

  ResolveKey key{envId, v};

  {
    std::lock_guard<std::mutex> L(ResolveCacheMu);
    if (auto it = ResolveCache.find(key); it != ResolveCache.end()) {
      ++ResolveCacheHits;
      return it->second;
    }
  }

  ++ResolveCacheMiss;

  const llvm::Value *res = v;
  for (auto *n = EnvRoots[envId]; n; n = n->parent) {
    if (n->key == v) {
      res = n->val;
      break;
    }
  }

  {
    std::lock_guard<std::mutex> L(ResolveCacheMu);
    auto [it, inserted] = ResolveCache.emplace(key, res);
    if (inserted) ++ResolveCacheInserts;
  }

  return res;
}

uint32_t FeasibilityAnalysisManager::extendEnv(uint32_t baseEnvId,
                                               const llvm::Value *k,
                                               const llvm::Value *v) {
  if (!k || !v)
    return baseEnvId;
  if (baseEnvId >= EnvRoots.size())
    baseEnvId = 0;

  EnvKey ek{baseEnvId, k, v};
  if (auto it = EnvIntern.find(ek); it != EnvIntern.end()) {
    return it->second;
  }

  EnvPool.push_back(EnvNode{EnvRoots[baseEnvId], k, v});
  EnvRoots.push_back(&EnvPool.back());
  uint32_t newId = (uint32_t)EnvRoots.size() - 1;

  EnvIntern.emplace(ek, newId);
  return newId;
}

uint32_t FeasibilityAnalysisManager::applyPhiPack(uint32_t inEnvId,
                                                  const llvm::BasicBlock *pred,
                                                  const llvm::BasicBlock *succ) {
  if (inEnvId >= EnvRoots.size())
    inEnvId = 0;
  if (!pred || !succ)
    return inEnvId;

  if (pred == succ)
    return inEnvId;

  PhiKey key{inEnvId, pred, succ};

  {
    std::lock_guard<std::mutex> L(PhiCacheMu);
    if (auto it = PhiCache.find(key); it != PhiCache.end()) {
      ++PhiCacheHits;
      return it->second;
    }
  }

  ++PhiCacheMiss;

  uint32_t env = inEnvId;

  for (auto &I : *succ) {
    auto *phi = llvm::dyn_cast<llvm::PHINode>(&I);
    if (!phi)
      break;

    const int idx = phi->getBasicBlockIndex(pred);
    if (idx < 0)
      continue;

    const llvm::Value *incoming = phi->getIncomingValue(idx);
    incoming = resolve(env, incoming);

    if (incoming == phi)
      continue;

    if (const llvm::Value *existing = lookupEnv(env, phi)) {
      if (existing == incoming)
        continue;
    }

    env = extendEnv(env, phi, incoming);
  }

  {
    std::lock_guard<std::mutex> L(PhiCacheMu);
    auto [it, inserted] = PhiCache.emplace(key, env);
    if (inserted)
      ++PhiCacheInserts;
    return it->second;
  }
}

// ===================== FeasibilityElement =====================

FeasibilityElement
FeasibilityElement::createElement(FeasibilityAnalysisManager *man,
                                  uint32_t formulaId, Kind type,
                                  uint32_t envId) {
  return FeasibilityElement(type, formulaId, man, envId);
}

FeasibilityElement FeasibilityElement::join(FeasibilityElement &Other) const {
  // New semantics:
  //   Top    = UNREACHED
  //   Bottom = REACHED baseline
  //   Normal = REACHED with info
  const bool thisUnreached  = this->isTop();
  const bool otherUnreached = Other.isTop();

  // Unreached only if BOTH are unreached.
  if (thisUnreached && otherUnreached) {
    return FeasibilityElement::createElement(getManager(),
                                             FeasibilityElement::topId,
                                             Kind::Top,
                                             /*envId=*/0);
  }

  // Otherwise: reached (baseline reset)
  return FeasibilityElement::createElement(getManager(),
                                           FeasibilityElement::topId, // PC=true baseline
                                           Kind::Bottom,
                                           /*envId=*/0);
}

std::string FeasibilityElement::toString() const {
  switch (kind) {
    case Kind::Top:
      return "UNREACHED";
    case Kind::Bottom:
      return "REACHED"; // baseline reached
    case Kind::Normal: {
      auto formula = manager->getExpression(formularID);
      std::string s;
      llvm::raw_string_ostream rso(s);
      rso << "REACHED(pc=" << formularID << ", env=" << envId << "): " << formula.to_string();
      rso.flush();
      return s;
    }
  }
  return "?";
}

std::ostream &operator<<(std::ostream &os,
                         const std::optional<FeasibilityElement> &E) {
  return os << toString(E);
}

std::string toString(const std::optional<FeasibilityElement> &E) {
  if (E.has_value()) return E->toString();
  return "nullopt";
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const FeasibilityElement &E) {
  return os << E.toString();
}

} // namespace Feasibility