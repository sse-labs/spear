/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <phasar/PhasarLLVM/ControlFlow/LLVMBasedICFG.h>
#include <phasar/PhasarLLVM/DataFlow/IfdsIde/LLVMZeroValue.h>

#include <utility>
#include <memory>

#include "analyses/feasibility/FeasibilityEdgeFunction.h"
#include "analyses/feasibility/FeasibilityAnalysis.h"

#include "analyses/feasibility/FeasibilityAnalysisManager.h"

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
    ContainerT computeTargets(D) override { return ContainerT{}; }
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
    : base_t(IRDB, {"main"},
    std::optional<d_t>(static_cast<d_t>(psr::LLVMZeroValue::getInstance()))) {
    manager = std::make_unique<FeasibilityAnalysisManager>(std::make_unique<z3::context>());
    this->ICFG = ICFG;
}


psr::InitialSeeds<FeasibilityAnalysis::n_t, FeasibilityAnalysis::d_t, FeasibilityAnalysis::l_t>
FeasibilityAnalysis::initialSeeds() {
    psr::InitialSeeds<n_t, d_t, l_t> Seeds;

    // We only analyse outgoing from the main function
    auto *Main = this->getProjectIRDB()->getFunctionDefinition("main");
    if (!Main || Main->isDeclaration()) {
        // if this error case occurs we are not in a valid program...
        return Seeds;
    }

    // Start with a zero fact and an empty element
    const d_t Zero = this->getZeroValue();
    l_t init = emptyElement();

    // For each starting point of main we add a set with the empty element
    for (n_t SP : ICFG->getStartPointsOf(Main)) {
        Seeds.addSeed(SP, Zero, init);
    }

    return Seeds;
}

FeasibilityAnalysis::d_t FeasibilityAnalysis::zeroValue() const {
    // Stick to the phasar zero value and do not introduce another element here
    return static_cast<d_t>(psr::LLVMZeroValue::getInstance());
}

