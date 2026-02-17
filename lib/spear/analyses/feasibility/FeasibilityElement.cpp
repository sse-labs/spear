/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityElement.h"

#include <unordered_map>
#include <utility>
#include <vector>

#include <z3++.h>

#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/raw_os_ostream.h>

namespace Feasibility {

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

  out.pcId = store->pcAssume(out.pcId, cond);

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
  if (store == nullptr) {
    return *this;
  }
  return store->join(*this, other);
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
  const z3::expr &existingPathConstraint = baseConstraints[pc];
  z3::expr newPathConstrains = (existingPathConstraint && cond);

  auto It = pathConditions.find(newPathConstrains);
  if (It != pathConditions.end()) {
    return It->second;
  }

  id_t newid = static_cast<id_t>(baseConstraints.size());
  baseConstraints.push_back(newPathConstrains);
  pcSatCache.push_back(-1);
  pathConditions.emplace(newPathConstrains, newid);

  return newid;
}

FeasibilityStateStore::id_t FeasibilityStateStore::pcClear() {
  pathConditions.clear();

  baseConstraints.clear();
  baseConstraints.push_back(context.bool_val(true));

  pcSatCache.clear();
  pcSatCache.push_back(-1);

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
  if (E.isBottom()) {
    return false;
  }
  if (E.isTop()) {
    return true;
  }

  assert(E.pcId < baseConstraints.size());
  assert(E.pcId < pcSatCache.size());

  auto &c = pcSatCache[E.pcId];
  if (c != -1) {
    return c == 1;
  }

  solver.push();
  solver.add(baseConstraints[E.pcId]);
  const bool sat = solver.check() == z3::sat;
  solver.pop();

  c = sat ? 1 : 0;
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
  // Stable hash from Z3 AST (fast)
  const unsigned h = Z3_get_ast_hash(ctx(), E);

  auto &bucket = ExprIntern[h];
  for (ExprId id : bucket) {
    if (z3::eq(ExprTable[id], E)) {
      return id;
    }
  }

  const ExprId newId = static_cast<ExprId>(ExprTable.size());
  ExprTable.push_back(E);
  bucket.push_back(newId);
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
