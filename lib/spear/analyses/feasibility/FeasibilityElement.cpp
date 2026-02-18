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
  return FeasibilityElement{S, true, Kind::IdeNeutral, S->PcInit, 0, 0};
}

FeasibilityElement FeasibilityElement::top(FeasibilityStateStore *S) noexcept {
  return FeasibilityElement{S, true, Kind::Top, S->PcTrueId, 0, 0};
}

FeasibilityElement FeasibilityElement::bottom(FeasibilityStateStore *S) noexcept {
  return FeasibilityElement{S, true, Kind::Bottom, S->PcFalseId, 0, 0};
}

FeasibilityElement FeasibilityElement::initial(FeasibilityStateStore *S) noexcept {
  return FeasibilityElement{S, true, Kind::Normal, S->PcInit, 0, 0};
}

bool FeasibilityElement::isIdeNeutral() const noexcept {
  return kind == Kind::IdeNeutral;
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

  if (isBottom()) {
    return *this;
  }

  // Early exit: if cond is trivially true, nothing to add
  if (cond.is_true()) {
    return *this;
  }

  // Early exit: if cond is trivially false, path is infeasible
  if (cond.is_false()) {
    return FeasibilityElement::bottom(store);
  }

  FeasibilityElement out = *this;

  auto oldId = out.pcId;
  out.pcId = store->pcAssume(out.pcId, cond);

  auto pcExpr = store->getPathConstraint(out.pcId);
  llvm::errs() << "[ASSUME] out.pcId=" << out.pcId
               << " expr=" << pcExpr.to_string() << "\n";
  llvm::errs() << "[ASSUME] decl_kind=" << (pcExpr.is_app() ? pcExpr.decl().decl_kind() : -1) << "\n";


  llvm::errs() << "[ASSUME] oldPcId=" << oldId
               << " newPcId=" << out.pcId
               << " cond=" << cond.to_string() << "\n";

  llvm::errs() << "[ASSUME] oldPc=" << store->getPathConstraint(oldId).to_string() << "\n";
  llvm::errs() << "[ASSUME] newPc=" << store->getPathConstraint(out.pcId).to_string() << "\n";

  if (!store->isSatisfiable(out)) {
    return FeasibilityElement::bottom(store);
  }

  out.kind = Kind::Normal;

  return out;
}

