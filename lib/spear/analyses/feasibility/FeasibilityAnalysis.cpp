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
    l_t init = l_t::createElement(
        this->manager.get(),
        l_t::topId,          // pc = true baseline
        l_t::Kind::Bottom,   // REACHED baseline (yes, Bottom kind now means “reached”)
    0);

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
    return FeasibilityElement::createElement(this->manager.get(), FeasibilityElement::topId, FeasibilityElement::Kind::Top);
}

FeasibilityElement FeasibilityAnalysis::bottomElement() {
    return FeasibilityElement::createElement(this->manager.get(), FeasibilityElement::bottomId, FeasibilityElement::Kind::Bottom);
}

psr::EdgeFunction<FeasibilityAnalysis::l_t> FeasibilityAnalysis::allTopFunction() {
    return psr::AllTop<l_t>{};
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::join(l_t Lhs, l_t Rhs) {
    llvm::errs() << "[FDBG] VALUE.join U1=" << (Lhs.isTop()?"T":"F")
             << " U2=" << (Rhs.isTop()?"T":"F")
             << " -> " << ((Lhs.isTop() && Rhs.isTop())?"T":"F") << "\n";
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

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getNormalEdgeFunction(n_t curr, d_t /*currNode*/, n_t succ, d_t /*succNode*/) {
    const llvm::Instruction *SuccI = succ;
    const llvm::Instruction *CurrI = curr;

    const llvm::BasicBlock *SuccBB = SuccI ? SuccI->getParent() : nullptr;
    const llvm::BasicBlock *CurrBB = CurrI ? CurrI->getParent() : nullptr;

    auto *localManager = this->manager.get();

    // Fallback, if we cannot determine the edge type, we just return an identity function.
    if (!SuccBB || !CurrBB) {
        return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
    }

    // Build a clause
    FeasibilityClause clause;

    // If we encounter a edge which is part of a PHI translation, we need to add the corresponding phi step to the
    // clause, as it needs to be applied before any constraints on the edge. We can identify such edges by
    // checking if the successor block starts with a PHINode, as PHI translations are always represented by
    // edges that lead into a block with a PHINode (as the PHINodes are the ones that perform the actual translation).
    if (llvm::isa<llvm::PHINode>(SuccBB->begin())) {
        clause.PhiChain.push_back(PhiStep(CurrBB, SuccBB));
    }

    // Check if we are currently in a br instruction. Only add the phi step if we are in a br instruction with a
    // condition, as only such instructions can be part of a phi translation. If we are in a br instruction
    // without a condition, we are in an unconditional branch and thus not in a phi translation, even if
    // the successor block starts with a PHINode.
    auto *br = llvm::dyn_cast<llvm::BranchInst>(CurrI);
    if (!br || !br->isConditional()) {
        if (clause.PhiChain.empty() && clause.Constrs.empty()) {
            return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
        }

        return EF(std::in_place_type<FeasibilityANDFormulaEF>, localManager, std::move(clause));
    }

    // Determine which successor edge we are on
    const llvm::BasicBlock *TrueBB  = br->getSuccessor(0);
    const llvm::BasicBlock *FalseBB = br->getSuccessor(1);

    // Check if the successor is neither the true sucessor nor the false sucessor
    if (SuccBB != TrueBB && SuccBB != FalseBB) {
        // This case should never happen. If it does, burn down your device
        llvm::errs() << "WARNING: successor does not match branch successors; using Identity\n";
        if (clause.PhiChain.empty()) {
            return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
        }

        return EF(std::in_place_type<FeasibilityANDFormulaEF>, localManager, std::move(clause));
    }

    // Determine at which edge we are currently looking. If the successor is the true successor, we are on the true
    // edge, otherwise we are on the false edge. This is relevant for adding the correct constraint to the clause,
    // as we need to add the constraint that corresponds to the condition of the branch instruction and
    // whether we are on the true or false edge.
    const bool onTrueEdge = (SuccBB == TrueBB);

    // Prepare the conditon
    // Strip casts from the condition
    llvm::Value *CondV = br->getCondition();
    while (auto *CastI = llvm::dyn_cast<llvm::CastInst>(CondV)) {
        CondV = CastI->getOperand(0);
    }

    // Constant conditions
    if (auto *icmp = llvm::dyn_cast<llvm::ICmpInst>(CondV)) {
        //llvm::errs() << "Caculating for ICMP instruction: " << *icmp << "\n";
        // Determine if we can shortcut, i.e., if the condition is a constant value,
        // we can directly return bottom if the condition is not satisfied.
        if (auto *c0 = llvm::dyn_cast<llvm::ConstantInt>(icmp->getOperand(0))) {
            if (auto *c1 = llvm::dyn_cast<llvm::ConstantInt>(icmp->getOperand(1))) {
                // Determine if the condition is satisfied on this edge. If it is, we can return top, as the
                // path condition on this edge is true. If it is not, we can return bottom, as the path condition
                // on this edge is false.
                const bool cmpTrue = llvm::ICmpInst::compare(c0->getValue(), c1->getValue(), icmp->getPredicate());
                const bool edgeTaken = onTrueEdge ? cmpTrue : !cmpTrue;

                // If we can shortcut, i.e the constant condition is not
                // satisfied on this edge, we return bottom, as the path condition on this edge is false.
                if (!edgeTaken) {
                    return EF(std::in_place_type<FeasibilityAllTopEF>);
                }

                // Check if we encountered a phi. If not just return identity
                if (clause.PhiChain.empty()) {
                    return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
                }

                return EF(std::in_place_type<FeasibilityANDFormulaEF>, localManager, std::move(clause));
            }
        }

        // General case: We encoutered a generic ICMP
        // We need to add the condition of the branch instruction as a constraint to the clause, as it is part of the path condition on this edge.
        clause.Constrs.push_back(LazyICmp(icmp, onTrueEdge));
        return EF(std::in_place_type<FeasibilityANDFormulaEF>, localManager, std::move(clause));
    }

    // Fallback case: We encountered a non-constant condition that is not an ICMP instruction.
    // In this case, we cannot determine the condition on this edge, so we just return the clause
    // with any potential phi translation steps, but without adding any constraints to it,
    // as we do not know which constraints to add. If there are no phi translation steps, we just return identity.

    if (clause.PhiChain.empty()) {
        return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
    }

    return EF(std::in_place_type<FeasibilityANDFormulaEF>, localManager, std::move(clause));
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
