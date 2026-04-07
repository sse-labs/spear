/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_PASSUTIL_H
#define SPEAR_PASSUTIL_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>

#include "HLAC/hlac.h"

#define SHOWTIMINGS true

namespace HLAC {
class GenericNode;
class hlac;
class FunctionNode;
} // namespace HLAC

class PassUtil {
public:
    // Convert a double value to scientific notation with configurable precision
    static std::string formatScientific(double value, int precision = 12);

    static void prepareFunctionsForLegacyAnalysis(
        llvm::Module &module,
        llvm::FunctionAnalysisManager &functionAnalysisManager);

    static void legacyWrapper(
        llvm::Module &module,
        llvm::FunctionAnalysisManager &functionAnalysisManager);

    static void collectCallNodeBindingsFromNestedNodes(
        HLAC::GenericNode *currentNode,
        std::size_t topLevelNodeIndex,
        std::vector<HLAC::CallNodeBinding> &callNodeBindings);

    static void cacheDirectNodeEnergies(
        HLAC::GenericNode *currentNode,
        std::unordered_map<HLAC::GenericNode *, double> &directNodeEnergyCache);

    static std::shared_ptr<HLAC::hlac> buildInitializedGraph(
        llvm::Module &module,
        llvm::FunctionAnalysisManager &functionAnalysisManager,
        ResultRegistry &resultRegistry);

    static void runMonolithicOnModule(
        llvm::Module &module,
        llvm::FunctionAnalysisManager &functionAnalysisManager,
        ResultRegistry &resultRegistry);

    static void runClusteredOnModule(
        llvm::Module &module,
        llvm::FunctionAnalysisManager &functionAnalysisManager,
        ResultRegistry &resultRegistry);

    static void runComparisonAnalysesOnClonedModules(
        llvm::Module &module,
        llvm::ModuleAnalysisManager &moduleAnalysisManager,
        ResultRegistry &baseRegistry);
};

#endif // SPEAR_PASSUTIL_H