FeasibilityElement FeasibilityElement::clearPathConstraints() const {
  if (store == nullptr) {
    return *this;
  }
  if (isBottom()) {
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
  if (store == nullptr) {
    return *this;
  }

  auto result = store->join(*this, other);

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

FeasibilityStateStore::FeasibilityStateStore() : solver(context) {
  // Reserve:
  // 0 = initial (treated like true, but distinct id)
  // 1 = true
  // 2 = false
  baseConstraints.reserve(3);
  pcSatCache.reserve(3);

  baseConstraints.push_back(context.bool_val(true));   // id 0: initial
  baseConstraints.push_back(context.bool_val(true));   // id 1: true
  baseConstraints.push_back(context.bool_val(false));  // id 2: false

  pcSatCache.push_back(1); // initial is SAT
  pcSatCache.push_back(1); // true is SAT
  pcSatCache.push_back(0); // false is UNSAT

  // Fail fast if ids drift
  assert(PcInit == 0 && "PcInit must be 0");
  assert(PcTrueId == 1 && "PcTrueId must be 1");
  assert(PcFalseId == 2 && "PcFalseId must be 2");

  // Seed interning -> path id mapping so "true" resolves to PcTrueId (1),
  // and "false" resolves to PcFalseId (2). Do NOT map true -> 0.
  ExprId tId = internExpr(baseConstraints[PcTrueId]);
  ExprId fId = internExpr(baseConstraints[PcFalseId]);
  pathConditions.emplace(tId, PcTrueId);
  pathConditions.emplace(fId, PcFalseId);
}

FeasibilityStateStore::~FeasibilityStateStore() {}

z3::context &FeasibilityStateStore::ctx() noexcept {
  return context;
}


FeasibilityStateStore::id_t FeasibilityStateStore::pcAssume(id_t pc, const z3::expr &cond) {
  // Build new constraint efficiently
  z3::expr newPc = baseConstraints[pc] && cond;
  newPc = newPc.simplify();

  if (newPc.is_true())  return PcTrueId;
  if (newPc.is_false()) return PcFalseId;

  // Intern expression and use ExprId as cache key
  const ExprId eid = internExpr(newPc);

  if (auto It = pathConditions.find(eid); It != pathConditions.end()) {
    return It->second;
  }

  const id_t newid = static_cast<id_t>(baseConstraints.size());
  baseConstraints.push_back(newPc);
  pcSatCache.push_back(-1);
  pathConditions.emplace(eid, newid);

  return newid;
}


FeasibilityStateStore::id_t FeasibilityStateStore::pcClear() {
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
  if (AIn.isIdeNeutral()) return BIn;
  if (BIn.isIdeNeutral()) return AIn;

  FeasibilityElement A = AIn;
  FeasibilityElement B = BIn;

  // Top / Bottom handling (keep yours)
  if (A.isTop() || B.isTop()) return FeasibilityElement::top(this);
  if (A.isBottom()) return B;
  if (B.isBottom()) return A;
  if (A == B) return A;

  FeasibilityElement R = FeasibilityElement::initial(this);
  R.kind  = FeasibilityElement::Kind::Normal;

  llvm::errs() << "[JOIN] " << A.pcId << " U " << B.pcId << " -> " << R.pcId << "\n";

  // Join SSA/memory information as before
  R.memId = Mem.joinKeepEqual(A.memId, B.memId);
  R.ssaId = Ssa.joinKeepEqual(A.ssaId, B.ssaId);

  // Track store sizes
  uint64_t ssaSize = Ssa.poolSize();
  uint64_t memSize = Mem.poolSize();

  // ---- Path condition join: SSA-friendly widening ----
  // If PCs are identical, keep them (free precision)
  if (A.pcId == B.pcId) {
    R.pcId = A.pcId;
    return R;
  }

  // If either is already "true", merged PC is "true"
  if (A.pcId == PcTrueId || B.pcId == PcTrueId) {
    R.pcId = PcTrueId;
    return R;
  }

  // Otherwise: real merge of different feasible paths -> forget disjunction
  R.pcId = pcOr(A.pcId, B.pcId);
  return R;
}

bool FeasibilityStateStore::isSatisfiable(const FeasibilityElement &E) {
  if (E.isBottom()) return false;
  if (E.isTop())    return true;

  if (E.pcId == PcInit || E.pcId == PcTrueId) return true;
  if (E.pcId == PcFalseId) return false;

  if (E.pcId >= pcSatCache.size() || E.pcId >= baseConstraints.size()) {
    llvm::errs() << "[BUG] pcId out of range: " << E.pcId
                 << " pcSatCache=" << pcSatCache.size()
                 << " base=" << baseConstraints.size() << "\n";
    return false;
  }

  auto &c = pcSatCache[E.pcId];
  if (c != -1) return c == 1;

  z3::expr eq = baseConstraints[E.pcId];

  solver.push();
  solver.add(eq);
  bool sat = solver.check() == z3::sat;
  solver.pop();

  c = sat ? 1 : 0;
  return sat;
}

FeasibilityElement FeasibilityStateStore::normalizeIdeKinds(const FeasibilityElement &E,
                                                            FeasibilityStateStore *S) {
  if (E.isIdeNeutral()) {
    return FeasibilityElement::initial(S);
  }
  return E;
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const FeasibilityElement &E) {
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
      return id;
    }
  }

  const ExprId newId = static_cast<ExprId>(ExprTable.size());
  ExprTable.push_back(E);
  bucket.push_back(newId);

  return newId;
}

FeasibilityStateStore::id_t FeasibilityStateStore::pcOr(id_t a, id_t b) {
  if (a == b) return a;
  if (a == PcTrueId || a == PcInit) return PcTrueId; // <-- was 0
  if (b == PcTrueId || b == PcInit) return PcTrueId; // <-- was 0
  if (a == PcFalseId) return b;
  if (b == PcFalseId) return a;

  z3::expr e = baseConstraints[a] || baseConstraints[b];
  e = e.simplify();
  e = factor_or_and_not(e);

  ExprId eid = internExpr(e);
  if (auto it = pathConditions.find(eid); it != pathConditions.end())
    return it->second;

  id_t id = (id_t)baseConstraints.size();
  baseConstraints.push_back(e);
  pcSatCache.push_back(-1);
  pathConditions.emplace(eid, id);
  return id;
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
