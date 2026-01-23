
#ifndef SRC_SPEAR_ANALYSES_LOOPBOUND_H_
#define SRC_SPEAR_ANALYSES_LOOPBOUND_H_

#include <phasar/DataFlow/IfdsIde/IDETabulationProblem.h>

#include <llvm/IR/Value.h>
#include <llvm/IR/Instruction.h>

#include <set>
#include <string>
#include <unordered_set>
#include <vector>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/Analysis/LoopInfo.h>

#include <phasar/Domain/LatticeDomain.h>
#include <phasar/PhasarLLVM/DataFlow/IfdsIde/LLVMZeroValue.h>
#include <phasar/PhasarLLVM/Domain/LLVMAnalysisDomain.h>
#include <phasar/PhasarLLVM/DB/LLVMProjectIRDB.h>

namespace loopbound {

class DeltaInterval {
public:
    enum class ValueType {
        TOP,     // Unbound
        BOTTOM,  // Unreachable
        NORMAL   // Normal
    };

    DeltaInterval();

    static DeltaInterval bottom();
    static DeltaInterval top();
    static DeltaInterval constant();
    static DeltaInterval interval(int64_t low, int64_t high);

    bool isBottom() const;
    bool isTop() const;
    bool isNORMAL() const;

    int64_t getLowerBound() const;
    int64_t getUpperBound() const;

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

// Print to LLVM raw_ostream (most useful in LLVM/PhASAR)
inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                                     const DeltaInterval &DI) {
    if (DI.isBottom()) {
        OS << "⊥";
        return OS;
    }
    if (DI.isTop()) {
        OS << "⊤";
        return OS;
    }
    OS << "[" << DI.getLowerBound() << ", " << DI.getUpperBound() << "]";
    return OS;
}

// Optional: ADL to_string overload
inline std::string to_string(const DeltaInterval &DI) {
    std::string S;
    llvm::raw_string_ostream RSO(S);
    RSO << DI;
    return S;
}

struct LoopBoundDomain : psr::LLVMAnalysisDomainDefault {
  using d_t = const llvm::Value *;
  using l_t = DeltaInterval;  // We use int64_t as our lattice type
};

/**
 * Our analysis to find loop bounds using IDE
 */
class LoopBoundIDEAnalysis final : public psr::IDETabulationProblem<LoopBoundDomain> {
public:
  using base_t = psr::IDETabulationProblem<LoopBoundDomain>;
  using n_t = typename base_t::n_t;
  using d_t = typename base_t::d_t;
  using l_t = typename base_t::l_t;
  using f_t = typename base_t::f_t;

  using FlowFunctionPtrType = typename base_t::FlowFunctionPtrType;
  using container_t = std::set<d_t>;

  // NEW: convenient alias for your new edge-function type
  using EdgeFunctionType = psr::EdgeFunction<l_t>;  // <-- NEW (use in .cpp if you want)

    LoopBoundIDEAnalysis(const psr::LLVMProjectIRDB &IRDB,
                     const typename LoopBoundDomain::c_t &CFG,
                     std::vector<std::string> EntryPoints);

  // Flow functions
  FlowFunctionPtrType getNormalFlowFunction(n_t Curr, n_t Succ) override;
  FlowFunctionPtrType getCallFlowFunction(n_t CallSite,
                                          const llvm::Function *Callee) override;
  FlowFunctionPtrType getRetFlowFunction(n_t CallSite,
                                         const llvm::Function *Callee,
                                         n_t ExitStmt,
                                         n_t RetSite) override;

  FlowFunctionPtrType getCallToRetFlowFunction(
      n_t CallSite, n_t RetSite,
      llvm::ArrayRef<f_t> Callees) override;

  FlowFunctionPtrType getSummaryFlowFunction(
      n_t CallSite, const llvm::Function *Callee) override;

  // Edge functions (already correct for your PhASAR EdgeFunction.h)
  EdgeFunctionType getNormalEdgeFunction(
      n_t Curr, d_t CurrNode, n_t Succ, d_t SuccNode) override;

  EdgeFunctionType getCallEdgeFunction(
      n_t CallSite, d_t SrcNode,
      const llvm::Function *DestinationFunction,
      d_t DestNode) override;

  EdgeFunctionType getReturnEdgeFunction(
      n_t CallSite, const llvm::Function *Callee,
      n_t ExitStmt, d_t ExitNode,
      n_t RetSite, d_t RetNode) override;

  EdgeFunctionType getCallToRetEdgeFunction(
      n_t CallSite, d_t CallNode,
      n_t RetSite, d_t RetSiteNode,
      llvm::ArrayRef<f_t> Callees) override;


    psr::InitialSeeds<n_t, d_t, l_t> initialSeeds() override;

    l_t topElement() override;
    l_t bottomElement() override;
    l_t join(l_t Lhs, l_t Rhs) override;
    psr::EdgeFunction<l_t> allTopFunction() override;

    bool isZeroValue(d_t Fact) const noexcept override;

private:
  const psr::LLVMProjectIRDB *IRDBPtr = nullptr;
  std::vector<std::string> EntryPoints;
  std::unordered_map<const llvm::Function *, llvm::LoopInfo> LoopInfos;
  std::unordered_set<const llvm::PHINode *> InductionPHIs;
  std::unordered_map<const llvm::BasicBlock *, const llvm::PHINode *>
      HeaderToInduction;
    const typename LoopBoundDomain::c_t *CFGPtr = nullptr; // c_t is LLVMBasedCFG
};

} // namespace loopbound

#endif  // SRC_SPEAR_ANALYSES_LOOPBOUND_H_
