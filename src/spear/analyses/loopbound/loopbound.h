/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

/**
 * Main LoopBound Anaylsis component. Implements a Phasar based IDE analysis.
 * Uses Cullman and Martins approach for loop bound solving. See
 *
 * Christoph Cullmann and Florian Martin. Data-Flow Based Detection of Loop Bounds.
 * In 7th International Workshop on Worst-Case Execution Time Analysis (WCET'07).
 * Open Access Series in Informatics (OASIcs), Volume 6, pp. 1-6,
 * Schloss Dagstuhl – Leibniz-Zentrum für Informatik (2007)
 * https://doi.org/10.4230/OASIcs.WCET.2007.1193
 *
 * Calculates loop increment per iteration (alongside other loop properties) which we then can use
 * to calculate the amount of iterations the loop executes.
 *
 */

#ifndef SRC_SPEAR_ANALYSES_LOOPBOUND_H_
#define SRC_SPEAR_ANALYSES_LOOPBOUND_H_

#include <phasar/DataFlow/IfdsIde/IDETabulationProblem.h>
#include <phasar/DataFlow/IfdsIde/EdgeFunctions.h>
#include <phasar/PhasarLLVM/Domain/LLVMAnalysisDomain.h>
#include "phasar/PhasarLLVM/DB/LLVMProjectIRDB.h"

#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>

#include <set>
#include <string>
#include <vector>
#include <llvm/Analysis/LoopInfo.h>

#include "DeltaInterval.h"

