#include "analyses/loopbound.h"

#include <phasar/PhasarLLVM/DataFlow/IfdsIde/LLVMZeroValue.h>
#include <phasar/PhasarLLVM/DB/LLVMProjectIRDB.h>

#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>

#include <memory>
#include <optional>

namespace loopbound {

namespace {

template <typename D, typename ContainerT>
class IdentityFlow final : public psr::FlowFunction<D, ContainerT> {
public:
  ContainerT computeTargets(D Src) override { return ContainerT{Src}; }
};

static LoopBoundIDEAnalysis::l_t initLattice() {
  return LoopBoundIDEAnalysis::l_t{};
}

} // namespace

LoopBoundIDEAnalysis::LoopBoundIDEAnalysis(
    const psr::LLVMProjectIRDB &IRDB,
    std::vector<std::string> EPs)
    : base_t(&IRDB,
             EPs,
             std::optional<d_t>(
                 static_cast<d_t>(psr::LLVMZeroValue::getInstance()))),
      IRDBPtr(&IRDB),
      EntryPoints(std::move(EPs)) {}

// ---------------- Flow functions ----------------

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getNormalFlowFunction(n_t Curr, n_t Succ) {

  // We need to define the flow here...

  /**
   * We mainly have to deal with multiple cases here
   *
   * 1) If an variable is allocated or loaded, we reserver a variable value on our scratchpad
   * 2) If we store a value to this variable we update our scratchpad
   * 3) If we calculate a value using add/sub/mul/div, we update our scratchpad
   */

  if (auto *icmpInst = llvm::dyn_cast<llvm::ICmpInst>(Curr)) {
    llvm::outs() << icmpInst << "\n";
  }

  return std::make_shared<IdentityFlow<d_t, container_t>>();
}

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getCallFlowFunction(n_t, const llvm::Function *) {
  return std::make_shared<IdentityFlow<d_t, container_t>>();
}

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getRetFlowFunction(n_t, const llvm::Function *, n_t, n_t) {
  return std::make_shared<IdentityFlow<d_t, container_t>>();
}

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getCallToRetFlowFunction(
    n_t, n_t, llvm::ArrayRef<f_t>) {
  return std::make_shared<IdentityFlow<d_t, container_t>>();
}

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getSummaryFlowFunction(n_t, const llvm::Function *) {
  return nullptr;
}

// ---------------- Edge functions ----------------

psr::EdgeFunction<LoopBoundIDEAnalysis::l_t>
LoopBoundIDEAnalysis::getNormalEdgeFunction(n_t, d_t, n_t, d_t) {
  return psr::EdgeIdentity<l_t>{};
}

psr::EdgeFunction<LoopBoundIDEAnalysis::l_t>
LoopBoundIDEAnalysis::getCallEdgeFunction(n_t, d_t, const llvm::Function *, d_t) {
  return psr::EdgeIdentity<l_t>{};
}

psr::EdgeFunction<LoopBoundIDEAnalysis::l_t>
LoopBoundIDEAnalysis::getReturnEdgeFunction(
    n_t, const llvm::Function *, n_t, d_t, n_t, d_t) {
  return psr::EdgeIdentity<l_t>{};
}

psr::EdgeFunction<LoopBoundIDEAnalysis::l_t>
LoopBoundIDEAnalysis::getCallToRetEdgeFunction(
    n_t, d_t, n_t, d_t, llvm::ArrayRef<f_t>) {
  return psr::EdgeIdentity<l_t>{};
}

// ---------------- Seeds ----------------

psr::InitialSeeds<
    LoopBoundIDEAnalysis::n_t,
    LoopBoundIDEAnalysis::d_t,
    LoopBoundIDEAnalysis::l_t>
LoopBoundIDEAnalysis::initialSeeds() {

  psr::InitialSeeds<n_t, d_t, l_t> Seeds;
  const d_t Z = static_cast<d_t>(psr::LLVMZeroValue::getInstance());
  const l_t Init = initLattice();

  for (const auto &Name : EntryPoints) {
    if (!IRDBPtr) continue;
    const llvm::Function *F = IRDBPtr->getFunctionDefinition(Name);
    if (!F || F->empty()) continue;
    Seeds.addSeed(&F->getEntryBlock().front(), Z, Init);
  }

  return Seeds;
}

} // namespace loopbound