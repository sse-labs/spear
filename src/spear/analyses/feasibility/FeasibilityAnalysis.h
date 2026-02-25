/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_FEASIBILITYANALYSIS_H
#define SPEAR_FEASIBILITYANALYSIS_H

#include <phasar/PhasarLLVM/ControlFlow/LLVMBasedICFG.h>
#include <phasar/PhasarLLVM/DB/LLVMProjectIRDB.h>
#include <phasar/PhasarLLVM/Domain/LLVMAnalysisDomain.h>

#include <phasar/DataFlow/IfdsIde/IDETabulationProblem.h>
#include <phasar/DataFlow/IfdsIde/FlowFunctions.h>
#include "analyses/feasibility/FeasibilityAnalysisManager.h"

#include <llvm/Analysis/LoopInfo.h>

#include "FeasibilityElement.h"

#include <memory>


namespace Feasibility {

/**
 * Domain definition of the LoopBoundAnalysis
 */
struct FeasibilityDomain : psr::LLVMAnalysisDomainDefault {
    using d_t = const llvm::Value *;  // Flow-Fact -> In our case the loop counter root
    using l_t = FeasibilityElement;  // Latice Element -> Our DeltaInterval to track increments
    using i_t = psr::LLVMBasedICFG;  // Control flow type -> Here we operate on the ICFG
};

class FeasibilityAnalysis final  : public psr::IDETabulationProblem<FeasibilityDomain, std::set<const llvm::Value *>> {
public:
    /**
    * Derive types of the analysis from the base class definition for usage inside the class.
    */
    using base_t = psr::IDETabulationProblem<FeasibilityDomain, std::set<const llvm::Value *>>;
    using n_t = typename base_t::n_t;
    using d_t = typename base_t::d_t;
    using l_t = typename base_t::l_t;
    using f_t = typename base_t::f_t;

    /**
     * Define function pointer types
     */
    using container_t = std::set<d_t>;
    using FlowFunctionPtrType = typename base_t::FlowFunctionPtrType;
    using EdgeFunctionType = psr::EdgeFunction<l_t>;

    // New implementation uses a state store (owns z3::context and persistent state).
    std::unique_ptr<FeasibilityAnalysisManager> manager;

    explicit FeasibilityAnalysis(llvm::FunctionAnalysisManager *FAM, const psr::LLVMProjectIRDB *IRDB, const psr::LLVMBasedICFG *ICFG);

    bool isZeroValue(d_t Fact) const noexcept override;

    /**
     * Normal edge function
     *
     * Performs the detection of the relevant instructions for our analysis.
     *
     * Emits a DeltaIntervalIdentity for every case except if we encounter a store instruction.
     * In this case a DeltaIntervalCollect is emitted.
     *
     * @param Curr Current node
     * @param CurrNode Fact of the current node
     * @param Succ Next node
     * @param SuccNode Fact of the next node
     * @return EdgeFunction with either DeltaIntervalIdentity or DeltaIntervalCollect
     */
    EdgeFunctionType getNormalEdgeFunction(n_t Curr, d_t CurrNode, n_t Succ, d_t SuccNode) override;

private:
    [[nodiscard]] psr::InitialSeeds<n_t, d_t, l_t> initialSeeds() override;

    [[nodiscard]] d_t zeroValue() const;

    l_t topElement() override;

    l_t bottomElement() override;

    l_t emptyElement();

    l_t join(l_t Lhs, l_t Rhs) override;

    const psr::LLVMBasedICFG *ICFG = nullptr;

    psr::EdgeFunction<l_t> allTopFunction() override;

    /**
     * Normal flow function.
     * Propagates only the ZeroValue to keep the analysis strictly scoped.
     *
     * @param Curr Current node
     * @param Succ Next node
     * @return Returns the IdentityFlow
     */
    FlowFunctionPtrType getNormalFlowFunction(n_t Curr, n_t Succ) override;

    /**
     * Call flow function.
     * Just returns the IdentityFlow, as the LoopBound analysis is entirely intraprocedual.
     *
     * @param CallSite Current node
     * @param Callee Next node
     * @return Returns the IdentityFlow
     */
    FlowFunctionPtrType getCallFlowFunction(n_t CallSite, f_t Callee) override;

    /**
     * Ret flow function.
     * Just returns the IdentityFlow, as the LoopBound analysis is entirely intraprocedual.
     *
     * @param CallSite Current node
     * @param Callee Next node
     * @param ExitStmt Exit node
     * @param RetSite Return node
     * @return Returns the IdentityFlow
     */
    FlowFunctionPtrType getRetFlowFunction(n_t CallSite, f_t Callee, n_t ExitStmt, n_t RetSite) override;

    /**
     * CallRet flow function
     * Just returns the KeepLocalOnCallToRet, as the LoopBound analysis is entirely intraprocedual.
     *
     * @param CallSite Call node
     * @param RetSite Return node
     * @param Callees Called functions
     * @return Return the KeepLocalOnCallToRet flow
     */
    FlowFunctionPtrType getCallToRetFlowFunction(n_t CallSite, n_t RetSite, llvm::ArrayRef<f_t> Callees) override;

    /**
     * Call edge function
     *
     * Unused, as analysis is intraprocedual.
     *
     * @param CallSite Node of the call
     * @param SrcNode Fact at the call site
     * @param DestFun Called function
     * @param DestNode Fact at the site of the called function
     * @return Returns an edge function with DeltaIntervalIdentity
     */
    EdgeFunctionType getCallEdgeFunction(n_t CallSite, d_t SrcNode, f_t DestFun, d_t DestNode) override;

    /**
     * Return edge function
     *
     * Unused, as analysis is intraprocedual.
     *
     * @param CallSite Node of the call
     * @param Callee Callee function
     * @param ExitStmt
     * @param ExitNode
     * @param RetSite
     * @param RetNode
     * @return Returns an edge function with DeltaIntervalIdentity
     */
    EdgeFunctionType getReturnEdgeFunction(n_t CallSite, f_t Callee, n_t ExitStmt, d_t ExitNode, n_t RetSite,
                                          d_t RetNode) override;

    /**
     * Call to ret edge function
     *
     * Unused, as analysis is intraprocedual.
     *
     * @param CallSite Node of the call
     * @param CallNode Fact of the call
     * @param RetSite
     * @param RetSiteNode
     * @param Callees
     * @return Returns an edge function with DeltaIntervalIdentity
     */
    EdgeFunctionType getCallToRetEdgeFunction(n_t CallSite, d_t CallNode,
                                             n_t RetSite, d_t RetSiteNode,
                                             llvm::ArrayRef<f_t> Callees) override;
};

} // namespace Feasibility

#endif //SPEAR_FEASIBILITYANALYSIS_H