namespace loopbound {

struct CounterFromIcmp {
    llvm::Value *CounterSide = nullptr;     // operator
    llvm::Value *InvariantSide = nullptr;   // Bound the counter is checked against
    std::vector<const llvm::Value *> Roots;       // Counter that we want to analyze
};

// TODO: Find a better name for this shitty internal representation
struct LoopDescription {
    llvm::Loop *loop;
    llvm::ICmpInst *icmp;
    const llvm::Value *counterRoot;
    llvm::Value *counterExpr;
    llvm::Value *limitExpr;
    std::optional<int64_t> init;
    std::optional<int64_t> step;
};

struct LoopBoundDomain : psr::LLVMAnalysisDomainDefault {
    using d_t = const llvm::Value *;
    using l_t = DeltaInterval;
    using i_t = psr::LLVMBasedICFG;
};

class LoopBoundIDEAnalysis final
    : public psr::IDETabulationProblem<LoopBoundDomain,
                                       std::set<const llvm::Value *>> {
public:
    using base_t =
      psr::IDETabulationProblem<LoopBoundDomain, std::set<const llvm::Value *>>;

    using n_t = typename base_t::n_t;
    using d_t = typename base_t::d_t;
    using l_t = typename base_t::l_t;
    using f_t = typename base_t::f_t;

    using container_t = std::set<d_t>;
    using FlowFunctionPtrType = typename base_t::FlowFunctionPtrType;
    using EdgeFunctionType = psr::EdgeFunction<l_t>;

    explicit LoopBoundIDEAnalysis(const psr::LLVMProjectIRDB *IRDB, std::vector<llvm::Loop*> *loops);

    // Seeds / lattice / all-top
    [[nodiscard]] psr::InitialSeeds<n_t, d_t, l_t> initialSeeds() override;

    l_t topElement() override;
    l_t bottomElement() override;
    l_t join(l_t Lhs, l_t Rhs) override;
    psr::EdgeFunction<l_t> allTopFunction() override;

    bool isLatchToHeaderEdge(n_t Curr, n_t Succ) const;

    bool isZeroValue(d_t Fact) const noexcept override;

    // Flow functions
    FlowFunctionPtrType getNormalFlowFunction(n_t Curr, n_t Succ) override;
    FlowFunctionPtrType getCallFlowFunction(n_t CallSite, f_t Callee) override;
    FlowFunctionPtrType getRetFlowFunction(n_t CallSite, f_t Callee, n_t ExitStmt,
                                         n_t RetSite) override;
    FlowFunctionPtrType getCallToRetFlowFunction(n_t CallSite, n_t RetSite,
                                               llvm::ArrayRef<f_t> Callees) override;
    FlowFunctionPtrType getSummaryFlowFunction(n_t CallSite, f_t Callee) override;

    const LoopDescription *getLoopDescriptionForInst(const llvm::Instruction *I) const;

    bool isCounterRootFactAtInst(d_t Fact, n_t AtInst) const;

    std::optional<int64_t> computeConstTripCount(const LoopDescription &LD) const;

    bool isExitingToExitEdge(n_t Curr, n_t Succ, const LoopDescription &LD) const;

    // Edge functions
    EdgeFunctionType getNormalEdgeFunction(n_t Curr, d_t CurrNode, n_t Succ,
                                         d_t SuccNode) override;
    EdgeFunctionType getCallEdgeFunction(n_t CallSite, d_t SrcNode, f_t DestFun,
                                       d_t DestNode) override;
    EdgeFunctionType getReturnEdgeFunction(n_t CallSite, f_t Callee, n_t ExitStmt,
                                         d_t ExitNode, n_t RetSite,
                                         d_t RetNode) override;
    EdgeFunctionType getCallToRetEdgeFunction(n_t CallSite, d_t CallNode,
                                            n_t RetSite, d_t RetSiteNode,
                                            llvm::ArrayRef<f_t> Callees) override;


    std::vector<LoopDescription> LoopDescriptions;

    static const llvm::Value *stripAddr(const llvm::Value *Ptr);

    std::vector<LoopDescription> getLoopDescriptions();

    static std::optional<int64_t> extractConstIncFromStore(const llvm::StoreInst *storeInst, const llvm::Value *counterRoot);

private:
    const psr::LLVMProjectIRDB *IRDBPtr = nullptr;
    std::vector<llvm::Loop*> *loops;

    llvm::DenseSet<const llvm::Value *> CounterRoots;
    llvm::DenseMap<const llvm::Loop *, llvm::DenseSet<const llvm::Value *>> CounterRootsPerLoop;

    void findLoopCounters();

    /**
     * Take the given ICMP inst and iterate from it backwards until
     * @param inst
     * @param loop /
     * @return
     */
    std::optional<CounterFromIcmp> findCounterFromICMP(llvm::ICmpInst *inst, llvm::Loop *loop);

    /**
     * Start at the given ICMP instruction and analyse the operands until we reach the corresponding
     * phi instruction to determine the loop counter.
     * @param start Instruction to start from
     * @param loop Loop the analysis runs in
     * @return Vector of roots corresponding to the start node
     */
    std::vector<const llvm::Value *> sliceBackwards(llvm::Value *start, llvm::Loop *loop);

    bool phiHasIncomingValueFromLoop(const llvm::PHINode *phi, llvm::Loop *loop);

    bool loadIsCarriedIn(const llvm::LoadInst *inst, llvm::Loop *loop);

    bool isMemWrittenInLoop(const llvm::LoadInst *inst, llvm::Loop *loop);

    bool ptrDependsOnLoopCariedPhi(const llvm::Value *ptr, llvm::Loop *loop);

    bool isIrrelevantToLoop(const llvm::Value *val, llvm::Loop *loop);

    static bool isStoredToInLoop(llvm::Value *Addr, llvm::Loop *L);

    static std::optional<int64_t> findConstStepForCell(const llvm::Value *Addr, llvm::Loop *L);

    /**
     * Find init value of given counter value
     * @param Addr Counter to analyze
     * @param loop Corresponding loop
     * @return Optional of the found value
     */
    std::optional<int64_t> findConstInitForCell(const llvm::Value *Addr, llvm::Loop *loop);

    void buildCounterRootIndex();

    static bool isLoadOfCounterRoot(llvm::Value *value, const llvm::Value *root);

    bool isCounterRootFact(d_t Fact);
};

} // namespace loopbound

#endif // SRC_SPEAR_ANALYSES_LOOPBOUND_H_
