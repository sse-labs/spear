/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_FEASIBILITYEDGEFUNCTION_H
#define SPEAR_FEASIBILITYEDGEFUNCTION_H

#include <phasar/DataFlow/IfdsIde/EdgeFunction.h>
#include <llvm/IR/Value.h>           // add if not already pulled transitively
#include <z3++.h>                    // add if not already pulled transitively

#include <utility>
#include <llvm/IR/Instructions.h>
#include <phasar/DataFlow/IfdsIde/EdgeFunctionUtils.h>

#include "FeasibilityElement.h"

namespace Feasibility {

using l_t = Feasibility::FeasibilityElement;
using EF = psr::EdgeFunction<l_t>;

/**
 * Identity edge function for the FeasibilityAnalysis. Maps any input to itself.
 */
struct FeasibilityIdentityEF {
    using l_t = Feasibility::l_t;

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityIdentityEF>,
                                   const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityIdentityEF> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityIdentityEF &) const = default;

    bool isConstant() const noexcept;
};

/**
 * Top edge function for the FeasibilityAnalysis. Maps any input to top.
 */
struct FeasibilityAllTopEF {
    using l_t = Feasibility::l_t;

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAllTopEF>,
                                   const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAllTopEF> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityAllTopEF &) const = default;

    bool isConstant() const noexcept;
};

/**
 * Bottom edge function for the FeasibilityAnalysis. Maps any input to bottom.
 */
struct FeasibilityAllBottomEF {
    using l_t = Feasibility::l_t;

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAllBottomEF>,
                                   const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAllBottomEF> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityAllBottomEF &) const = default;

    bool isConstant() const noexcept;
};

/**
 * SSA edge function to manipulate SSA store of the FeasibilityElement.
 * Maps any input to the result of updating the SSA store with a new binding.
 */
struct FeasibilitySetSSAEF {
    using l_t = Feasibility::l_t;

    const llvm::Value *Key = nullptr;
    const llvm::Value *Loc = nullptr;

    // Direct set: SSA[Key] := ValueExpr
    FeasibilitySetSSAEF(const llvm::Value *Key, const llvm::Value *Loc)
        : Key(Key), Loc(Loc) {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;
    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilitySetSSAEF>, const EF &secondFunction);
    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilitySetSSAEF>, const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilitySetSSAEF &O) const = default;

    bool isConstant() const noexcept;
};

/**
 * Memory edge function to manipulate memory store of the FeasibilityElement.
 * Maps any input to the result of updating the memory store with a new binding.
 */
struct FeasibilitySetMemEF {
    using l_t = Feasibility::l_t;

    const llvm::Value *Loc = nullptr;
    FeasibilityStateStore::ExprId ValueId = 0;

    FeasibilitySetMemEF(const llvm::Value *Loc, FeasibilityStateStore::ExprId ValueExpr)
        : Loc(Loc), ValueId(ValueExpr) {}


    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilitySetMemEF>,
                                   const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilitySetMemEF> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilitySetMemEF &) const = default;

    bool isConstant() const noexcept;
};

struct FeasibilityAssumeIcmpEF {
    using l_t = Feasibility::l_t;

    const llvm::ICmpInst *Cmp = nullptr;
    bool TakeTrueEdge = true; // if false => use !cond

    FeasibilityAssumeIcmpEF(const llvm::ICmpInst *C, bool T)
        : Cmp(C), TakeTrueEdge(T) {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAssumeIcmpEF>,
                                    const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAssumeIcmpEF> thisFunc,
                                 const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityAssumeIcmpEF &O) const {
        return Cmp == O.Cmp && TakeTrueEdge == O.TakeTrueEdge;
    }

    bool isConstant() const noexcept { return false; }
};

struct FeasibilityPhiEF {
    using l_t = Feasibility::l_t;

    const llvm::PHINode *phinode = nullptr;
    const llvm::Value *incomingVal = nullptr;

    FeasibilityPhiEF(const llvm::PHINode *phinode, const llvm::Value *incomingVal) : phinode(phinode), incomingVal(incomingVal) {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityPhiEF>,
                                    const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityPhiEF> thisFunc,
                                 const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityPhiEF &O) const {
        return phinode == O.phinode && incomingVal == O.incomingVal;
    }

    bool isConstant() const noexcept { return false; }
};

struct PackedOp {
  enum Kind { SetMem, SetSSA, AssumeIcmp, Phi } K;

  // SetMem:
  const llvm::Value *Loc = nullptr;
  FeasibilityStateStore::ExprId Val = 0;

  // SetSSA:
  const llvm::Value *Key = nullptr;   // SSA key (load inst / reg)
  const llvm::Value *LocSSA = nullptr; // optional, if you need it

  // AssumeIcmp:
  const llvm::ICmpInst *Cmp = nullptr;
  bool TakeTrue = true;

  // Phi:
  const llvm::PHINode *PhiN = nullptr;
  const llvm::Value *Incoming = nullptr;

  static PackedOp mkSetMem(const llvm::Value *L, FeasibilityStateStore::ExprId V) {
    PackedOp O; O.K = SetMem; O.Loc = L; O.Val = V; return O;
  }

  static PackedOp mkAssume(const llvm::ICmpInst *C, bool T) {
    PackedOp O; O.K = AssumeIcmp; O.Cmp = C; O.TakeTrue = T; return O;
  }

  static PackedOp mkPhi(const llvm::PHINode *P, const llvm::Value *In) {
    PackedOp O; O.K = Phi; O.PhiN = P; O.Incoming = In; return O;
  }
};

