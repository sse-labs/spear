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
// Stable EF identity
// ============================================================================

FeasibilityAnalysisManager::EFStableId
FeasibilityAnalysisManager::efStableId(const EF &F) const {
  /*static std::atomic<uint64_t> dbg{0};
  auto n = ++dbg;
  if ((n % 10000) == 0) {
    llvm::errs() << "[FDBG] efStableId calls=" << n
                 << " nextId=" << NextEFStableId.load()
                 << " mapSize=" << EFIdIntern.size() << "\n";
  }*/

  // fixed ids for canonical singletons (these must be truly canonical)
  if (F.template isa<psr::EdgeIdentity<l_t>>()) return 1;
  if (F.template isa<FeasibilityAllTopEF>())    return 2;
  if (F.template isa<FeasibilityAllBottomEF>()) return 3;

  // Use *concrete object address* as identity where possible.
  const void *semantic = nullptr;

  // Concrete Feasibility EFs:
  if (auto *p = F.template dyn_cast<FeasibilityPHITranslateEF>())      semantic = p;
  else if (auto *p = F.template dyn_cast<FeasibilityAddConstrainEF>()) semantic = p;
  else if (auto *p = F.template dyn_cast<FeasibilityANDFormulaEF>())   semantic = p;
  else if (auto *p = F.template dyn_cast<FeasibilityORFormulaEF>())    semantic = p;
  else if (auto *p = F.template dyn_cast<FeasibilityComposeEF>())      semantic = p;
  else if (auto *p = F.template dyn_cast<FeasibilityJoinEF>())         semantic = p;

  // psr builtins beyond EdgeIdentity:
  else if (auto *p = F.template dyn_cast<psr::AllTop<l_t>>())          semantic = p;
  else if (auto *p = F.template dyn_cast<psr::AllBottom<l_t>>())       semantic = p;

  // Fallback: wrapper identity (only if truly unknown).
  if (!semantic)
    semantic = F.getOpaqueValue();

  std::lock_guard<std::mutex> L(EFIdInternMu);
  auto it = EFIdIntern.find(semantic);
  if (it != EFIdIntern.end()) return it->second;

  EFStableId id = NextEFStableId.fetch_add(1, std::memory_order_relaxed);
  EFIdIntern.emplace(semantic, id);
  return id;
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

uint32_t FeasibilityAnalysisManager::mkOr(uint32_t aId, uint32_t bId) {
  if (aId == FeasibilityElement::topId || bId == FeasibilityElement::topId)
    return FeasibilityElement::topId;
  if (aId == FeasibilityElement::bottomId)
    return bId;
  if (bId == FeasibilityElement::bottomId)
    return aId;
  if (aId == bId)
    return aId;

  if (aId > bId)
    std::swap(aId, bId);

  auto a = getExpression(aId);
  auto b = getExpression(bId);

  z3::expr res = mkOrSimplified(a, b);

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
bool FeasibilityAnalysisManager::isNotOf(const z3::expr &a,
                                         const z3::expr &b) {
  return isNotExpr(a) && Z3_is_eq_ast(a.ctx(), a.arg(0), b);
}

void FeasibilityAnalysisManager::collectOrArgs(const z3::expr &e,
                                               std::vector<z3::expr> &out) {
  if (e.is_app() && e.decl().decl_kind() == Z3_OP_OR) {
    for (unsigned i = 0; i < e.num_args(); ++i)
      collectOrArgs(e.arg(i), out);
  } else {
    out.push_back(e);
  }
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

z3::expr FeasibilityAnalysisManager::mkNaryOr(
    z3::context &ctx, const std::vector<z3::expr> &v) {
  std::vector<Z3_ast> asts;
  asts.reserve(v.size());
  for (auto &e : v)
    asts.push_back(e);
  return z3::expr(ctx, Z3_mk_or(ctx, (unsigned)asts.size(), asts.data()));
}

z3::expr FeasibilityAnalysisManager::mkNaryAnd(
    z3::context &ctx, const std::vector<z3::expr> &v) {
  std::vector<Z3_ast> asts;
  asts.reserve(v.size());
  for (auto &e : v)
    asts.push_back(e);
  return z3::expr(ctx, Z3_mk_and(ctx, (unsigned)asts.size(), asts.data()));
}

z3::expr FeasibilityAnalysisManager::mkOrSimplified(const z3::expr &a,
                                                    const z3::expr &b) {
  z3::context &ctx = a.ctx();

  std::vector<z3::expr> args;
  args.reserve(8);
  collectOrArgs(a, args);
  collectOrArgs(b, args);

  std::unordered_set<z3::expr, Z3ExprHash, Z3ExprEq> seen;
  seen.reserve(args.size());

  std::unordered_set<z3::expr, Z3ExprHash, Z3ExprEq> pos, neg;
  pos.reserve(args.size());
  neg.reserve(args.size());

  std::vector<z3::expr> uniq;
  uniq.reserve(args.size());

  for (auto &x : args) {
    if (isBoolTrue(x))
      return ctx.bool_val(true);
    if (isBoolFalse(x))
      continue;

    if (isNotExpr(x)) {
      z3::expr base = x.arg(0);
      if (pos.find(base) != pos.end())
        return ctx.bool_val(true);
      neg.insert(base);
    } else {
      if (neg.find(x) != neg.end())
        return ctx.bool_val(true);
      pos.insert(x);
    }

    if (seen.insert(x).second)
      uniq.push_back(x);
  }

  if (uniq.empty())
    return ctx.bool_val(false);
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

  return mkNaryOr(ctx, uniq);
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

// ============================================================================
// Join / Compose canonicalization helpers
// ============================================================================

void FeasibilityAnalysisManager::collectJoinOps(const EF &E,
                                                llvm::SmallVectorImpl<EF> &out) const {
  if (auto *J = E.template dyn_cast<FeasibilityJoinEF>()) {
    collectJoinOps(J->Left, out);
    collectJoinOps(J->Right, out);
    return;
  }
  out.push_back(E);
}

// ✅ FIXED: compute stable ids ONCE, then sort+dedup by those ids.
// This removes the efStableId() explosion from sort comparators.
void FeasibilityAnalysisManager::normalizeJoinOps(llvm::SmallVectorImpl<EF> &ops) const {
  bool hasTop = false;

  llvm::SmallVector<EF, 16> tmp;
  tmp.reserve(ops.size());

  for (auto &E : ops) {
    if (E.template isa<FeasibilityAllBottomEF>() ||
        E.template isa<psr::AllBottom<l_t>>()) {
      continue;
    }
    if (E.template isa<FeasibilityAllTopEF>() ||
        E.template isa<psr::AllTop<l_t>>()) {
      hasTop = true;
      break;
    }
    tmp.push_back(E);
  }

  ops.clear();
  if (hasTop) {
    ops.push_back(AllTopEF);
    return;
  }
  if (tmp.empty()) {
    ops.push_back(AllBottomEF);
    return;
  }

  llvm::SmallVector<std::pair<EFStableId, EF>, 16> tagged;
  tagged.reserve(tmp.size());
  for (auto &E : tmp)
    tagged.emplace_back(efStableId(E), E);

  llvm::sort(tagged, [](const auto &a, const auto &b) {
    return a.first < b.first;
  });

  tagged.erase(std::unique(tagged.begin(), tagged.end(),
                           [](const auto &a, const auto &b) {
                             return a.first == b.first;
                           }),
               tagged.end());

  ops.reserve(tagged.size());
  for (auto &p : tagged)
    ops.push_back(p.second);
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
    if (E.template isa<FeasibilityAllBottomEF>()) {
      chain.clear();
      chain.push_back(AllBottomEF);
      return;
    }
    if (E.template isa<FeasibilityAllTopEF>()) {
      chain.clear();
      chain.push_back(AllTopEF);
      return;
    }
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

  if (A.template isa<FeasibilityAllBottomEF>()) return A;
  if (B.template isa<FeasibilityAllBottomEF>()) return B;

  if (A.template isa<FeasibilityAllTopEF>()) return A;
  if (B.template isa<FeasibilityAllTopEF>()) return B;

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

FeasibilityAnalysisManager::EF
FeasibilityAnalysisManager::internComposeOpaque(EFStableId aId, const EF &A,
                                                EFStableId bId, const EF &B) {
  static std::atomic<uint64_t> dbg{0};
  uint64_t n = ++dbg;
  if (n % 10000 == 0) {
    llvm::errs() << "[FDBG] ComposeIntern hits=" << ComposeInternHits
                 << " miss=" << ComposeInternMiss
                 << " size=" << ComposeIntern.size() << "\n";
  }
  return internComposeById(aId, A, bId, B);
}

FeasibilityAnalysisManager::EF
FeasibilityAnalysisManager::internCompose(const EF &A0, const EF &B0) {
  // Fast paths
  if (A0.template isa<psr::EdgeIdentity<l_t>>()) return B0;
  if (B0.template isa<psr::EdgeIdentity<l_t>>()) return A0;
  if (A0.template isa<FeasibilityAllBottomEF>()) return A0;
  if (B0.template isa<FeasibilityAllBottomEF>()) return B0;
  if (A0.template isa<FeasibilityAllTopEF>()) return A0;
  if (B0.template isa<FeasibilityAllTopEF>()) return B0;

  // Canonicalize: right-associate + remove identities + reuse singletons
  llvm::SmallVector<EF, 16> chain;
  chain.reserve(8);

  llvm::SmallVector<EF, 16> left;
  collectComposeChainLeft(A0, left);

  for (auto &E : left) chain.push_back(E);
  chain.push_back(B0);

  normalizeComposeChain(chain);

  if (chain.size() == 1) return chain[0];

  // ✅ FIXED: keep accId so we don't call efStableId(acc) multiple times per step.
  EF acc = chain.back();
  EFStableId accId = efStableId(acc);

  for (int i = (int)chain.size() - 2; i >= 0; --i) {
    const EF &A = chain[i];
    const EFStableId aId = efStableId(A);
    acc = internComposeById(aId, A, accId, acc);
    accId = efStableId(acc); // one call per fold step
  }

  return acc;
}

// ============================================================================
// Interning: Join (n-ary cache keyed by stable ids)
// ============================================================================

FeasibilityAnalysisManager::EF
FeasibilityAnalysisManager::buildBalancedJoinTree(const llvm::SmallVectorImpl<EF> &ops,
                                                  size_t lo, size_t hi) const {
  if (hi - lo == 1) return ops[lo];
  size_t mid = lo + (hi - lo) / 2;
  EF L = buildBalancedJoinTree(ops, lo, mid);
  EF R = buildBalancedJoinTree(ops, mid, hi);
  return EF(std::in_place_type<FeasibilityJoinEF>,
            const_cast<FeasibilityAnalysisManager *>(this), L, R);
}

FeasibilityAnalysisManager::EF
FeasibilityAnalysisManager::internJoinOpaque(EFStableId /*aId*/, const EF &A,
                                             EFStableId /*bId*/, const EF &B) {
  static std::atomic<uint64_t> dbg{0};
  uint64_t n = ++dbg;
  if (n % 1000 == 0) {
    llvm::errs() << "[FDBG] JoinIntern hits=" << JoinInternHits
                 << " miss=" << JoinInternMiss
                 << " inserts=" << JoinInternInserts
                 << " size=" << JoinInternNary.size() << "\n";
  }
  // Join is fully canonicalized n-ary in internJoin()
  return internJoin(A, B);
}

FeasibilityAnalysisManager::EF
FeasibilityAnalysisManager::internJoin(const EF &A0, const EF &B0) {
  // absorbers / neutrals
  if (A0.template isa<FeasibilityAllTopEF>())    return A0;
  if (B0.template isa<FeasibilityAllTopEF>())    return B0;
  if (A0.template isa<FeasibilityAllBottomEF>()) return B0;
  if (B0.template isa<FeasibilityAllBottomEF>()) return A0;

  llvm::SmallVector<EF, 16> ops;
  ops.reserve(8);

  collectJoinOps(A0, ops);
  collectJoinOps(B0, ops);

  // ✅ FIXED: normalize without comparator-calling efStableId
  normalizeJoinOps(ops);

  if (ops.size() == 1)
    return ops[0];

  // Build EFVecKey from stable ids (one efStableId call per operand).
  EFVecKey key;
  key.ids.reserve(ops.size());
  for (const EF &x : ops)
    key.ids.push_back(efStableId(x));

  // n-ary cache lookup
  {
    std::lock_guard<std::mutex> L(JoinInternMu);
    if (auto it = JoinInternNary.find(key); it != JoinInternNary.end()) {
      ++JoinInternHits;
      return it->second;
    }
  }
  ++JoinInternMiss;

  EF acc = buildBalancedJoinTree(ops, 0, ops.size());

  {
    std::lock_guard<std::mutex> L(JoinInternMu);
    auto [it, inserted] = JoinInternNary.emplace(std::move(key), acc);
    if (inserted) ++JoinInternInserts;
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

FeasibilityAnalysisManager::EFStableId
FeasibilityAnalysisManager::efStableIdFromOpaque(const void *opaque) const {
  if (!opaque) return 0;

  std::lock_guard<std::mutex> L(EFIdInternMu);
  auto it = EFIdIntern.find(opaque);
  if (it != EFIdIntern.end()) return it->second;

  EFStableId id = NextEFStableId.fetch_add(1, std::memory_order_relaxed);
  EFIdIntern.emplace(opaque, id);
  return id;
}

// ===================== FeasibilityElement =====================

FeasibilityElement
FeasibilityElement::createElement(FeasibilityAnalysisManager *man,
                                  uint32_t formulaId, Kind type,
                                  uint32_t envId) {
  return FeasibilityElement(type, formulaId, man, envId);
}

FeasibilityElement FeasibilityElement::join(FeasibilityElement &other) const {
  if (this->isBottom()) return *this;
  if (other.isBottom()) return other;
  if (this->isTop()) return other;
  if (other.isTop()) return *this;

  uint32_t newPC = manager->mkOr(this->formularID, other.formularID);

  // env join must be finite-height, otherwise loops can diverge
  uint32_t newEnv = (this->envId == other.envId) ? this->envId : 0;

  return FeasibilityElement(Kind::Normal, newPC, manager, newEnv);
}

std::string FeasibilityElement::toString() const {
  if (kind == Kind::Bottom) return "⊥";
  if (kind == Kind::Top) return "⊤";
  auto formula = this->manager->getExpression(this->formularID);
  std::string s;
  llvm::raw_string_ostream rso(s);
  rso << formula.to_string();
  rso.flush();
  return s;
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