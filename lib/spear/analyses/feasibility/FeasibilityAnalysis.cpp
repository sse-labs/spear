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

    // Create initial lattice element:
    // PC = true (topId)
    // Env = 0 (empty)
    // Kind = Top
    l_t init = l_t::createBottom(this->manager.get());

    for (n_t SP : ICFG->getStartPointsOf(Main)) {
        Seeds.addSeed(SP, Zero, init);
    }

    return Seeds;
}

FeasibilityAnalysis::d_t FeasibilityAnalysis::zeroValue() const {
    // Creates an empty stupid fact
    return static_cast<d_t>(psr::LLVMZeroValue::getInstance());
}

bool FeasibilityAnalysis::isZeroValue(d_t Fact) const noexcept{
    // This is mere a dummy function, as we only propagate zero values
    return base_t::isZeroValue(Fact);
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::topElement() {
    return FeasibilityElement::createTop(this->manager.get());
}

FeasibilityElement FeasibilityAnalysis::bottomElement() {
    return FeasibilityElement::createBottom(this->manager.get());
}

psr::EdgeFunction<FeasibilityAnalysis::l_t> FeasibilityAnalysis::allTopFunction() {
    return psr::AllTop<l_t>{};
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::join(l_t Lhs, l_t Rhs) {
    auto kindToStr = [](const l_t &V) -> const char * {
        if (V.isTop()) return "Top";
        if (V.isBottom()) return "Bottom";
        return "Normal";
    };

    auto res = Lhs.join(Rhs);

    llvm::errs() << "[FDBG] VALUE.join "
                 << "Lhs=" << kindToStr(Lhs)
                 << " Rhs=" << kindToStr(Rhs)
                 << " -> Result=" << kindToStr(res)
                 << "\n";
    return res;
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
FeasibilityAnalysis::getNormalEdgeFunction(n_t curr, d_t /*currNode*/, n_t succ,
                                           d_t /*succNode*/) {
  const llvm::Instruction *SuccI = succ;
  const llvm::Instruction *CurrI = curr;

  const llvm::BasicBlock *SuccBB = SuccI ? SuccI->getParent() : nullptr;
  const llvm::BasicBlock *CurrBB = CurrI ? CurrI->getParent() : nullptr;

  auto *M = this->manager.get();

    auto dumpEdge = [&](const char *tag, const EF &ef, const FeasibilityClause *C = nullptr) {
        llvm::errs()
          << "[FDBG][EDGE] " << tag
          << " currI=" << (CurrI ? CurrI->getOpcodeName() : "<null>")
          << " @" << (const void*)CurrI
          << " (" << (CurrBB ? CurrBB->getName() : "<null>") << ")"
          << "  -> succI=" << (SuccI ? SuccI->getOpcodeName() : "<null>")
          << " @" << (const void*)SuccI
          << " (" << (SuccBB ? SuccBB->getName() : "<null>") << ")"
          << "  term=" << (CurrBB && CurrBB->getTerminator() ? CurrBB->getTerminator()->getOpcodeName() : "<null>")
          << "  ef=" << efName(ef)
          << (C ? (" steps=" + std::to_string(C->Steps.size())) : "")
          << "\n";
    };

  if (!SuccBB || !CurrBB)
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);

  // -------------------------------------------------------------------------
  // IMPORTANT (PhASAR instruction supergraph):
  // For edges that cross basic blocks, 'curr' is NOT guaranteed to be the
  // terminator. Therefore, decide branch semantics solely from:
  //   CurrBB --(terminator)-> SuccBB
  // -------------------------------------------------------------------------

  const bool crossBB = (CurrBB != SuccBB);

  // Intra-block edges never contribute feasibility constraints.
  // (Constraints are attached to terminator->successor edges.)
  if (!crossBB) {
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
  }

  // Build clause for the CFG edge CurrBB -> SuccBB
  FeasibilityClause clause;

  // If successor begins with PHIs, record the phi translation as a step
  if (!SuccBB->empty() && llvm::isa<llvm::PHINode>(&*SuccBB->begin())) {
    clause.Steps.push_back(ClauseStep::mkPhi(CurrBB, SuccBB));
  }

  // Decide branch on the terminator of CurrBB
  const llvm::Instruction *TermI = CurrBB->getTerminator();
  auto *br = llvm::dyn_cast_or_null<llvm::BranchInst>(TermI);

  // Non-conditional terminator (or not a branch): only phi-step if any.
  if (!br || !br->isConditional()) {
    if (clause.Steps.empty())
      return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
  }

  const llvm::BasicBlock *TrueBB  = br->getSuccessor(0);
  const llvm::BasicBlock *FalseBB = br->getSuccessor(1);

  if (SuccBB != TrueBB && SuccBB != FalseBB) {
    // This shouldn't happen for a CFG successor edge; be conservative
    if (clause.Steps.empty())
      return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
    return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
  }

  const bool onTrueEdge = (SuccBB == TrueBB);

  // Strip casts from branch condition
  llvm::Value *CondV = br->getCondition();
  while (auto *CastI = llvm::dyn_cast<llvm::CastInst>(CondV)) {
    CondV = CastI->getOperand(0);
  }

  // Only support ICmp for now
  if (auto *icmp = llvm::dyn_cast<llvm::ICmpInst>(CondV)) {
    // Constant-fold if both operands constant
    if (auto *c0 = llvm::dyn_cast<llvm::ConstantInt>(icmp->getOperand(0))) {
      if (auto *c1 = llvm::dyn_cast<llvm::ConstantInt>(icmp->getOperand(1))) {
        const bool cmpTrue = llvm::ICmpInst::compare(
            c0->getValue(), c1->getValue(), icmp->getPredicate());
        const bool edgeTaken = onTrueEdge ? cmpTrue : !cmpTrue;

        if (!edgeTaken) {
          // infeasible edge => UNREACHABLE
          return EF(std::in_place_type<FeasibilityAllTopEF>);
        }

        // feasible edge; keep only phi-step if any
        if (clause.Steps.empty())
          return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
        return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
      }
    }

    // General case: add constraint step
    clause.Steps.push_back(ClauseStep::mkICmp(icmp, onTrueEdge));
      EF out = EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
      dumpEdge("RET", out, &out.template dyn_cast<FeasibilityANDFormulaEF>()->Clause); // or just &clause before move
      return out;
  }

  // Fallback: unknown condition; only phi-step if any
  if (clause.Steps.empty())
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
  return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
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