constexpr size_t PACK_MAX_OPS = 128;

struct FeasibilityPackedEF {
  using l_t = Feasibility::l_t;

  std::array<PackedOp, PACK_MAX_OPS> Ops{};
  uint8_t N = 0;

  FeasibilityPackedEF() = default;

  bool pushBack(const PackedOp &O) {
    if (N >= PACK_MAX_OPS) return false;
    Ops[N++] = O;
    return true;
  }

  [[nodiscard]] l_t computeTarget(const l_t &source) const {
    auto *S = source.getStore();
    if (!S || source.isBottom()) return source;

    l_t out = source;
    if (out.isTop() || out.isIdeNeutral()) {
      out = l_t::initial(S);
    }

    for (uint8_t i = 0; i < N; ++i) {
      const auto &Op = Ops[i];

      switch (Op.K) {
        case PackedOp::SetMem: {
          out.memId = S->Mem.set(out.memId, Op.Loc, Op.Val);
          break;
        }

        case PackedOp::AssumeIcmp: {
          // reuse your existing AssumeIcmp EF logic without allocating a new EF:
          FeasibilityAssumeIcmpEF A(Op.Cmp, Op.TakeTrue);
          out = A.computeTarget(out);
          if (out.isBottom()) return out;
          break;
        }

        case PackedOp::Phi: {
          // reuse your Phi EF logic:
          FeasibilityPhiEF P(Op.PhiN, Op.Incoming);
          out = P.computeTarget(out);
          if (out.isBottom()) return out;
          break;
        }

        case PackedOp::SetSSA:
          // only if you actually need it; otherwise drop this kind
          break;
      }
    }

    return out;
  }

  // ---- KEY PART: compose must KEEP the pack ----
  [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityPackedEF> self,
                                  const EF &second) {
    // compose(self, Id) = self   (THIS is what you're missing)
    if (second.template isa<FeasibilityIdentityEF>() ||
        second.template isa<psr::EdgeIdentity<l_t>>()) {
      return EF(self);
    }

    if (second.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(second)) {
      return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    if (second.template isa<FeasibilityAllTopEF>() ||
        llvm::isa<psr::AllTop<l_t>>(second)) {
      return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    // pack ∘ pack  => concatenate ops (bounded)
    if (second.template isa<FeasibilityPackedEF>()) {
      const auto &B = second.template cast<FeasibilityPackedEF>();
      FeasibilityPackedEF C = *self; // copy
      for (uint8_t i = 0; i < B->N; ++i) {
        if (!C.pushBack(B->Ops[i])) {
          return EF(std::in_place_type<FeasibilityAllTopEF>);
        }
      }
      return EF(std::in_place_type<FeasibilityPackedEF>, C);
    }

    // pack ∘ assume => append Assume op
    if (second.template isa<FeasibilityAssumeIcmpEF>()) {
      const auto &A = second.template cast<FeasibilityAssumeIcmpEF>();
      FeasibilityPackedEF C = *self;
      if (!C.pushBack(PackedOp::mkAssume(A->Cmp, A->TakeTrueEdge))) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
      }
      return EF(std::in_place_type<FeasibilityPackedEF>, C);
    }

    // pack ∘ phi => append Phi op (rare, but correct)
    if (second.template isa<FeasibilityPhiEF>()) {
      const auto &P = second.template cast<FeasibilityPhiEF>();
      FeasibilityPackedEF C = *self;
      if (!C.pushBack(PackedOp::mkPhi(P->phinode, P->incomingVal))) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
      }
      return EF(std::in_place_type<FeasibilityPackedEF>, C);
    }

    // otherwise: unknown function => widen
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }

  [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityPackedEF> self,
                               const psr::EdgeFunction<l_t> &other) {
    if (other.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(other) ||
        other.template isa<FeasibilityIdentityEF>() ||
        other.template isa<psr::EdgeIdentity<l_t>>()) {
      return EF(self);
    }
    if (other.template isa<FeasibilityAllTopEF>() ||
        llvm::isa<psr::AllTop<l_t>>(other)) {
      return EF(std::in_place_type<FeasibilityAllTopEF>);
    }
    if (other.template isa<FeasibilityPackedEF>()) {
      const auto &O = other.template cast<FeasibilityPackedEF>();
      if (*self == *O) return EF(self);
    }
    return EF(std::in_place_type<FeasibilityAllTopEF>);
  }

  bool operator==(const FeasibilityPackedEF &O) const {
    if (N != O.N) return false;
    for (uint8_t i = 0; i < N; ++i) {
      const auto &A = Ops[i];
      const auto &B = O.Ops[i];
      if (A.K != B.K) return false;
      // compare fields relevant to kind
      switch (A.K) {
        case PackedOp::SetMem:    if (A.Loc != B.Loc || A.Val != B.Val) return false; break;
        case PackedOp::AssumeIcmp:if (A.Cmp != B.Cmp || A.TakeTrue != B.TakeTrue) return false; break;
        case PackedOp::Phi:       if (A.PhiN != B.PhiN || A.Incoming != B.Incoming) return false; break;
        case PackedOp::SetSSA:    if (A.Key != B.Key || A.LocSSA != B.LocSSA) return false; break;
      }
    }
    return true;
  }

  bool isConstant() const noexcept { return false; }
};


EF edgeIdentity();

EF edgeTop();

EF edgeBottom();

}  // namespace Feasibility

#endif  // SPEAR_FEASIBILITYEDGEFUNCTION_H

