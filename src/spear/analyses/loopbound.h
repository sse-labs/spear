
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
#include <phasar/PhasarLLVM/Domain/LLVMAnalysisDomain.h>

namespace loopbound {

struct LoopBoundDomain : psr::LLVMAnalysisDomainDefault {
  using d_t = const llvm::Value *;
  using l_t = psr::LatticeDomain<int64_t>;  // We use int64_t as our lattice type
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

  LoopBoundIDEAnalysis(const psr::LLVMProjectIRDB &IRDB,
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

  // Edge functions
  psr::EdgeFunction<l_t> getNormalEdgeFunction(
      n_t Curr, d_t CurrNode, n_t Succ, d_t SuccNode) override;

  psr::EdgeFunction<l_t> getCallEdgeFunction(
      n_t CallSite, d_t SrcNode,
      const llvm::Function *DestinationFunction,
      d_t DestNode) override;

  psr::EdgeFunction<l_t> getReturnEdgeFunction(
      n_t CallSite, const llvm::Function *Callee,
      n_t ExitStmt, d_t ExitNode,
      n_t RetSite, d_t RetNode) override;

  psr::EdgeFunction<l_t> getCallToRetEdgeFunction(
      n_t CallSite, d_t CallNode,
      n_t RetSite, d_t RetSiteNode,
      llvm::ArrayRef<f_t> Callees) override;

  psr::InitialSeeds<n_t, d_t, l_t> initialSeeds() override;

private:
  const psr::LLVMProjectIRDB *IRDBPtr = nullptr;
  std::vector<std::string> EntryPoints;
  std::unordered_map<const llvm::Function *, llvm::LoopInfo> LoopInfos;
  std::unordered_set<const llvm::PHINode *> InductionPHIs;
  std::unordered_map<const llvm::BasicBlock *, const llvm::PHINode *>
      HeaderToInduction;
};

} // namespace loopbound

#endif  // SRC_SPEAR_ANALYSES_LOOPBOUND_H_