bool FeasibilityAnalysis::isZeroValue(d_t Fact) const noexcept {
    return base_t::isZeroValue(Fact);
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::topElement() {
    // Create a new Feasibility element with empty properties
    return emptyElement();
}

FeasibilityElement FeasibilityAnalysis::bottomElement() {
    // Create a new Feasibility element with bottom properties
    return FeasibilityElement::createElement(this->manager.get(),
                                    FeasibilityElement::topId,
                                            FeasibilityElement::Kind::Bottom);
}

FeasibilityElement FeasibilityAnalysis::emptyElement() {
    // Create a new Feasibility element with empty properties
    return FeasibilityElement::createElement(this->manager.get(),
                                    FeasibilityElement::topId,
                                            FeasibilityElement::Kind::Empty);
}

psr::EdgeFunction<l_t> FeasibilityAnalysis::allTopFunction() {
    return psr::AllTop<l_t>{emptyElement()};
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::join(l_t Lhs, l_t Rhs) {
    // Delegate the call to the lattice join
    return Lhs.join(Rhs);
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getNormalFlowFunction(n_t Curr, n_t Succ) {
    // Only propagate the Zero fact behind our debug flow
    auto Inner = std::make_shared<ZeroOnlyFlow<d_t, container_t>>(this);
    return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "ZeroOnly", this, Curr, Succ);
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getCallFlowFunction(n_t CallSite, f_t Callee) {
    // Intraprocedural: do not propagate any facts into the callee.
    auto Inner = std::make_shared<EmptyFlow<d_t, container_t>>();
    return std::make_shared<DebugFlow<d_t, container_t>>(
        Inner, "CallEmpty", this, CallSite, CallSite);
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getRetFlowFunction(n_t CallSite, f_t Callee,
                                                                                n_t ExitStmt, n_t RetSite) {
    // Intraprocedural: do not propagate any facts back from the callee.
    auto Inner = std::make_shared<EmptyFlow<d_t, container_t>>();
    return std::make_shared<DebugFlow<d_t, container_t>>(
        Inner, "RetEmpty", this, CallSite, RetSite);
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getCallToRetFlowFunction(n_t CallSite, n_t RetSite,
                                                                                       llvm::ArrayRef<f_t> Callees) {
    // Intraprocedural: keep facts within the caller across a call site.
    auto Inner = std::make_shared<KeepLocalOnCallToRet<d_t, container_t>>();
    return std::make_shared<DebugFlow<d_t, container_t>>(
        Inner, "CallToRetKeepLocal", this, CallSite, RetSite);
}

FeasibilityAnalysis::EdgeFunctionType
FeasibilityAnalysis::getNormalEdgeFunction(n_t curr, d_t currNode, n_t succ, d_t succNode) {
    // Search or the blocks we are currently operating in
    const llvm::BasicBlock *SuccBB = succ ? succ->getParent() : nullptr;
    const llvm::BasicBlock *CurrBB = curr ? curr->getParent() : nullptr;

    // Query a local instance of the manager component
    auto *localManager = this->manager.get();

    // If we cannot determine blocks: be conservative and do nothing.
    if (!SuccBB || !CurrBB) {
        return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
    }

    // We only care about branch conditions for pruning; everything else is identity.
    auto *br = llvm::dyn_cast<llvm::BranchInst>(curr);
    if (!br) {
        return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
    }

    // Unconditional branch: no constraint. Still, phi substitution will be applied
    // later when constraints are built (in successor), so nothing to add here.
    if (!br->isConditional()) {
        return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
    }

    // Query the successor blocks of the branch instruction
    const llvm::BasicBlock *TrueBB  = br->getSuccessor(0);
    const llvm::BasicBlock *FalseBB = br->getSuccessor(1);

    // Sanity check: Validate that the determined successor block is either the true or the false BB.
    // Otherwise, warn and do nothing
    if (SuccBB != TrueBB && SuccBB != FalseBB) {
        // This case should only occur if we encounter malformed programs
        llvm::errs() << "WARNING: successor does not match branch successors; using Identity\n";
        return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
    }

    // Determine at which edge we are currently looking. Therefore check, on which we are currently operating
    const bool onTrueEdge = (SuccBB == TrueBB);

    // Strip casts from the condition
    llvm::Value *CondV = br->getCondition();
    while (auto *CastI = llvm::dyn_cast<llvm::CastInst>(CondV)) {
        CondV = CastI->getOperand(0);
    }

    /**
     * Query the ICMP instruction the previous found branch instruction and its condition depend on
     * branch conditions are either conditional or non-conditional.
     * Conditional branch instructions (the ones of interest here) always use an ICMP instruction to calculate the
     * boolean check of the conditional jump
     *
     * i.e:
     *
     * %cmp = icmp eq %length, 10
     * br i1 %cmp, label %truecase, label %falsecase
     */
    if (auto *icmp = llvm::dyn_cast<llvm::ICmpInst>(CondV)) {
        return EF(std::in_place_type<FeasibilityAddAtomsEF>, localManager, CurrBB, SuccBB, icmp, onTrueEdge);
    }

    /**
     * TODO: Maybe add handling for other comparison methods here...
     *
     */

    // If the branch is conditional but the comparison happens with other operations like the above checked,
    // we bail out and do nothing.
    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
}

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getCallEdgeFunction(n_t CallSite, d_t SrcNode,
                                                                              f_t DestFun, d_t DestNode) {
    // We only deal with intraprocedual edges. On Edges leaving the function do nothing
    return EF(std::in_place_type<psr::EdgeIdentity<FeasibilityElement>>);
}

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getReturnEdgeFunction(n_t CallSite, f_t Callee,
                                                                                n_t ExitStmt, d_t ExitNode,
                                                                                n_t RetSite, d_t RetNode) {
    // We only deal with intraprocedual edges. On Edges returning to the function do nothing
    return EF(std::in_place_type<psr::EdgeIdentity<FeasibilityElement>>);
}

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getCallToRetEdgeFunction(n_t CallSite, d_t CallNode,
                                                                                   n_t RetSite, d_t RetSiteNode,
                                                                                   llvm::ArrayRef<f_t> Callees) {
    // Only intraprocedual
    return EF(std::in_place_type<psr::EdgeIdentity<FeasibilityElement>>);
}


}  // namespace Feasibility
