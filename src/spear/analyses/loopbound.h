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

namespace loopbound {

struct CounterFromIcmp {
    llvm::Value *CounterSide = nullptr;     // operator
    llvm::Value *InvariantSide = nullptr;   // bound
    std::vector<llvm::Value *> Roots;       // phi
};

class DeltaInterval {
public:
    enum class ValueType { TOP, BOTTOM, NORMAL };

    DeltaInterval();

    static DeltaInterval bottom();
    static DeltaInterval top();
    static DeltaInterval interval(int64_t low, int64_t high);

    bool isBottom() const;
    bool isTop() const;
    bool isNORMAL() const;

    int64_t getLowerBound() const;
    int64_t getUpperBound() const;

    // PhASAR expects "join" to be the semilattice join used by IDE values.
    // In your logs you treat it as meet (⊓). That is okay as long as you're
    // consistent across the solver; we keep your semantics.
    DeltaInterval join(const DeltaInterval &other) const;

    bool operator==(const DeltaInterval &other) const;
    bool operator!=(const DeltaInterval &other) const;

    DeltaInterval add(int64_t constant) const;

private:
    DeltaInterval(ValueType valuetype, int64_t low, int64_t high)
      : valueType(valuetype), lowerBound(low), upperBound(high) {}

    ValueType valueType;
    int64_t lowerBound;
    int64_t upperBound;
};

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                                 const DeltaInterval &DI) {
    if (DI.isBottom()) return OS << "⊥";
    if (DI.isTop()) return OS << "⊤";
    return OS << "[" << DI.getLowerBound() << ", " << DI.getUpperBound() << "]";
}

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

    explicit LoopBoundIDEAnalysis(const psr::LLVMProjectIRDB *IRDB,
                               std::vector<std::string> EntryPoints, std::vector<llvm::Loop*> *loops);

    // Seeds / lattice / all-top
    [[nodiscard]] psr::InitialSeeds<n_t, d_t, l_t> initialSeeds() override;

    l_t topElement() override;
    l_t bottomElement() override;
    l_t join(l_t Lhs, l_t Rhs) override;
    psr::EdgeFunction<l_t> allTopFunction() override;

    bool isZeroValue(d_t Fact) const noexcept override;

    // Flow functions
    FlowFunctionPtrType getNormalFlowFunction(n_t Curr, n_t Succ) override;
    FlowFunctionPtrType getCallFlowFunction(n_t CallSite, f_t Callee) override;
    FlowFunctionPtrType getRetFlowFunction(n_t CallSite, f_t Callee, n_t ExitStmt,
                                         n_t RetSite) override;
    FlowFunctionPtrType getCallToRetFlowFunction(n_t CallSite, n_t RetSite,
                                               llvm::ArrayRef<f_t> Callees) override;
    FlowFunctionPtrType getSummaryFlowFunction(n_t CallSite, f_t Callee) override;

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

private:
    const psr::LLVMProjectIRDB *IRDBPtr = nullptr;
    std::vector<std::string> EntryPoints;
    std::vector<llvm::Loop*> *loops;

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
    std::vector<llvm::Value *> sliceBackwards(llvm::Value *start, llvm::Loop *loop);

    bool phiHasIncomingValueFromLoop(llvm::PHINode *phi, llvm::Loop *loop);

    bool loadIsCarriedIn(llvm::LoadInst *inst, llvm::Loop *loop);

    bool isMemWrittenInLoop(llvm::LoadInst *inst, llvm::Loop *loop);

    bool ptrDependsOnLoopCariedPhi(llvm::Value *ptr, llvm::Loop *loop);

    bool isIrrelevantToLoop(llvm::Value *val, llvm::Loop *loop);
};

} // namespace loopbound

#endif // SRC_SPEAR_ANALYSES_LOOPBOUND_H_
