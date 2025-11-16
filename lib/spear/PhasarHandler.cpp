#include "PhasarHandler.h"

#include <vector>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>

PhasarHandler::PhasarHandler(llvm::Module *mod) {
    this->_entrypoints = {std::string("main")};
    //psr::HelperAnalyses HA(filename, EntryPoints);
    psr::HelperAnalyses HA(mod, this->_entrypoints);
    this->_HA = std::make_unique<psr::HelperAnalyses>(mod, this->_entrypoints);
    this->module = mod;
    this->_analysisResult = nullptr;
}

void PhasarHandler::runAnalysis() {
    if (this->_HA->getProjectIRDB().getFunctionDefinition("main")) {
        auto M = psr::createAnalysisProblem<psr::IDELinearConstantAnalysis>(*this->_HA, this->_entrypoints);
        // Alternative way of solving an IFDS/IDEProblem:
        auto result = psr::solveIDEProblem(M, this->_HA->getICFG());
        this->_analysisResult = std::make_unique<psr::OwningSolverResults<
            const llvm::Instruction *,
            const llvm::Value *,
            psr::LatticeDomain<long>
        >>(result);
    }
}

void PhasarHandler::dumpState() {
    this->_analysisResult->dumpResults(this->_HA->getICFG());
}

std::map<std::string, std::pair<const llvm::Value *, psr::IDELinearConstantAnalysisDomain::l_t>> PhasarHandler::queryBoundVars(llvm::Function * func) {
    std::map<std::string, std::pair<const llvm::Value *, psr::IDELinearConstantAnalysisDomain::l_t>> resultMap;

    for (const llvm::BasicBlock &BB : func->getBasicBlockList()) {
        for (const llvm::Instruction &inst : BB) {

            if (this->_analysisResult->containsNode(&inst)) {
                psr::LLVMAnalysisDomainDefault::d_t b = nullptr;
                auto res = this->_analysisResult->resultsAtInLLVMSSA(&inst, b);

                for (const auto &resElement : res) {
                    const llvm::Value *val = resElement.first;
                    const auto &domainValue = resElement.second;

                    std::string key = val->hasName()
                        ? val->getName().str()
                        : "<unnamed_" + std::to_string(reinterpret_cast<uintptr_t>(val)) + ">";

                    resultMap[key] = std::make_pair(val, domainValue);
                }
            }
        }
    }


    return resultMap;
}
