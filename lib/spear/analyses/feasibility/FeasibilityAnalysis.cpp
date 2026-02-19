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


psr::InitialSeeds<FeasibilityAnalysis::n_t, FeasibilityAnalysis::d_t, FeasibilityAnalysis::l_t>
FeasibilityAnalysis::initialSeeds() {
    psr::InitialSeeds<n_t, d_t, l_t> Seeds;

    auto Main = this->getProjectIRDB()->getFunctionDefinition("main");
    if (!Main || Main->isDeclaration()) {
        return Seeds;
    }

    // We initially create a zero fact (we do not propagate other facts)
    const d_t Zero = this->getZeroValue();
    // We create an initial path condition with the top element signaling "True" as path conditions,
    // as the entry to the function is always true in terms of path conditions.
    const l_t IdeNeutral = topElement();

    for (n_t SP : ICFG->getStartPointsOf(&*Main)) {
        // Seed the analysis
        Seeds.addSeed(SP, Zero, IdeNeutral);
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
    // Path-condition domain with join = OR.
    // Top  := true  (least restrictive)  -> absorbing for OR
    // Bottom := false (infeasible)       -> identity for OR

    if (Lhs.isBottom() || Rhs.isBottom()) {
        return bottomElement();
    }

    if (Lhs.isTop()) {
        return Rhs;
    }

    if (Rhs.isTop()) {
        return Lhs;
    }

    // Normal: disjunction of the two formulas
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

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getNormalEdgeFunction(n_t curr, d_t currNode, n_t succ, d_t succNode) {
    /**
     * Path conditions are generated at branch instructions
     *
     */
    if (auto *Br = llvm::dyn_cast<llvm::BranchInst>(curr)) {
        // Early return if the branch is unconditional, as it does not contribute to path conditions.
        if (!Br->isConditional()) {
            return EF(std::in_place_type<psr::EdgeIdentity<FeasibilityElement>>);
        }

        llvm::Value *CondV = Br->getCondition();
        while (auto *CastI = llvm::dyn_cast<llvm::CastInst>(CondV)) {
            CondV = CastI->getOperand(0);
        }

        // Determine the blocks that follow out branch instruction
        const llvm::BasicBlock *TrueBB  = Br->getSuccessor(0);
        const llvm::BasicBlock *FalseBB = Br->getSuccessor(1);

        // Additionally determine the successor basic block this analysis step leads to
        // Phasar will iterate over all possible paths so we can just check which successor matches the current step's successor.
        const llvm::Instruction *SuccI = succ; // n_t assumed Instruction*
        const llvm::BasicBlock  *SuccBB = SuccI ? SuccI->getParent() : nullptr;

        if (SuccBB != TrueBB && SuccBB != FalseBB) {
            // This should not happen, as Phasar should only call this function for valid successor nodes.
            llvm::errs() << "WARNING: Successor block does not match any of the branch's successors. Falling back to identity edge function.\n";
        } else {
            bool areWeInTheTrueBranch = (SuccBB == TrueBB);

            // Handle simple cases where the condition is a constant or an ICmp instruction with constant operands.
            // Example: br i1 true, label %then, label %else
            if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(CondV)) {
                // Case where the condition is just a constant integer (e.g., br i1 true, ... or br i1 false, ...)
                // Queries the value of the constant to determine if the branch is taken or not.
                // 0 -> false, non-zero -> true
                const bool CondTrue = !CI->isZero();
                // Determine which edge was taken based on the branch condition and the successor block.
                // Determines if we are in the true or false branch and whether the condition evaluates to true or false to figure out if the edge is taken.
                bool trueEdgeTaken = areWeInTheTrueBranch ? CondTrue : !CondTrue;


                if (trueEdgeTaken) {
                    // If we are on the true edge, we return a top element, as the path condition is satisfied and does not constrain the analysis.
                    return EF(std::in_place_type<FeasibilityAllTopEF>);
                } else {
                    // If we are on the false edge, we return a bottom element, as the path condition is not satisfied and thus makes the path infeasible.
                    return EF(std::in_place_type<FeasibilityAllBottomEF>);
                }

            } else if (auto *ICmp = llvm::dyn_cast<llvm::ICmpInst>(CondV)) {
                // Determine if the operators are constants
                if (auto *C0 = llvm::dyn_cast<llvm::ConstantInt>(ICmp->getOperand(0))) {
                    if (auto *C1 = llvm::dyn_cast<llvm::ConstantInt>(ICmp->getOperand(1))) {
                        // Special case, were both operands are constants. We can directly evaluate the condition and return either top or bottom based on whether the condition is satisfied or not.
                        const bool CmpTrue = llvm::ICmpInst::compare(C0->getValue(), C1->getValue(), ICmp->getPredicate());
                        bool trueEdgeTaken = areWeInTheTrueBranch ? CmpTrue : !CmpTrue;

                        if (trueEdgeTaken) {
                            // If we are on the true edge, we return a top element, as the path condition is satisfied and does not constrain the analysis.
                            return EF(std::in_place_type<FeasibilityAllTopEF>);
                        } else {
                            // If we are on the false edge, we return a bottom element, as the path condition is not satisfied and thus makes the path infeasible.
                            return EF(std::in_place_type<FeasibilityAllBottomEF>);
                        }
                    }
                }

                auto localManager = this->manager.get();
                // llvm::errs() << "Handling ICmp instruction in getNormalEdgeFunction. Condition: " << *ICmp << "\n";

                // Create a local representation of the constraint represented by the ICmp instruction,
                // which we can then add to the path condition in the edge function.
                // This allows us to keep track of the constraint for later use in the analysis, without immediately evaluating it.
                z3::expr constraintExpr = Util::createConstraintFromICmp(localManager, ICmp, areWeInTheTrueBranch);

                // llvm::errs() << "\t Resulting Expression: " << constraintExpr.to_string() << "\n";

                auto constraintId = Util::findOrAddFormulaId(localManager, constraintExpr);

                // llvm::errs() << "\t Resulting ID: " << constraintId << "\n";

                // If either one or both operands of the icmp are not constants, we return an edge function that adds the constraint represented by the icmp instruction to the path condition.
                // This allows us to keep track of the constraint for later use in the analysis, without immediately evaluating it.
                return EF(std::in_place_type<FeasibilityAddConstrainEF>, localManager, constraintId);
            }
        }

        return EF(std::in_place_type<psr::EdgeIdentity<FeasibilityElement>>);
    }


    // Fallback, if the current instructions is not in our handling scope
    return EF(std::in_place_type<psr::EdgeIdentity<FeasibilityElement>>);
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
