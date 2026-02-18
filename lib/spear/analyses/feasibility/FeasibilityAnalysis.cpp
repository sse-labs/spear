/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityAnalysis.h"

#include <phasar/PhasarLLVM/ControlFlow/LLVMBasedICFG.h>
#include <phasar/PhasarLLVM/DataFlow/IfdsIde/LLVMZeroValue.h>

#include "analyses/feasibility/util.h"
#include "analyses/feasibility/FeasibilityEdgeFunction.h" // add
#include "analyses/loopbound/LoopBoundEdgeFunction.h"

namespace Feasibility {

template <typename D, typename ContainerT>
class DebugFlow final : public psr::FlowFunction<D, ContainerT> {
    std::shared_ptr<psr::FlowFunction<D, ContainerT>> Inner;
    const char *Name;
    Feasibility::FeasibilityAnalysis  *A;
    Feasibility::FeasibilityAnalysis ::n_t Curr;
    Feasibility::FeasibilityAnalysis ::n_t Succ;

public:
    DebugFlow(std::shared_ptr<psr::FlowFunction<D, ContainerT>> Inner,
              const char *Name,
              Feasibility::FeasibilityAnalysis  *A,
              Feasibility::FeasibilityAnalysis ::n_t Curr,
              Feasibility::FeasibilityAnalysis ::n_t Succ)
        : Inner(std::move(Inner)), Name(Name), A(A), Curr(Curr), Succ(Succ) {}

