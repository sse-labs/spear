/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityElement.h"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

#include <z3++.h>

#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/raw_ostream.h>

namespace Feasibility {

// ============================================================================
// FeasibilityElement (handle) - trivial methods
// ============================================================================

FeasibilityElement FeasibilityElement::ideNeutral(FeasibilityStateStore *S) noexcept {
  return FeasibilityElement{S, Kind::IdeNeutral, 0, 0, 0};
}

FeasibilityElement FeasibilityElement::ideAbsorbing(FeasibilityStateStore *S) noexcept {
  return FeasibilityElement{S, Kind::IdeAbsorbing, 0, 0, 0};
}

FeasibilityElement FeasibilityElement::top(FeasibilityStateStore *S) noexcept {
  return FeasibilityElement{S, Kind::Top, 0, 0, 0};
}

FeasibilityElement FeasibilityElement::bottom(FeasibilityStateStore *S) noexcept {
  return FeasibilityElement{S, Kind::Bottom, 0, 0, 0};
}

FeasibilityElement FeasibilityElement::initial(FeasibilityStateStore *S) noexcept {
  // "Normal empty state" (not IDE neutral).
  return FeasibilityElement{S, Kind::Normal, 0, 0, 0};
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
  // IDE-neutral should behave like "no information yet": materialize to empty normal.
  if (out.isTop() || out.isIdeNeutral()) {
    out = initial(store);
  }

  out.pcId = store->pcAssume(out.pcId, cond);
  out.kind = Kind::Normal;
  return out;
}

FeasibilityElement FeasibilityElement::setSSA(const llvm::Value *key,
                                              const z3::expr &expr) const {
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

  out.ssaId = store->ssaSet(out.ssaId, key, expr);
  out.kind = Kind::Normal;
  return out;
}

FeasibilityElement FeasibilityElement::setMem(const llvm::Value *loc,
                                              const z3::expr &expr) const {
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

  out.memId = store->memSet(out.memId, loc, expr);
  out.kind = Kind::Normal;
  return out;
}

FeasibilityElement FeasibilityElement::forgetSSA(const llvm::Value *key) const {
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

  out.ssaId = store->ssaForget(out.ssaId, key);
  out.kind = Kind::Normal;
  return out;
}

FeasibilityElement FeasibilityElement::forgetMem(const llvm::Value *loc) const {
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

  out.memId = store->memForget(out.memId, loc);
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

bool FeasibilityElement::leq(const FeasibilityElement &other) const {
  if (store == nullptr) {
    return false;
  }
  return store->leq(*this, other);
}

FeasibilityElement FeasibilityElement::join(const FeasibilityElement &other) const {
  if (store == nullptr) {
    return *this;
  }
  return store->join(*this, other);
}

FeasibilityElement FeasibilityElement::meet(const FeasibilityElement &other) const {
  if (store == nullptr) {
    return *this;
  }
  return store->meet(*this, other);
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

bool FeasibilityElement::isSatisfiableWith(const z3::expr &additionalConstraint) const {
  if (store == nullptr) {
    return false;
  }
  return store->isSatisfiableWith(*this, additionalConstraint);
}

// ============================================================================
// FeasibilityStateStore::Impl (heavy state)
// ============================================================================

struct FeasibilityStateStore::Impl final {
  z3::context Ctx;

  // Persistent PC as a linked list of constraints:
  // pcId == 0 means empty (true).
  struct PcNode {
    id_t prev = 0;
    z3::expr constraint;

    PcNode(id_t p, const z3::expr &c) : prev(p), constraint(c) {}
  };

  std::vector<PcNode> PcNodes; // index+1 == id (so id 0 is reserved)

  using Map = std::unordered_map<const llvm::Value *, z3::expr>;

  // Very simple persistent maps by full-copy per update:
  // id 0 is empty map.
  std::vector<Map> SsaStates;
  std::vector<Map> MemStates;

  Impl() {
    SsaStates.emplace_back(); // id 0
    MemStates.emplace_back(); // id 0
  }

  [[nodiscard]] const Map &ssa(id_t id) const {
    if (id == 0) {
      return SsaStates[0];
    }
    return SsaStates[id];
  }

  [[nodiscard]] const Map &mem(id_t id) const {
    if (id == 0) {
      return MemStates[0];
    }
    return MemStates[id];
  }

  // Collect constraints of a pcId into a vector (from oldest to newest).
  void collectPc(id_t pc, std::vector<z3::expr> &out) const {
    out.clear();
    // pcId refers to PcNodes[pc-1]
    while (pc != 0) {
      const PcNode &N = PcNodes[pc - 1];
      out.push_back(N.constraint);
      pc = N.prev;
    }
    std::reverse(out.begin(), out.end());
  }

  bool pcEq(id_t a, id_t b) const {
    return a == b;
  }

  static bool storeLeqMust(const Map &A, const Map &B) {
    // A ⊑ B (must): A has at least all bindings of B with identical expr
    for (const auto &KV : B) {
      auto it = A.find(KV.first);
      if (it == A.end()) {
        return false;
      }
      if (!z3::eq(it->second, KV.second)) {
        return false;
      }
    }
    return true;
  }

  static void mustJoinInto(Map &out, const Map &A, const Map &B) {
    out.clear();
    for (const auto &KV : A) {
      auto it = B.find(KV.first);
      if (it != B.end()) {
        if (z3::eq(KV.second, it->second)) {
          out.emplace(KV.first, KV.second);
        }
      }
    }
  }
};

FeasibilityStateStore::FeasibilityStateStore() {
  PImpl = new Impl();
}

FeasibilityStateStore::~FeasibilityStateStore() {
  delete PImpl;
  PImpl = nullptr;
}

z3::context &FeasibilityStateStore::ctx() noexcept {
  return PImpl->Ctx;
}

// ---- PC ops ----

FeasibilityStateStore::id_t FeasibilityStateStore::pcAssume(id_t pc,
                                                            const z3::expr &cond) {
  // Store node; id is index+1
  PImpl->PcNodes.emplace_back(pc, cond);
  return static_cast<id_t>(PImpl->PcNodes.size());
}

FeasibilityStateStore::id_t FeasibilityStateStore::pcClear() {
  return 0;
}

// ---- SSA ops ----

FeasibilityStateStore::id_t FeasibilityStateStore::ssaSet(id_t ssa,
                                                          const llvm::Value *key,
                                                          const z3::expr &expr) {
  Impl::Map next = (ssa == 0) ? PImpl->SsaStates[0] : PImpl->SsaStates[ssa];
  next.insert_or_assign(key, expr);
  PImpl->SsaStates.push_back(std::move(next));
  return static_cast<id_t>(PImpl->SsaStates.size() - 1);
}

FeasibilityStateStore::id_t FeasibilityStateStore::ssaForget(id_t ssa,
                                                             const llvm::Value *key) {
  Impl::Map next = (ssa == 0) ? PImpl->SsaStates[0] : PImpl->SsaStates[ssa];
  next.erase(key);
  PImpl->SsaStates.push_back(std::move(next));
  return static_cast<id_t>(PImpl->SsaStates.size() - 1);
}

// ---- Mem ops ----

FeasibilityStateStore::id_t FeasibilityStateStore::memSet(id_t mem,
                                                          const llvm::Value *loc,
                                                          const z3::expr &expr) {
  Impl::Map next = (mem == 0) ? PImpl->MemStates[0] : PImpl->MemStates[mem];
  next.insert_or_assign(loc, expr);
  PImpl->MemStates.push_back(std::move(next));
  return static_cast<id_t>(PImpl->MemStates.size() - 1);
}

FeasibilityStateStore::id_t FeasibilityStateStore::memForget(id_t mem,
                                                             const llvm::Value *loc) {
  Impl::Map next = (mem == 0) ? PImpl->MemStates[0] : PImpl->MemStates[mem];
  next.erase(loc);
  PImpl->MemStates.push_back(std::move(next));
  return static_cast<id_t>(PImpl->MemStates.size() - 1);
}

// ---- lattice ops ----

bool FeasibilityStateStore::leq(const FeasibilityElement &A,
                                const FeasibilityElement &B) {
  // IDE special elements: treat absorbing as least, neutral as greatest (for IDE usage).
  // (They should mostly be handled by FeasibilityAnalysis::join, but keep this safe.)
  if (A.isIdeAbsorbing()) {
    return true;
  }
  if (B.isIdeNeutral()) {
    return true;
  }
  if (B.isIdeAbsorbing()) {
    return A.isIdeAbsorbing();
  }
  if (A.isIdeNeutral()) {
    return B.isIdeNeutral();
  }

  // Bottom ⊑ X ⊑ Top (domain)
  if (A.isBottom()) {
    return true;
  }
  if (B.isTop()) {
    return true;
  }
  if (B.isBottom()) {
    return A.isBottom();
  }
  if (A.isTop()) {
    return B.isTop();
  }

  // both normal: require identical PC handle and must-store order
  if (!PImpl->pcEq(A.pcId, B.pcId)) {
    return false;
  }

  const auto &ASsa = PImpl->ssa(A.ssaId);
  const auto &BSsa = PImpl->ssa(B.ssaId);
  if (!Impl::storeLeqMust(ASsa, BSsa)) {
    return false;
  }

  const auto &AMem = PImpl->mem(A.memId);
  const auto &BMem = PImpl->mem(B.memId);
  if (!Impl::storeLeqMust(AMem, BMem)) {
    return false;
  }

  return true;
}

FeasibilityElement FeasibilityStateStore::join(const FeasibilityElement &A,
                                               const FeasibilityElement &B) {
  if (A.isTop() || B.isTop()) {
    return FeasibilityElement::top(this);
  }
  if (A.isBottom()) {
    return B;
  }
  if (B.isBottom()) {
    return A;
  }

  // MUST join:
  //  - keep PC only if equal, else drop (pcId=0 means true/unknown)
  //  - intersect identical bindings
  FeasibilityElement R = FeasibilityElement::initial(this);
  R.kind = FeasibilityElement::Kind::Normal;

  if (PImpl->pcEq(A.pcId, B.pcId)) {
    R.pcId = A.pcId;
  } else {
    R.pcId = 0;
  }

  Impl::Map joinedSsa;
  Impl::mustJoinInto(joinedSsa, PImpl->ssa(A.ssaId), PImpl->ssa(B.ssaId));
  PImpl->SsaStates.push_back(std::move(joinedSsa));
  R.ssaId = static_cast<id_t>(PImpl->SsaStates.size() - 1);

  Impl::Map joinedMem;
  Impl::mustJoinInto(joinedMem, PImpl->mem(A.memId), PImpl->mem(B.memId));
  PImpl->MemStates.push_back(std::move(joinedMem));
  R.memId = static_cast<id_t>(PImpl->MemStates.size() - 1);

  return R;
}

FeasibilityElement FeasibilityStateStore::meet(const FeasibilityElement &A,
                                               const FeasibilityElement &B) {
  if (A.isBottom() || B.isBottom()) {
    return FeasibilityElement::bottom(this);
  }
  if (A.isTop()) {
    return B;
  }
  if (B.isTop()) {
    return A;
  }

  // Meet:
  //  - PC := concatenate constraints of A then B (creates new pcId chain)
  //  - stores: must-intersection (same as join)
  FeasibilityElement R = FeasibilityElement::initial(this);
  R.kind = FeasibilityElement::Kind::Normal;

  std::vector<z3::expr> ca;
  std::vector<z3::expr> cb;
  PImpl->collectPc(A.pcId, ca);
  PImpl->collectPc(B.pcId, cb);

  id_t pc = 0;
  for (const auto &c : ca) {
    pc = pcAssume(pc, c);
  }
  for (const auto &c : cb) {
    pc = pcAssume(pc, c);
  }
  R.pcId = pc;

  Impl::Map meetSsa;
  Impl::mustJoinInto(meetSsa, PImpl->ssa(A.ssaId), PImpl->ssa(B.ssaId));
  PImpl->SsaStates.push_back(std::move(meetSsa));
  R.ssaId = static_cast<id_t>(PImpl->SsaStates.size() - 1);

  Impl::Map meetMem;
  Impl::mustJoinInto(meetMem, PImpl->mem(A.memId), PImpl->mem(B.memId));
  PImpl->MemStates.push_back(std::move(meetMem));
  R.memId = static_cast<id_t>(PImpl->MemStates.size() - 1);

  return R;
}

// ---- SMT ----

bool FeasibilityStateStore::isSatisfiable(const FeasibilityElement &E) {
  if (E.isBottom()) {
    return false;
  }
  // Top: satisfiable by convention
  if (E.isTop()) {
    return true;
  }

  z3::solver S(PImpl->Ctx);

  std::vector<z3::expr> cs;
  PImpl->collectPc(E.pcId, cs);
  for (const auto &c : cs) {
    S.add(c);
  }

  return S.check() == z3::sat;
}

bool FeasibilityStateStore::isSatisfiableWith(const FeasibilityElement &E,
                                              const z3::expr &additionalConstraint) {
  if (E.isBottom()) {
    return false;
  }
  if (E.isTop()) {
    z3::solver S(PImpl->Ctx);
    S.add(additionalConstraint);
    return S.check() == z3::sat;
  }

  z3::solver S(PImpl->Ctx);

  std::vector<z3::expr> cs;
  PImpl->collectPc(E.pcId, cs);
  for (const auto &c : cs) {
    S.add(c);
  }
  S.add(additionalConstraint);

  return S.check() == z3::sat;
}

// ============================================================================
// Printing helpers
// ============================================================================

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const FeasibilityElement &E) {
  if (E.isIdeAbsorbing()) {
    return OS << "IDE_⊥";
  }
  if (E.isIdeNeutral()) {
    return OS << "IDE_⊤";
  }
  if (E.isBottom()) {
    return OS << "⊥";
  }
  if (E.isTop()) {
    return OS << "⊤";
  }
  // Keep your existing minimalist formatting:
  return OS << "[" << "" << "]";
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

} // namespace Feasibility
