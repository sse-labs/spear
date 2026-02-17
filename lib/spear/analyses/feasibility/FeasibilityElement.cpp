/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityElement.h"

#include <chrono>
#include <unordered_map>
#include <utility>
#include <vector>

#include <z3++.h>

#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/raw_os_ostream.h>

namespace Feasibility {

void AnalysisMetrics::reset() {
  totalSatCheckTime = 0;
  totalJoinTime = 0;
  totalAssumeTime = 0;
  totalEdgeFunctionTime = 0;
  satCheckCount = 0;
  satCacheHits = 0;
  satCacheMisses = 0;
  joinCount = 0;
  assumeCount = 0;
  edgeFunctionCount = 0;
  maxPcAstDepth = 0;
  maxPcAstNodes = 0;
  totalPcCreated = 0;
  maxPcPoolSize = 0;
  pcClearCalls = 0;
  maxSsaEnvSize = 0;
  maxMemEnvSize = 0;
  internCalls = 0;
  internInserted = 0;
  internHits = 0;
}

void AnalysisMetrics::report(llvm::raw_ostream &OS) const {
  OS << "\n========== Feasibility Analysis Metrics ==========\n";

  OS << "\n--- Timing (microseconds) ---\n";
  OS << "  SAT checks:        " << totalSatCheckTime.load() << " ns";
  if (satCheckCount.load() > 0) {
    OS << " (avg: " << (totalSatCheckTime.load() / satCheckCount.load()) << " ns/check)";
  }
  OS << "\n";
  OS << "  Joins:             " << totalJoinTime.load() << " ns\n";
  OS << "  Assumes:           " << totalAssumeTime.load() << " ns\n";
  OS << "  Edge functions:    " << totalEdgeFunctionTime.load() << " ns\n";

  uint64_t totalTime = totalSatCheckTime.load() + totalJoinTime.load() +
                       totalAssumeTime.load() + totalEdgeFunctionTime.load();
  OS << "  TOTAL:             " << totalTime << " ns ("
     << (totalTime / 1000.0) << " µs)\n";

  OS << "\n--- Operation Counts ---\n";
  OS << "  SAT checks:        " << satCheckCount.load() << "\n";
  OS << "    Cache hits:      " << satCacheHits.load() << "\n";
  OS << "    Cache misses:    " << satCacheMisses.load() << "\n";
  OS << "    Sum (should = checks): " << (satCacheHits.load() + satCacheMisses.load()) << "\n";
  if (satCheckCount.load() > 0) {
    OS << "    Hit rate:        "
       << (100.0 * satCacheHits.load() / satCheckCount.load()) << "%\n";
  }
  OS << "  Joins:             " << joinCount.load() << "\n";
  OS << "  Assumes:           " << assumeCount.load() << "\n";
  OS << "  Edge functions:    " << edgeFunctionCount.load() << "\n";

  OS << "\n--- Path Constraint Growth ---\n";
  OS << "  Total PCs created: " << totalPcCreated.load() << "\n";
  OS << "  Max PC pool size:  " << maxPcPoolSize.load() << "\n";
  OS << "  PC clear calls:    " << pcClearCalls.load() << "\n";
  OS << "  Max AST depth:     " << maxPcAstDepth.load() << "\n";
  OS << "  Max AST nodes:     " << maxPcAstNodes.load() << "\n";

  OS << "\n--- Store Metrics ---\n";
  OS << "  Max SSA env size:  " << maxSsaEnvSize.load() << "\n";
  OS << "  Max Mem env size:  " << maxMemEnvSize.load() << "\n";

  OS << "\n--- Interning Debug ---\n";
  OS << "  Intern calls:      " << internCalls.load() << "\n";
  OS << "  Intern inserted:   " << internInserted.load() << "\n";
  OS << "  Intern cache hits: " << internHits.load() << "\n";
  if (internCalls.load() > 0) {
    OS << "  Intern hit rate:   "
       << (100.0 * internHits.load() / internCalls.load()) << "%\n";
  }

  OS << "\n==================================================\n\n";
}

uint64_t FeasibilityStateStore::computeAstDepth(const z3::expr &E) const {
  if (!E.is_app()) {
    return 1;
  }

  uint64_t maxChildDepth = 0;
  for (unsigned i = 0; i < E.num_args(); ++i) {
    maxChildDepth = std::max(maxChildDepth, computeAstDepth(E.arg(i)));
  }
  return 1 + maxChildDepth;
}

uint64_t FeasibilityStateStore::computeAstNodes(const z3::expr &E) const {
  if (!E.is_app()) {
    return 1;
  }

  uint64_t total = 1;
  for (unsigned i = 0; i < E.num_args(); ++i) {
    total += computeAstNodes(E.arg(i));
  }
  return total;
}

FeasibilityElement::Kind FeasibilityElement::getKind() const noexcept {
  return kind;
}

FeasibilityStateStore *FeasibilityElement::getStore() const noexcept {
  return store;
}

bool operator==(const FeasibilityElement &A, const FeasibilityElement &B) noexcept {
  return A.equal_to(B);
}

bool operator!=(const FeasibilityElement &A, const FeasibilityElement &B) noexcept {
  return !A.equal_to(B);
}

FeasibilityElement FeasibilityElement::ideNeutral(FeasibilityStateStore *S) noexcept {
  return FeasibilityElement{S, true, Kind::IdeNeutral, 0, 0, 0};
}

FeasibilityElement FeasibilityElement::ideAbsorbing(FeasibilityStateStore *S) noexcept {
  // Compatibility alias: absorbing is infeasible.
  return FeasibilityElement{S,true, Kind::IdeAbsorbing, 0, 0, 0};
}

FeasibilityElement FeasibilityElement::top(FeasibilityStateStore *S) noexcept {
  return FeasibilityElement{S,true, Kind::Top, 0, 0, 0};
}

FeasibilityElement FeasibilityElement::bottom(FeasibilityStateStore *S) noexcept {
  return FeasibilityElement{S,true, Kind::Bottom, 0, 0, 0};
}

FeasibilityElement FeasibilityElement::initial(FeasibilityStateStore *S) noexcept {
  return FeasibilityElement{S,true, Kind::Normal, 0, 0, 0};
}

bool FeasibilityElement::isIdeNeutral() const noexcept {
  return kind == Kind::IdeNeutral;
}

bool FeasibilityElement::isIdeAbsorbing() const noexcept {
  return kind == Kind::IdeAbsorbing;
}

bool FeasibilityElement::isTop() const noexcept {
  return kind == Kind::Top;
}

bool FeasibilityElement::isBottom() const noexcept {
  return kind == Kind::Bottom;
}

bool FeasibilityElement::isNormal() const noexcept {
  return kind == Kind::Normal;
}

FeasibilityElement FeasibilityElement::assume(const z3::expr &cond) const {
  auto start = std::chrono::high_resolution_clock::now();

  if (store == nullptr) {
    return *this;
  }
  store->metrics.assumeCount++;

  if (isBottom() || isIdeAbsorbing()) {
    return *this;
  }

  // Early exit: if cond is trivially true, nothing to add
  if (cond.is_true()) {
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    store->metrics.totalAssumeTime += duration.count();
    return *this;
  }

  // Early exit: if cond is trivially false, path is infeasible
  if (cond.is_false()) {
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    store->metrics.totalAssumeTime += duration.count();
    return FeasibilityElement::bottom(store);
  }

  FeasibilityElement out = *this;
  if (out.isTop() || out.isIdeNeutral()) {
    out = initial(store);
  }

  out.pcId = store->pcAssume(out.pcId, cond);

  if (!store->isSatisfiable(out)) {
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    store->metrics.totalAssumeTime += duration.count();
    return FeasibilityElement::bottom(store);
  }

  out.kind = Kind::Normal;

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
  store->metrics.totalAssumeTime += duration.count();

  return out;
}

FeasibilityElement FeasibilityElement::clearPathConstraints() const {
  if (store == nullptr) {
    return *this;
  }
  if (isBottom() || isIdeAbsorbing()) {
    return *this;
  }

  FeasibilityElement out = *this;
  if (out.isTop() || out.isIdeNeutral()) {
    out = initial(store);
  }

  out.pcId = store->pcClear();
  out.kind = Kind::Normal;
  return out;
}

FeasibilityElement FeasibilityElement::join(const FeasibilityElement &other) const {
  auto start = std::chrono::high_resolution_clock::now();

  if (store == nullptr) {
    return *this;
  }
  store->metrics.joinCount++;

  auto result = store->join(*this, other);

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
  store->metrics.totalJoinTime += duration.count();

  return result;
}

bool FeasibilityElement::equal_to(const FeasibilityElement &other) const noexcept {
  return store == other.store && kind == other.kind && pcId == other.pcId &&
         ssaId == other.ssaId && memId == other.memId;
}

bool FeasibilityElement::isSatisfiable() const {
  if (store == nullptr) {
    return false;
  }
  return store->isSatisfiable(*this);
}

bool FeasibilityStateStore::isValid(const z3::expr &e) {
  solver.push();
  solver.add(!e);
  const bool ok = solver.check() == z3::unsat;
  solver.pop();
  return ok;
}

bool FeasibilityStateStore::isUnsat(const z3::expr &e) {
  solver.push();
  solver.add(e);
  const bool ok = solver.check() == z3::unsat;
  solver.pop();
  return ok;
}

bool FeasibilityStateStore::isEquivalent(const z3::expr &A, const z3::expr &B) {
  solver.push();
  solver.add(A ^ B);
  const bool ok = solver.check() == z3::unsat;
  solver.pop();
  return ok;
}

FeasibilityStateStore::FeasibilityStateStore() : solver(context) {
  baseConstraints.push_back(context.bool_val(true)); // pcId 0
  pcSatCache.push_back(-1);                          // cache for pcId 0
}

FeasibilityStateStore::~FeasibilityStateStore() {}

z3::context &FeasibilityStateStore::ctx() noexcept {
  return context;
}


FeasibilityStateStore::id_t FeasibilityStateStore::pcAssume(id_t pc, const z3::expr &cond) {
  // Build new constraint efficiently
  z3::expr newPc = (pc == 0) ? cond : (baseConstraints[pc] && cond);

  // Intern expression and use ExprId as cache key
  const ExprId eid = internExpr(newPc);

  if (auto It = pathConditions.find(eid); It != pathConditions.end()) {
    return It->second;
  }

  // NEW PC created
  metrics.totalPcCreated++;

  const id_t newid = static_cast<id_t>(baseConstraints.size());
  baseConstraints.push_back(newPc);
  pcSatCache.push_back(-1);
  pathConditions.emplace(eid, newid);

  // Track pool size
  {
    const uint64_t poolSize = baseConstraints.size();
    uint64_t currentMaxPool = metrics.maxPcPoolSize.load();
    while (poolSize > currentMaxPool &&
           !metrics.maxPcPoolSize.compare_exchange_weak(currentMaxPool, poolSize)) {}
  }

  // Track AST size ONLY for newly created PCs (much cheaper overall)
  {
    const uint64_t depth = computeAstDepth(newPc);
    const uint64_t nodes = computeAstNodes(newPc);

    uint64_t currentMaxDepth = metrics.maxPcAstDepth.load();
    while (depth > currentMaxDepth &&
           !metrics.maxPcAstDepth.compare_exchange_weak(currentMaxDepth, depth)) {}

    uint64_t currentMaxNodes = metrics.maxPcAstNodes.load();
    while (nodes > currentMaxNodes &&
           !metrics.maxPcAstNodes.compare_exchange_weak(currentMaxNodes, nodes)) {}
  }

  return newid;
}


FeasibilityStateStore::id_t FeasibilityStateStore::pcClear() {
  // FIXED: Don't clear caches! Just return id 0 (= true)
  // This makes clearing O(1) and preserves all interning/caching
  metrics.pcClearCalls++;
  return 0;
}

z3::expr FeasibilityStateStore::getPathConstraint(id_t pcId) const {
  return baseConstraints[pcId];
}

bool FeasibilityStateStore::isNotOf(const z3::expr &A, const z3::expr &B) {
  if (!A.is_app()) {
    return false;
  }
  if (A.decl().decl_kind() != Z3_OP_NOT) {
    return false;
  }
  return z3::eq(A.arg(0), B);
}

bool FeasibilityStateStore::isAnd2(const z3::expr &E) {
  return E.is_app() && E.decl().decl_kind() == Z3_OP_AND && E.num_args() == 2;
}

bool FeasibilityStateStore::isOr2(const z3::expr &E) {
  return E.is_app() && E.decl().decl_kind() == Z3_OP_OR && E.num_args() == 2;
}

z3::expr FeasibilityStateStore::factor_or_and_not(const z3::expr &E) {
  if (!isOr2(E)) {
    return E;
  }

  const z3::expr A = E.arg(0);
  const z3::expr B = E.arg(1);

  if (!isAnd2(A) || !isAnd2(B)) {
    return E;
  }

  const z3::expr a0 = A.arg(0);
  const z3::expr a1 = A.arg(1);
  const z3::expr b0 = B.arg(0);
  const z3::expr b1 = B.arg(1);

  if (z3::eq(a0, b0) && isNotOf(b1, a1)) {
    return a0;
  }
  if (z3::eq(a0, b0) && isNotOf(a1, b1)) {
    return a0;
  }

  if (z3::eq(a0, b1) && isNotOf(b0, a1)) {
    return a0;
  }
  if (z3::eq(a1, b0) && isNotOf(b1, a0)) {
    return a1;
  }
  if (z3::eq(a1, b1) && isNotOf(b0, a0)) {
    return a1;
  }

  return E;
}

FeasibilityElement
FeasibilityStateStore::join(const FeasibilityElement &AIn,
                            const FeasibilityElement &BIn) {
  FeasibilityElement A = normalizeIdeKinds(AIn, this);
  FeasibilityElement B = normalizeIdeKinds(BIn, this);

  // Top / Bottom handling (keep yours)
  if (A.isTop() || B.isTop()) return FeasibilityElement::top(this);
  if (A.isBottom()) return B;
  if (B.isBottom()) return A;
  if (A == B) return A;

  FeasibilityElement R = FeasibilityElement::initial(this);
  R.kind  = FeasibilityElement::Kind::Normal;

  // Join SSA/memory information as before
  R.memId = Mem.joinKeepEqual(A.memId, B.memId);
  R.ssaId = Ssa.joinKeepEqual(A.ssaId, B.ssaId);

  // Track store sizes
  uint64_t ssaSize = Ssa.poolSize();
  uint64_t memSize = Mem.poolSize();

  uint64_t currentMaxSsa = metrics.maxSsaEnvSize.load();
  while (ssaSize > currentMaxSsa &&
         !metrics.maxSsaEnvSize.compare_exchange_weak(currentMaxSsa, ssaSize)) {}

  uint64_t currentMaxMem = metrics.maxMemEnvSize.load();
  while (memSize > currentMaxMem &&
         !metrics.maxMemEnvSize.compare_exchange_weak(currentMaxMem, memSize)) {}

  // ---- Path condition join: SSA-friendly widening ----
  // If PCs are identical, keep them (free precision)
  if (A.pcId == B.pcId) {
    R.pcId = A.pcId;
    return R;
  }

  // If either is already "true", merged PC is "true"
  if (A.pcId == 0 || B.pcId == 0) {
    R.pcId = 0;
    return R;
  }

  // Otherwise: real merge of different feasible paths -> forget disjunction
  R.pcId = 0;
  return R;
}

bool FeasibilityStateStore::isSatisfiable(const FeasibilityElement &E) {
  auto start = std::chrono::high_resolution_clock::now();

  if (E.isBottom()) {
    return false;
  }
  if (E.isTop()) {
    return true;
  }

  // ONLY count checks that actually consult the pcSatCache
  metrics.satCheckCount++;

  assert(E.pcId < baseConstraints.size());
  assert(E.pcId < pcSatCache.size());

  auto &c = pcSatCache[E.pcId];
  if (c != -1) {
    metrics.satCacheHits++;
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    metrics.totalSatCheckTime += duration.count();
    return c == 1;
  }

  metrics.satCacheMisses++;

  solver.push();
  solver.add(baseConstraints[E.pcId]);
  const bool sat = solver.check() == z3::sat;
  solver.pop();

  c = sat ? 1 : 0;

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
  metrics.totalSatCheckTime += duration.count();

  return sat;
}

FeasibilityElement FeasibilityStateStore::normalizeIdeKinds(const FeasibilityElement &E,
                                                            FeasibilityStateStore *S) {
  if (E.isIdeNeutral()) {
    return FeasibilityElement::initial(S);
  }
  if (E.isIdeAbsorbing()) {
    return FeasibilityElement::bottom(S);
  }
  return E;
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const FeasibilityElement &E) {
  if (E.isIdeAbsorbing()) {
    return OS << "⊥";
  }
  if (E.isIdeNeutral()) {
    return OS << "init";
  }
  if (E.isBottom()) {
    return OS << "⊥";
  }
  if (E.isTop()) {
    return OS << "⊤";
  }

  auto expression = E.getStore()->getPathConstraint(E.pcId);
  return OS << "[" << expression.to_string() << "]";
}

std::string toString(const std::optional<FeasibilityElement> &E) {
  std::string s;
  llvm::raw_string_ostream rso(s);
  if (E.has_value()) {
    rso << *E;
  } else {
    rso << "nullopt";
  }
  rso.flush();
  return s;
}

std::ostream &operator<<(std::ostream &os, const std::optional<FeasibilityElement> &E) {
  return os << toString(E);
}

FeasibilityStateStore::ExprId FeasibilityStateStore::internExpr(const z3::expr &E) {
  // Track every call
  metrics.internCalls++;

  // Debug: print on first call to verify we're in the right code
  static std::atomic<bool> firstCall{true};
  if (firstCall.exchange(false)) {
    llvm::errs() << "[DEBUG] internExpr called for first time, store @ " << this << "\n";
    llvm::errs() << "[DEBUG] ExprTable @ " << &ExprTable << "\n";
    llvm::errs() << "[DEBUG] ExprIntern @ " << &ExprIntern << "\n";
  }

  // Stable hash from Z3 AST (fast)
  const unsigned h = Z3_get_ast_hash(ctx(), E);

  auto &bucket = ExprIntern[h];

  for (ExprId id : bucket) {
    if (z3::eq(ExprTable[id], E)) {
      // Cache hit
      metrics.internHits++;
      return id;
    }
  }

  // NEW expression - actually insert it
  const ExprId newId = static_cast<ExprId>(ExprTable.size());
  ExprTable.push_back(E);
  bucket.push_back(newId);

  // Track insertion
  metrics.internInserted++;

  return newId;
}

const z3::expr &FeasibilityStateStore::exprOf(ExprId Id) const {
  return ExprTable[Id];
}

FeasibilityStateStore::ExprId
FeasibilityStateStore::getOrCreateSym(const llvm::Value *V,
                                      unsigned Bw,
                                      llvm::StringRef Prefix) {
  auto It = SymCache.find(V);
  if (It != SymCache.end()) {
    return It->second;
  }

  // Make a stable name (pointer-stable is fine for one run; you can swap in a stableName() later)
  std::string Name;
  {
    llvm::raw_string_ostream OS(Name);
    OS << Prefix << "_" << (const void *)V;
  }

  z3::expr Sym = ctx().bv_const(Name.c_str(), Bw);
  ExprId Id = internExpr(Sym);
  SymCache[V] = Id;
  return Id;
}

} // namespace Feasibility
