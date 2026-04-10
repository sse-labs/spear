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

#include <llvm/IR/PassManager.h>

#include "HLAC/hlac.h"
#include "ProgramGraph.h"

#define SHOWTIMINGS true

namespace HLAC {
class GenericNode;
class hlac;
class FunctionNode;
}  // namespace HLAC

class PassUtil {
 public:
    /**
     * Convert the given double value to a string with fixed precision
     * @param value Value to convert
     * @param precision Precision of the value
     * @return String represnting the given double value with the specified precision
     */
    static std::string formatScientific(double value, int precision = 12);

    /**
     * Execute the necessary passes of the legacy analysis on the given module
     * @param module Module to run the passes on
     * @param functionAnalysisManager Underlying FunctionAnalysisManager to execute the passes with
     */
    static void prepareFunctionsForLegacyAnalysis(llvm::Module &module,
                                                  llvm::FunctionAnalysisManager &functionAnalysisManager);

    /**
     * Calculate only legacy analysis relevant analysis steps and execute the legacy analysis afterward
     * @param module Module to run the steps on
     * @param functionAnalysisManager FAM to base the underlying calculation on
     * @return JSON object with the result of the legacy analysis
     */
    static nlohmann::json legacyWrapper(llvm::Module &module, llvm::FunctionAnalysisManager &functionAnalysisManager);

    /**
     * Collect call CallNodeBindings from nodes contained in the given node.
     * This function is used to resolve callnodes after they have been analyzed by the respective analysis.
     * @param currentNode Node to base the collection on
     * @param topLevelNodeIndex Index of the node we are executing the analysis on
     * @param callNodeBindings Vector where we store the found CallNodeBindings
     */
    static void collectCallNodeBindingsFromNestedNodes(HLAC::GenericNode *currentNode, std::size_t topLevelNodeIndex,
                                                       std::vector<HLAC::CallNodeBinding> &callNodeBindings);

    /**
     * Cache already calculated node energy values to mitigate recalculation of nodes down the line
     * @param currentNode Node to cache energy for
     * @param directNodeEnergyCache Storage of the cached energy
     */
    static void cacheDirectNodeEnergies(HLAC::GenericNode *currentNode,
                                        std::unordered_map<HLAC::GenericNode *, double> &directNodeEnergyCache);

    /**
     * Construct edited HLAC that can be used for monolithic or clustered analysis
     * @param module Module the HLAC is based uppon
     * @param functionAnalysisManager FAM to base all calculations on
     * @param resultRegistry PHASAR result registry to use phasar based analyses
     * @return Edited HLAC ready for analysis
     */
    static std::shared_ptr<HLAC::hlac> buildInitializedGraph(llvm::Module &module,
                                                             llvm::FunctionAnalysisManager &functionAnalysisManager,
                                                             ResultRegistry &resultRegistry);

    /**
     * Execute the Monolithic IPET analysis on the given module
     * @param module Module to run the analysis on
     * @param functionAnalysisManager FAM used for calculations
     * @param resultRegistry PHASAR result registry to use phasar based analyses
     * @return JSON object with the analysis graph
     */
    static nlohmann::json runMonolithicOnModule(llvm::Module &module,
                                                llvm::FunctionAnalysisManager &functionAnalysisManager,
                                                ResultRegistry &resultRegistry);

    /**
     * Execute the Clustered IPET analysis on the given module
     * @param module Module to run the analysis on
     * @param functionAnalysisManager FAM used for calculations
     * @param resultRegistry PHASAR result registry to use phasar based analyses
     * @return JSON object with the analysis graph
     */
    static nlohmann::json runClusteredOnModule(llvm::Module &module,
                                               llvm::FunctionAnalysisManager &functionAnalysisManager,
                                               ResultRegistry &resultRegistry);

    /**
     * Execute Monolithic, Clustered and the legacy analysis on the given module
     * @param module Module to run the analysis on
     * @param moduleAnalysisManager MAM used for calculations
     * @param resultRegistry PHASAR result registry to use phasar based analyses
     * @return Mapping from analysis name to generated JSON output
     */
    static std::unordered_map<std::string, nlohmann::json>
    runComparisonAnalysesOnClonedModules(llvm::Module &module, llvm::ModuleAnalysisManager &moduleAnalysisManager,
                                         ResultRegistry &resultRegistry);


    /**
     * Recursive function to append node energy content to the given json object
     * @param baseOutput JSON object to append to
     * @param node Node to analyse for inner content
     * @return JSON output enriched with inner node content
     */
    static nlohmann::json appendGraphContent(nlohmann::json &baseOutput, HLAC::GenericNode *node);

    /**
     * Recursive function to append node energy content to the given json object
     * @param baseOutput JSON object to append to
     * @param node Node to analyse for inner content
     * @param loopresult Result of the previous executed ILP clustered loop analysis to enrich loopnode energy with
     * vaues
     * @return JSON output enriched with inner node content
     */
    static nlohmann::json appendGraphContent(nlohmann::json &baseOutput, HLAC::GenericNode *node,
                                             ILPClusteredLoopResult loopresult);

    /**
     * Recursive function to append node energy content to the given json object based on the legacy programgraph
     * @param handler LLVMHandler to deal with LLVM IR code
     * @param baseOutput JSON object to append to
     * @param node Node to analyse for inner content
     * @return JSON output enriched with inner node content
     */
    static nlohmann::json appendGraphContentLegacy(LLVMHandler handler, nlohmann::json &baseOutput, Node *node);

    /**
     * Extract the filename from the given file path
     * @param filePath Path to analyse
     * @return Filename if it can be extracted
     */
    static std::string extractFileNameWithoutExtension(const std::string &filePath);
};

#endif  // SPEAR_PASSUTIL_H
