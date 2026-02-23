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


FeasibilityAnalysis::FeasibilityAnalysis(llvm::FunctionAnalysisManager *FAM,
                                         const psr::LLVMProjectIRDB *IRDB,
                                         const psr::LLVMBasedICFG *ICFG)
    : base_t(IRDB, {"main"}, std::optional<d_t>(static_cast<d_t>(psr::LLVMZeroValue::getInstance()))) {
    (void)FAM;

    manager = std::make_unique<FeasibilityAnalysisManager>(std::make_unique<z3::context>());
    this->ICFG = ICFG;
}


psr::InitialSeeds<FeasibilityAnalysis::n_t,
                  FeasibilityAnalysis::d_t,
                  FeasibilityAnalysis::l_t>
FeasibilityAnalysis::initialSeeds() {

    psr::InitialSeeds<n_t, d_t, l_t> Seeds;

    auto *Main = this->getProjectIRDB()->getFunctionDefinition("main");
    if (!Main || Main->isDeclaration()) {
        return Seeds;
    }

    const d_t Zero = this->getZeroValue();

    l_t init = emptyElement();

    for (n_t SP : ICFG->getStartPointsOf(Main)) {
        Seeds.addSeed(SP, Zero, init);
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
    return FeasibilityElement::createElement(this->manager.get(), FeasibilityElement::topId, FeasibilityElement::Kind::Top);
}

FeasibilityElement FeasibilityAnalysis::bottomElement() {
    return FeasibilityElement::createElement(this->manager.get(), FeasibilityElement::bottomId, FeasibilityElement::Kind::Bottom);
}

FeasibilityElement FeasibilityAnalysis::emptyElement() {
    return FeasibilityElement::createElement(this->manager.get(), FeasibilityElement::topId, FeasibilityElement::Kind::Empty);
}

psr::EdgeFunction<l_t> FeasibilityAnalysis::allTopFunction() {
    return psr::AllTop<l_t>{};
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::join(l_t Lhs, l_t Rhs) {
    /*llvm::errs() << "[FDBG] VALUE.join U1=" << (Lhs.isTop()?"T":"F")
             << " U2=" << (Rhs.isTop()?"T":"F")
             << " -> " << ((Lhs.isTop() && Rhs.isTop())?"T":"F") << "\n";*/
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
FeasibilityAnalysis::getNormalEdgeFunction(n_t curr, d_t /*currNode*/,
                                           n_t succ, d_t /*succNode*/) {
  const llvm::Instruction *SuccI = succ;
  const llvm::Instruction *CurrI = curr;

  const llvm::BasicBlock *SuccBB = SuccI ? SuccI->getParent() : nullptr;
  const llvm::BasicBlock *CurrBB = CurrI ? CurrI->getParent() : nullptr;

  auto *M = this->manager.get();

  // If we cannot determine blocks: be conservative (do nothing).
  if (!SuccBB || !CurrBB) {
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
  }

  // We only care about branch conditions for pruning; everything else is identity.
  auto *br = llvm::dyn_cast<llvm::BranchInst>(CurrI);
  if (!br) {
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
  }

  // Unconditional branch: no constraint. Still, phi substitution will be applied
  // later when constraints are built (in successor), so nothing to add here.
  if (!br->isConditional()) {
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
  }

  const llvm::BasicBlock *TrueBB  = br->getSuccessor(0);
  const llvm::BasicBlock *FalseBB = br->getSuccessor(1);

  if (SuccBB != TrueBB && SuccBB != FalseBB) {
    llvm::errs() << "WARNING: successor does not match branch successors; using Identity\n";
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
  }

  const bool onTrueEdge = (SuccBB == TrueBB);

  // Strip casts from the condition
  llvm::Value *CondV = br->getCondition();
  while (auto *CastI = llvm::dyn_cast<llvm::CastInst>(CondV)) {
    CondV = CastI->getOperand(0);
  }

  // Constant conditions: if we can decide the edge is never taken, return UNREACHABLE.
  // NOTE: in your NEW MODEL, unreachable is lattice Bottom (Kind::Bottom).
  // If your AllTopEF is "Top=true", then use AllBottomEF here instead.
  if (auto *icmp = llvm::dyn_cast<llvm::ICmpInst>(CondV)) {
    if (auto *c0 = llvm::dyn_cast<llvm::ConstantInt>(icmp->getOperand(0))) {
      if (auto *c1 = llvm::dyn_cast<llvm::ConstantInt>(icmp->getOperand(1))) {
        const bool cmpTrue =
            llvm::ICmpInst::compare(c0->getValue(), c1->getValue(), icmp->getPredicate());
        const bool edgeTaken = onTrueEdge ? cmpTrue : !cmpTrue;

        if (!edgeTaken) {
          // Edge is infeasible.
          // Choose the EF that maps to lattice Bottom (unreachable).
            EF(std::in_place_type<FeasibilityAddAtomsEF>, M, CurrBB, SuccBB, icmp, edgeTaken);
        }

        // Edge always taken: adds no constraint.
          EF(std::in_place_type<FeasibilityAddAtomsEF>, M, CurrBB, SuccBB, icmp, edgeTaken);
      }
    }

    // General case: store a LazyAtom (phi-aware) and let computeTarget:
    //  1) applyPhiPack(source.env, CurrBB, SuccBB)
    //  2) build z3 atom under that env
    //  3) addAtom to pc-set
    /*llvm::errs() << "DEBUG: Adding FeasibilityAddAtomsEF for edge " << CurrBB->getName() << " -> " << SuccBB->getName()
         << " with condition " << *icmp << " on " << (onTrueEdge ? "true" : "false") << " branch\n";*/
    return EF(std::in_place_type<FeasibilityAddAtomsEF>, M, CurrBB, SuccBB, icmp, onTrueEdge);
  }

  // Non-icmp condition: unknown, do nothing.
  return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
}

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getCallEdgeFunction(n_t CallSite, d_t SrcNode,
                                                                              f_t DestFun, d_t DestNode) {
    return EF(std::in_place_type<psr::EdgeIdentity<FeasibilityElement>>);
}

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getReturnEdgeFunction(n_t CallSite, f_t Callee,
                                                                                n_t ExitStmt, d_t ExitNode,
                                                                                n_t RetSite, d_t RetNode) {
    return EF(std::in_place_type<psr::EdgeIdentity<FeasibilityElement>>);
}

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getCallToRetEdgeFunction(n_t CallSite, d_t CallNode,
                                                                                   n_t RetSite, d_t RetSiteNode,
                                                                                   llvm::ArrayRef<f_t> Callees) {
    return EF(std::in_place_type<psr::EdgeIdentity<FeasibilityElement>>);
}


}  // namespace Feasibility