    ContainerT computeTargets(D Src) override {
        ContainerT Out = Inner->computeTargets(Src);

        if (F_DEBUG_ENABLED) {
            llvm::errs() << F_TAG << " FF " << Name << "  ";
            if (Curr) { Feasibility::Util::dumpInst(Curr); } else { llvm::errs() << "<null>"; }
            llvm::errs() << "  ->  ";
            if (Succ) { Feasibility::Util::dumpInst(Succ); } else { llvm::errs() << "<null>"; }
            llvm::errs() << "\n" << F_TAG << "   Src=";
            Feasibility::Util::dumpFact(A,
                                static_cast<Feasibility::FeasibilityAnalysis ::d_t>(Src));
            llvm::errs() << "   Targets={";
            bool first = true;
            for (auto T : Out) {
                if (!first) {
                    llvm::errs() << ", ";
                }
                first = false;
                Feasibility::Util::dumpFact(
                    A, static_cast<Feasibility::FeasibilityAnalysis ::d_t>(T));
            }
            llvm::errs() << "}\n";
        }

        return Out;
    }
};

template <typename D, typename ContainerT>
class IdentityFlow final : public psr::FlowFunction<D, ContainerT> {
public:
    ContainerT computeTargets(D Src) override { return ContainerT{Src}; }
};


template <typename D, typename ContainerT>
class KeepLocalOnCallToRet final : public psr::FlowFunction<D, ContainerT> {
public:
    ContainerT computeTargets(D Src) override { return ContainerT{Src}; }
};


template <typename D, typename ContainerT>
class EmptyFlow final : public psr::FlowFunction<D, ContainerT> {
public:
    ContainerT computeTargets(D /*Src*/) override { return ContainerT{}; }
};


template <typename D, typename ContainerT>
class ZeroOnlyFlow final : public psr::FlowFunction<D, ContainerT> {
    Feasibility::FeasibilityAnalysis *A;
public:
    explicit ZeroOnlyFlow(Feasibility::FeasibilityAnalysis *A) : A(A) {}
    ContainerT computeTargets(D Src) override {
        return A->isZeroValue(static_cast<Feasibility::FeasibilityAnalysis::d_t>(Src))
                   ? ContainerT{Src}
                   : ContainerT{};
    }
};


FeasibilityAnalysis::FeasibilityAnalysis(llvm::FunctionAnalysisManager *FAM, const psr::LLVMProjectIRDB *IRDB, const psr::LLVMBasedICFG *ICFG)
: base_t(IRDB, {"main"}, std::optional<d_t>(
    static_cast<d_t>(psr::LLVMZeroValue::getInstance()))) {
    (void)FAM;
    store = std::make_unique<FeasibilityStateStore>();
    this->ICFG = ICFG;

    llvm::errs() << "[METRICS] FeasibilityAnalysis initialized - metrics tracking enabled\n";
}


psr::InitialSeeds<FeasibilityAnalysis::n_t, FeasibilityAnalysis::d_t,
                  FeasibilityAnalysis::l_t>
FeasibilityAnalysis::initialSeeds() {
    psr::InitialSeeds<n_t, d_t, l_t> Seeds;

    auto Main = this->getProjectIRDB()->getFunctionDefinition("main");
    if (!Main || Main->isDeclaration()) {
        return Seeds;
    }
    if (!ICFG) {
        assert(false && "FeasibilityAnalysis: ICFG is null");
        return Seeds;
    }

    const d_t Zero = this->getZeroValue();
    const l_t IdeNeutral = topElement();

    for (n_t SP : ICFG->getStartPointsOf(&*Main)) {
        Seeds.addSeed(SP, Zero, IdeNeutral);

        if (F_DEBUG_ENABLED) {
            llvm::errs() << F_TAG << " Seed(ICFG) @";
            Feasibility::Util::dumpInst(SP);
            llvm::errs() << "\n";
        }
    }

    return Seeds;
}

FeasibilityAnalysis::d_t FeasibilityAnalysis::zeroValue() const {
    return static_cast<d_t>(psr::LLVMZeroValue::getInstance());
}

bool FeasibilityAnalysis::isZeroValue(d_t Fact) const noexcept{
    return base_t::isZeroValue(Fact);
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::topElement() {
    return l_t::ideNeutral(this->store.get());
}

FeasibilityElement FeasibilityAnalysis::bottomElement() {
    return l_t::bottom(this->store.get());
}

psr::EdgeFunction<FeasibilityAnalysis::l_t> FeasibilityAnalysis::allTopFunction() {
    return psr::AllTop<l_t>{};
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::join(l_t Lhs, l_t Rhs) {
    if (Lhs.isBottom()) {
        return Rhs;
    }
    if (Rhs.isBottom()) {
        return Lhs;
    }

    if (Lhs.isIdeNeutral()) {
        return Rhs;
    }
    if (Rhs.isIdeNeutral()) {
        return Lhs;
    }

    return Lhs.join(Rhs);
}

FeasibilityAnalysis::FlowFunctionPtrType
FeasibilityAnalysis::getNormalFlowFunction(n_t Curr, n_t Succ) {
    // Only propagate the Zero fact. This prevents any non-zero facts from
    // reaching "weird" functions/blocks via the ICFG.



    auto Inner = std::make_shared<ZeroOnlyFlow<d_t, container_t>>(this);
    return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "ZeroOnly", this, Curr, Succ);
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getCallFlowFunction(n_t CallSite, f_t Callee) {
    // Intraprocedural: do not propagate any facts into the callee.
    auto Inner = std::make_shared<EmptyFlow<d_t, container_t>>();
    return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "CallEmpty", this, CallSite, CallSite);
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getRetFlowFunction(n_t CallSite, f_t Callee,
                                                                                n_t ExitStmt, n_t RetSite) {
    // Intraprocedural: do not propagate any facts back from the callee.
    auto Inner = std::make_shared<EmptyFlow<d_t, container_t>>();
    return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "RetEmpty", this, CallSite, RetSite);
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getCallToRetFlowFunction(n_t CallSite, n_t RetSite,
                                                                                       llvm::ArrayRef<f_t> Callees) {
    // Intraprocedural: keep facts within the caller across a call site.
    auto Inner = std::make_shared<KeepLocalOnCallToRet<d_t, container_t>>();
    return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "CallToRetKeepLocal", this, CallSite, RetSite);
}

FeasibilityAnalysis::EdgeFunctionType
FeasibilityAnalysis::getNormalEdgeFunction(n_t curr, d_t currNode, n_t succ, d_t succNode) {

    auto packBoundaryPhisIfAny =
  [&](EF &Ret, const char *&RetName) -> void {

    // If Ret is already dead, do not generate PHI constraints.
    if (Ret.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(Ret)) {
      llvm::errs() << "[PHI-PACK] skip PHIs because edge is AllBottom\n";
      return;
    }

    const llvm::BasicBlock *PredBB = curr ? curr->getParent() : nullptr;
    const llvm::BasicBlock *SuccBB = succ ? succ->getParent() : nullptr;
    if (!PredBB || !SuccBB || PredBB == SuccBB) return;

    const llvm::Instruction *PredTerm  = PredBB->getTerminator();
    const llvm::Instruction *SuccFirst = Util::firstInst(SuccBB);

    // Only pack on the precise boundary edge in the ICFG
    if (curr != PredTerm || succ != SuccFirst) return;

    FeasibilityPackedEF PhiPack;
    bool Any = false;

    for (const auto &I : *SuccBB) {
      auto *PHI = llvm::dyn_cast<llvm::PHINode>(&I);
      if (!PHI) break;
      if (auto *InV = PHI->getIncomingValueForBlock(PredBB)) {
        Any = true;
        if (!PhiPack.pushBack(PackedOp::mkPhi(PHI, InV))) {
          Ret = EF(std::in_place_type<FeasibilityAllTopEF>);
          RetName = "AllTop (phi pack overflow)";
          return;
        }
      }
    }

    llvm::errs() << "[PHI-PACK] edge " << PredBB->getName() << " -> " << SuccBB->getName()
                 << " Curr=" << *curr << "\n"
                 << " Succ=" << *succ << "\n"
                 << " packed=" << (int)PhiPack.N << "\n";

    if (!Any) return;

    // Build Combined = (Base then PHIs) i.e. PhiPack ∘ Base
    FeasibilityPackedEF Combined;

    // 1) encode Base into PackedOps (when possible)
    if (Ret.template isa<FeasibilityIdentityEF>() ||
        Ret.template isa<psr::EdgeIdentity<l_t>>()) {
      // no base-op
    } else if (Ret.template isa<FeasibilityAssumeIcmpEF>()) {
      const auto &A = Ret.template cast<FeasibilityAssumeIcmpEF>();
      if (!Combined.pushBack(PackedOp::mkAssume(A->Cmp, A->TakeTrueEdge))) {
        Ret = EF(std::in_place_type<FeasibilityAllTopEF>);
        RetName = "AllTop (pack overflow)";
        return;
      }
    } else if (Ret.template isa<FeasibilitySetMemEF>()) {
      const auto &M = Ret.template cast<FeasibilitySetMemEF>();
      if (!Combined.pushBack(PackedOp::mkSetMem(M->Loc, M->ValueId))) {
        Ret = EF(std::in_place_type<FeasibilityAllTopEF>);
        RetName = "AllTop (pack overflow)";
        return;
      }
    } else if (Ret.template isa<FeasibilityPackedEF>()) {
      // Base is already packed: copy its ops
      const auto &B = Ret.template cast<FeasibilityPackedEF>();
      for (uint8_t i = 0; i < B->N; ++i) {
        if (!Combined.pushBack(B->Ops[i])) {
          Ret = EF(std::in_place_type<FeasibilityAllTopEF>);
          RetName = "AllTop (pack overflow)";
          return;
        }
      }
    } else if (Ret.template isa<FeasibilityAllTopEF>() ||
               llvm::isa<psr::AllTop<l_t>>(Ret)) {
      // If base is ⊤, we can just keep ⊤ (PHIs won't recover precision)
      Ret = EF(std::in_place_type<FeasibilityAllTopEF>);
      RetName = "AllTop";
      return;
    } else {
      // Unknown base EF kind => safest widen
      Ret = EF(std::in_place_type<FeasibilityAllTopEF>);
      RetName = "AllTop (cannot pack base)";
      return;
    }

    // 2) append PHIs (must happen after base)
    for (uint8_t i = 0; i < PhiPack.N; ++i) {
      if (!Combined.pushBack(PhiPack.Ops[i])) {
        Ret = EF(std::in_place_type<FeasibilityAllTopEF>);
        RetName = "AllTop (pack overflow)";
        return;
      }
    }

    Ret = EF(std::in_place_type<FeasibilityPackedEF>, Combined);
    RetName = "Packed(Base then PHIs)";
  };

  // Fast gate: only ZERO facts matter
  /*if (!isZeroValue(currNode) || !isZeroValue(succNode)) {
    auto Ret = EF(std::in_place_type<FeasibilityAllBottomEF>);
    llvm::errs() << F_TAG << " EF normal @"; Feasibility::Util::dumpInst(curr);
    llvm::errs() << "\n" << F_TAG << "   currCond="; Feasibility::Util::dumpFact(this, currNode);
    llvm::errs() << "\n" << F_TAG << "   succCond="; Feasibility::Util::dumpFact(this, succNode);
    llvm::errs() << "\n" << F_TAG << "   retEF=AllBottom (non-zero fact)\n";
    return Ret;
  }*/

  EF Ret(std::in_place_type<FeasibilityIdentityEF>);
  const char *RetName = "Identity";

  // ---------------------------------------------------------------------------
  // 1) Conditional branches
  // ---------------------------------------------------------------------------
  if (auto *Br = llvm::dyn_cast<llvm::BranchInst>(curr)) {
    if (!Br->isConditional()) {
      Ret = EF(std::in_place_type<FeasibilityIdentityEF>);
      RetName = "Identity (uncond br)";
    } else {
      const llvm::BasicBlock *TrueBB  = Br->getSuccessor(0);
      const llvm::BasicBlock *FalseBB = Br->getSuccessor(1);

      const llvm::Instruction *SuccI = succ; // n_t assumed Instruction*
      const llvm::BasicBlock  *SuccBB = SuccI ? SuccI->getParent() : nullptr;

      const bool Interesting = llvm::isa<llvm::BranchInst>(curr) ||
                               !Ret.template isa<FeasibilityIdentityEF>();

      /*
      if (Interesting) {
        llvm::errs() << F_TAG << "   Br=" << *Br << "\n";
        llvm::errs() << F_TAG << "   SuccBB=" << (SuccBB ? SuccBB->getName() : "<null>") << "\n";
        llvm::errs() << F_TAG << "   TrueBB=" << TrueBB->getName()
                     << " FalseBB=" << FalseBB->getName() << "\n";
      }
      */

      if (SuccBB != TrueBB && SuccBB != FalseBB) {
        Ret = EF(std::in_place_type<FeasibilityIdentityEF>);
        RetName = "Identity (not a succ edge)";
      } else {
        const bool TakeTrue = (SuccBB == TrueBB);

        llvm::Value *CondV = Br->getCondition();
        while (auto *CastI = llvm::dyn_cast<llvm::CastInst>(CondV))
          CondV = CastI->getOperand(0);

        // br i1 true/false
        if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(CondV)) {
          const bool CondTrue = !CI->isZero();
          const bool EdgeTaken = TakeTrue ? CondTrue : !CondTrue;
          if (EdgeTaken) {
            Ret = EF(std::in_place_type<FeasibilityIdentityEF>);
            RetName = "Identity (const br taken)";
          } else {
            Ret = EF(std::in_place_type<FeasibilityAllBottomEF>);
            RetName = "AllBottom (const br not taken)";
          }

        } else if (auto *ICmp = llvm::dyn_cast<llvm::ICmpInst>(CondV)) {

          if (auto *C0 = llvm::dyn_cast<llvm::ConstantInt>(ICmp->getOperand(0))) {
            if (auto *C1 = llvm::dyn_cast<llvm::ConstantInt>(ICmp->getOperand(1))) {

              const bool CmpTrue =
                llvm::ICmpInst::compare(C0->getValue(), C1->getValue(), ICmp->getPredicate());

              const bool EdgeTaken = TakeTrue ? CmpTrue : !CmpTrue;
              if (EdgeTaken) {
                Ret = EF(std::in_place_type<FeasibilityIdentityEF>);
                RetName = "Identity (folded icmp edge taken)";
              } else {
                Ret = EF(std::in_place_type<FeasibilityAllBottomEF>);
                RetName = "AllBottom (folded icmp edge not taken)";
              }
            } else {
              Ret = EF(std::in_place_type<FeasibilityAssumeIcmpEF>, ICmp, TakeTrue);
              RetName = TakeTrue ? "AssumeIcmp(true)" : "AssumeIcmp(false)";
            }
          } else {
            Ret = EF(std::in_place_type<FeasibilityAssumeIcmpEF>, ICmp, TakeTrue);
            RetName = TakeTrue ? "AssumeIcmp(true)" : "AssumeIcmp(false)";
          }

        } else {
          Ret = EF(std::in_place_type<FeasibilityIdentityEF>);
          RetName = "Identity (non-icmp cond)";
        }
      }
    }

    // ---- PHI PACKING (BOUNDARY-ONLY) AFTER BASE EF IS KNOWN ----
    // If the edge is dead, do NOT pack PHIs.
    if (!(Ret.template isa<FeasibilityAllBottomEF>() ||
          llvm::isa<psr::AllBottom<l_t>>(Ret))) {

      const llvm::BasicBlock *PredBB = curr ? curr->getParent() : nullptr;
      const llvm::BasicBlock *SuccBB = succ ? succ->getParent() : nullptr;

      if (PredBB && SuccBB && PredBB != SuccBB) {
        const llvm::Instruction *PredTerm  = PredBB->getTerminator();
        const llvm::Instruction *SuccFirst = Util::firstInst(SuccBB);

        // IMPORTANT: match exactly the ICFG boundary edge
        if (curr == PredTerm && succ == SuccFirst) {

          FeasibilityPackedEF Pack;
          bool Any = false;

          // Pack *all* PHIs in SuccBB for this incoming edge
          for (const auto &I : *SuccBB) {
            auto *PHI = llvm::dyn_cast<llvm::PHINode>(&I);
            if (!PHI) break;

            if (auto *InV = PHI->getIncomingValueForBlock(PredBB)) {
              Any = true;
              if (!Pack.pushBack(PackedOp::mkPhi(PHI, InV))) {
                Ret = EF(std::in_place_type<FeasibilityAllTopEF>);
                RetName = "AllTop (phi pack overflow)";
                Any = false;
                break;
              }
            }
          }

          llvm::errs() << "[PHI-PACK] edge " << PredBB->getName() << " -> " << SuccBB->getName()
                       << " Curr=" << *curr << "\n"
                       << " Succ=" << *succ << "\n"
                       << " packed=" << (int)Pack.N << "\n";

          if (Any) {
            // We want: result(x) = Pack( Ret(x) )
            // i.e., Pack ∘ Ret
            EF PhiEF(std::in_place_type<FeasibilityPackedEF>, Pack);
            packBoundaryPhisIfAny(Ret, RetName);
            RetName = "PackedPHI ∘ Base";
          }
        }
      }
    } else {
      llvm::errs() << "[PHI-PACK] skip PHIs because edge is AllBottom\n";
    }

    // unified logging + return for branch case
    /*llvm::errs() << F_TAG << " EF normal @"; Feasibility::Util::dumpInst(curr);
    llvm::errs() << "\n" << F_TAG << "   currCond="; Feasibility::Util::dumpFact(this, currNode);
    llvm::errs() << "\n" << F_TAG << "   succCond="; Feasibility::Util::dumpFact(this, succNode);
    llvm::errs() << "\n" << F_TAG << "   retEF=" << RetName << "\n";*/
    return Ret;
  }

  // ---------------------------------------------------------------------------
  // 2) STORE / LOAD effects
  // ---------------------------------------------------------------------------
  if (auto *storeinst = llvm::dyn_cast<llvm::StoreInst>(curr)) {
    const llvm::Value *valOp = storeinst->getValueOperand();
    const llvm::Value *ptrOp = storeinst->getPointerOperand()->stripPointerCasts();

    if (auto *constval = llvm::dyn_cast<llvm::ConstantInt>(valOp)) {
      unsigned bw = constval->getBitWidth();
      uint64_t bits = constval->getValue().getZExtValue();
      z3::expr c = store->ctx().bv_val(bits, bw);
      const auto valueId = store->internExpr(c);
      Ret = EF(std::in_place_type<FeasibilitySetMemEF>, ptrOp, valueId);
      RetName = "SetMem(const)";
    } else {
      Ret = EF(std::in_place_type<FeasibilityIdentityEF>);
      RetName = "Identity (store non-const)";
    }

  } else if (auto *loadInst = llvm::dyn_cast<llvm::LoadInst>(curr)) {
    const llvm::Value *loc = loadInst->getPointerOperand()->stripPointerCasts();
    const llvm::Value *key = loadInst;
    Ret = EF(std::in_place_type<FeasibilitySetSSAEF>, key, loc);
    RetName = "SetSSA(load)";
  } else {
    Ret = EF(std::in_place_type<FeasibilityIdentityEF>);
    RetName = "Identity";
  }

  // ---- PHI PACKING FOR NON-BRANCH CASES TOO (AFTER BASE EF) ----
  if (!(Ret.template isa<FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<l_t>>(Ret))) {

    const llvm::BasicBlock *PredBB = curr ? curr->getParent() : nullptr;
    const llvm::BasicBlock *SuccBB = succ ? succ->getParent() : nullptr;

    if (PredBB && SuccBB && PredBB != SuccBB) {
      const llvm::Instruction *PredTerm  = PredBB->getTerminator();
      const llvm::Instruction *SuccFirst = Util::firstInst(SuccBB);

      if (curr == PredTerm && succ == SuccFirst) {
        FeasibilityPackedEF Pack;
        bool Any = false;

        for (const auto &I : *SuccBB) {
          auto *PHI = llvm::dyn_cast<llvm::PHINode>(&I);
          if (!PHI) break;
          if (auto *InV = PHI->getIncomingValueForBlock(PredBB)) {
            Any = true;
            if (!Pack.pushBack(PackedOp::mkPhi(PHI, InV))) {
              Ret = EF(std::in_place_type<FeasibilityAllTopEF>);
              RetName = "AllTop (phi pack overflow)";
              Any = false;
              break;
            }
          }
        }

        llvm::errs() << "[PHI-PACK] edge " << PredBB->getName() << " -> " << SuccBB->getName()
                     << " Curr=" << *curr << "\n"
                     << " Succ=" << *succ << "\n"
                     << " packed=" << (int)Pack.N << "\n";

        if (Any) {
          EF PhiEF(std::in_place_type<FeasibilityPackedEF>, Pack);
          packBoundaryPhisIfAny(Ret, RetName);
          RetName = "PackedPHI ∘ Base";
        }
      }
    }
  } else {
    llvm::errs() << "[PHI-PACK] skip PHIs because edge is AllBottom\n";
  }

  // unified logging
  /*llvm::errs() << F_TAG << " EF normal @"; Feasibility::Util::dumpInst(curr);
  llvm::errs() << "\n" << F_TAG << "   currCond="; Feasibility::Util::dumpFact(this, currNode);
  llvm::errs() << "\n" << F_TAG << "   succCond="; Feasibility::Util::dumpFact(this, succNode);
  llvm::errs() << "\n" << F_TAG << "   retEF=" << RetName << "\n";*/

  return Ret;
}



const llvm::BasicBlock* FeasibilityAnalysis::getSuccBB(FeasibilityAnalysis::n_t Succ) {
    if (!Succ) {
        return nullptr;
    }
    return Succ->getParent();
}

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getCallEdgeFunction(n_t CallSite, d_t SrcNode,
                                                                              f_t DestFun, d_t DestNode) {
    return EF(std::in_place_type<FeasibilityIdentityEF>);
}

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getReturnEdgeFunction(n_t CallSite, f_t Callee,
                                                                                n_t ExitStmt, d_t ExitNode,
                                                                                n_t RetSite, d_t RetNode) {
    return EF(std::in_place_type<FeasibilityIdentityEF>);
}

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getCallToRetEdgeFunction(n_t CallSite, d_t CallNode,
                                                                                   n_t RetSite, d_t RetSiteNode,
                                                                                   llvm::ArrayRef<f_t> Callees) {
    return EF(std::in_place_type<FeasibilityIdentityEF>);
}


}  // namespace Feasibility
