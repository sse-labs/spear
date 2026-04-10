/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <llvm/Analysis/LazyCallGraph.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Transforms/Scalar/LoopPassManager.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/InstructionNamer.h>
#include <llvm/Transforms/Utils/Mem2Reg.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>

#include "ClusteredAnalysis.h"
#include "FunctionTree.h"
#include "HLAC/hlacwrapper.h"
#include "HLAC/util.h"
#include "LegacyAnalysis.h"
#include "MonolithicAnalysis.h"
#include "PassUtil.h"

std::string PassUtil::formatScientific(double value, int precision) {
    std::ostringstream outputStream;
    outputStream << std::scientific << std::setprecision(precision) << value;
    return outputStream.str();
}

void PassUtil::prepareFunctionsForLegacyAnalysis(
    llvm::Module &module,
    llvm::FunctionAnalysisManager &functionAnalysisManager) {

    llvm::FunctionPassManager functionPassManager;

    // Give unnamed instructions stable names for debugging
    functionPassManager.addPass(llvm::InstructionNamerPass());

    // Promote allocas to SSA form
    functionPassManager.addPass(llvm::PromotePass());

    // Canonicalize loops so ScalarEvolution can reason about them better
    // functionPassManager.addPass(llvm::LoopSimplifyPass());

    // Keep loops in LCSSA form
    // functionPassManager.addPass(llvm::LCSSAPass());

    // Simplify induction variables
    llvm::LoopPassManager loopPassManager;
    // loopPassManager.addPass(llvm::IndVarSimplifyPass());

    functionPassManager.addPass(
        llvm::createFunctionToLoopPassAdaptor(std::move(loopPassManager)));

    for (llvm::Function &function : module) {
        if (function.isDeclaration()) {
            continue;
        }

        functionPassManager.run(function, functionAnalysisManager);
    }
}

json PassUtil::legacyWrapper(
    llvm::Module &module,
    llvm::FunctionAnalysisManager &functionAnalysisManager) {

    auto legacyPreparationStart = std::chrono::high_resolution_clock::now();

    prepareFunctionsForLegacyAnalysis(module, functionAnalysisManager);

    auto legacyPreparationEnd = std::chrono::high_resolution_clock::now();
    auto legacyPreparationTime = std::chrono::duration_cast<std::chrono::microseconds>(
        legacyPreparationEnd - legacyPreparationStart);

    /*Logger::getInstance().log(
        "Legacy IR preparation took: " + std::to_string(legacyPreparationTime.count()) + " µs",
        LOGLEVEL::INFO);*/

    // Construct the functionTrees to the functions of the module
    FunctionTree *functionTree = nullptr;
    for (llvm::Function &function : module) {
        if (function.getName() == "main") {
            functionTree = FunctionTree::construct(&function);
            break;
        }
    }

    if (functionTree == nullptr) {
        throw std::runtime_error("Could not construct FunctionTree: main function not found.");
    }

    return LegacyAnalysis::run(functionAnalysisManager, functionTree, SHOWTIMINGS);
}

void PassUtil::collectCallNodeBindingsFromNestedNodes(
    HLAC::GenericNode *currentNode,
    std::size_t topLevelNodeIndex,
    std::vector<HLAC::CallNodeBinding> &callNodeBindings) {

    if (currentNode->nodeType == HLAC::NodeType::CALLNODE) {
        auto *callNode = static_cast<HLAC::CallNode *>(currentNode);

        HLAC::CallNodeBinding binding;
        binding.nodeIndex = topLevelNodeIndex;
        binding.calleeName = callNode->calledFunction->getName().str();
        binding.reference = callNode;

        callNodeBindings.push_back(binding);

        return;
    }

    if (currentNode->nodeType == HLAC::NodeType::LOOPNODE) {
        auto *loopNode = static_cast<HLAC::LoopNode *>(currentNode);

        for (auto &nestedNode : loopNode->Nodes) {
            collectCallNodeBindingsFromNestedNodes(
                nestedNode.get(),
                topLevelNodeIndex,
                callNodeBindings);
        }
    }
}

void PassUtil::cacheDirectNodeEnergies(
    HLAC::GenericNode *currentNode,
    std::unordered_map<HLAC::GenericNode *, double> &directNodeEnergyCache) {

    // Call node energies are updated later once callee energies are known.
    // Do not cache them here to avoid storing stale values.
    if (currentNode->nodeType == HLAC::NodeType::CALLNODE) {
        return;
    }

    // Cache the direct energy of the current node itself
    directNodeEnergyCache[currentNode] = currentNode->getEnergy();

    // Recurse into nested loop contents
    if (currentNode->nodeType == HLAC::NodeType::LOOPNODE) {
        auto *loopNode = static_cast<HLAC::LoopNode *>(currentNode);

        for (auto &nestedNode : loopNode->Nodes) {
            cacheDirectNodeEnergies(nestedNode.get(), directNodeEnergyCache);
        }
    }
}

std::shared_ptr<HLAC::hlac> PassUtil::buildInitializedGraph(
    llvm::Module &module,
    llvm::FunctionAnalysisManager &functionAnalysisManager,
    ResultRegistry &resultRegistry) {

    auto postOrderFunctionList = HLAC::Util::getLazyCallGraphPostOrder(module, functionAnalysisManager);

    auto getTargetLibraryInfo = [&functionAnalysisManager](llvm::Function &function) -> llvm::TargetLibraryInfo & {
        return functionAnalysisManager.getResult<llvm::TargetLibraryAnalysis>(function);
    };

    llvm::LazyCallGraph lazyCallGraph(module, getTargetLibraryInfo);
    lazyCallGraph.buildRefSCCs();

    std::unique_ptr<HLAC::hlac> graph = HLAC::HLACWrapper::makeHLAC(resultRegistry, lazyCallGraph);
    std::shared_ptr<HLAC::hlac> sharedGraph = std::move(graph);

    auto startConstruction = std::chrono::high_resolution_clock::now();

    for (auto *function : postOrderFunctionList) {
        sharedGraph->makeFunction(function, &functionAnalysisManager);
    }

    auto endConstruction = std::chrono::high_resolution_clock::now();
    auto constructionTime = std::chrono::duration_cast<std::chrono::microseconds>(
        endConstruction - startConstruction);

    /*Logger::getInstance().log(
        "HLAC construction took: " + std::to_string(constructionTime.count()) + " µs",
        LOGLEVEL::INFO);*/

    auto dotWritingStart = std::chrono::high_resolution_clock::now();
    sharedGraph->printDotRepresentation();
    auto dotWritingEnd = std::chrono::high_resolution_clock::now();

    auto dotTime = std::chrono::duration_cast<std::chrono::microseconds>(dotWritingEnd - dotWritingStart);
    /*Logger::getInstance().log(
        "DOT writing took: " + std::to_string(dotTime.count()) + " µs",
        LOGLEVEL::INFO);*/

    // Iterate over the function nodes
    for (auto &functionNode : sharedGraph->functions) {
        auto &sortedNodeList = functionNode->topologicalSortedRepresentationOfNodes;

        functionNode->baseNodeEnergy.resize(sortedNodeList.size());
        functionNode->nodeEnergy.resize(sortedNodeList.size());

        std::fill(functionNode->baseNodeEnergy.begin(), functionNode->baseNodeEnergy.end(), 0.0);
        std::fill(functionNode->nodeEnergy.begin(), functionNode->nodeEnergy.end(), 0.0);

        // Reset the direct per-node energy cache
        functionNode->directNodeEnergyCache.clear();

        // Init the vector to store call node references
        functionNode->callNodeBindings.clear();

        for (std::size_t index = 0; index < sortedNodeList.size(); ++index) {
            HLAC::GenericNode *currentNode = sortedNodeList[index];

            if (currentNode->nodeType == HLAC::NodeType::CALLNODE
                || currentNode->nodeType == HLAC::NodeType::LOOPNODE) {
                // Collect call nodes, including call nodes nested inside loop nodes
                collectCallNodeBindingsFromNestedNodes(currentNode, index, functionNode->callNodeBindings);
                continue;
            }

            // Always cache the base energy of the current top-level node
            functionNode->baseNodeEnergy[index] = currentNode->getEnergy();

            // Cache direct energies for this node and all nested non-call nodes
            cacheDirectNodeEnergies(currentNode, functionNode->directNodeEnergyCache);
        }

        // Update the node_index to energy mapping
        functionNode->nodeEnergy = functionNode->baseNodeEnergy;
        functionNode->baseNodeEnergyInitialized = true;
        functionNode->directNodeEnergyCacheInitialized = true;
    }

    return sharedGraph;
}

json PassUtil::runMonolithicOnModule(
    llvm::Module &module,
    llvm::FunctionAnalysisManager &functionAnalysisManager,
    ResultRegistry &resultRegistry) {

    std::shared_ptr<HLAC::hlac> graph = buildInitializedGraph(
        module,
        functionAnalysisManager,
        resultRegistry);

    return MonolithicAnalysis::run(graph, SHOWTIMINGS);
}

json PassUtil::runClusteredOnModule(
    llvm::Module &module,
    llvm::FunctionAnalysisManager &functionAnalysisManager,
    ResultRegistry &resultRegistry) {

    std::shared_ptr<HLAC::hlac> graph = buildInitializedGraph(
        module,
        functionAnalysisManager,
        resultRegistry);

    return ClusteredAnalysis::run(graph, SHOWTIMINGS);
}

std::unordered_map<std::string, nlohmann::json> PassUtil::runComparisonAnalysesOnClonedModules(
    llvm::Module &module,
    llvm::ModuleAnalysisManager &moduleAnalysisManager,
    ResultRegistry &baseRegistry) {
    // Clone the module once per analysis so IR mutations stay isolated
    std::unique_ptr<llvm::Module> legacyModule = llvm::CloneModule(module);
    std::unique_ptr<llvm::Module> monolithicModule = llvm::CloneModule(module);
    std::unique_ptr<llvm::Module> clusteredModule = llvm::CloneModule(module);

    // Use separate registries so analysis-local state cannot leak between runs
    ResultRegistry legacyRegistry = baseRegistry;
    ResultRegistry monolithicRegistry = baseRegistry;
    ResultRegistry clusteredRegistry = baseRegistry;

    // Reuse the module analysis manager infrastructure for the cloned modules
    auto &legacyFunctionAnalysisManager =
        moduleAnalysisManager.getResult<llvm::FunctionAnalysisManagerModuleProxy>(*legacyModule).getManager();

    auto &monolithicFunctionAnalysisManager =
        moduleAnalysisManager.getResult<llvm::FunctionAnalysisManagerModuleProxy>(*monolithicModule).getManager();

    auto &clusteredFunctionAnalysisManager =
        moduleAnalysisManager.getResult<llvm::FunctionAnalysisManagerModuleProxy>(*clusteredModule).getManager();

    std::unordered_map<std::string, nlohmann::json> output = {};

    auto legacyOutput = legacyWrapper(*legacyModule, legacyFunctionAnalysisManager);
    auto monolithicOutput = runMonolithicOnModule(*monolithicModule, monolithicFunctionAnalysisManager, monolithicRegistry);
    auto clusteredOutput = runClusteredOnModule(*clusteredModule, clusteredFunctionAnalysisManager, clusteredRegistry);

    output = {
        {"legacy", legacyOutput},
        {"monolithic", monolithicOutput},
        {"clustered", clusteredOutput}
    };

    return output;
}

nlohmann::json PassUtil::appendGraphContent(nlohmann::json &baseOutput, HLAC::GenericNode *node) {
    if (node->nodeType == HLAC::NodeType::CALLNODE) {
        auto *callNode = static_cast<HLAC::CallNode *>(node);

        const std::string nodeName = callNode->name;
        const std::string calleeName = callNode->calledFunction->getName().str();

        bool nodeAlreadyExists = false;

        if (!baseOutput.contains("nodes") || !baseOutput["nodes"].is_array()) {
            baseOutput["nodes"] = nlohmann::json::array();
        }

        // Check for existing node with same identifying properties
        for (const auto &existingNode : baseOutput["nodes"]) {
            if (existingNode.contains("name") && existingNode.contains("callee")) {
                if (existingNode["name"] == nodeName &&
                    existingNode["callee"] == calleeName) {
                    nodeAlreadyExists = true;
                    break;
                    }
            }
        }

        // Only insert if not already present
        if (!nodeAlreadyExists) {
            nlohmann::json callNodeJson = {
                {"type", "call"},
                {"name", nodeName},
                {"callee", calleeName},
                {"energy", callNode->getEnergy()}
            };

            baseOutput["nodes"].push_back(callNodeJson);
        }
    }

    if (node->nodeType == HLAC::NodeType::LOOPNODE) {
        auto *loopNode = static_cast<HLAC::LoopNode *>(node);
        auto repetitionsArray = nlohmann::json::array();
        repetitionsArray.push_back(loopNode->bounds.getLowerBound());
        repetitionsArray.push_back(loopNode->bounds.getUpperBound());

        nlohmann::json loopNodeJson = {
            {"type", "loop"},
            {"name", loopNode->loop->getName().str()},
            {"energy", loopNode->getEnergy()},
            {"repetitions", repetitionsArray}
        };

        for (auto &nestedNode : loopNode->Nodes) {
            appendGraphContent(loopNodeJson, nestedNode.get());
        }

        baseOutput["nodes"].push_back(loopNodeJson);
    }

    if (node->nodeType == HLAC::NodeType::NODE) {
        auto *normalnode = static_cast<HLAC::Node *>(node);

        nlohmann::json normalNodeJson = {
            {"type", "node"},
            {"name", normalnode->name},
            {"energy", normalnode->getEnergy()},
        };

        baseOutput["nodes"].push_back(normalNodeJson);
    }

    return baseOutput;
}

nlohmann::json PassUtil::appendGraphContentLegacy(LLVMHandler handler, nlohmann::json &baseOutput, Node *node) {
    if (auto loopNode = dynamic_cast<LoopNode *>(node)) {
        auto repetitionsArray = nlohmann::json::array();
        repetitionsArray.push_back(loopNode->loopTree->iterations);

        nlohmann::json loopNodeJson = {
            {"type", "loop"},
            {"name", loopNode->loopTree->mainloop->getName().str()},
            {"energy", loopNode->getNodeEnergy(&handler)},
            {"repetitions", repetitionsArray}
        };

        for (auto &nestedGraphs : loopNode->subgraphs) {
            for (auto &nestedNode : nestedGraphs->getNodes()) {
                appendGraphContentLegacy(handler, loopNodeJson, nestedNode);
            }
        }

        baseOutput["nodes"].push_back(loopNodeJson);
        return baseOutput;
    }

    nlohmann::json normalNodeJson = {
        {"type", "node"},
        {"name", node->block->getName().str()},
        {"energy", node->energy},
    };

    baseOutput["nodes"].push_back(normalNodeJson);
    return baseOutput;
}

// Extract filename without extension from a path
std::string PassUtil::extractFileNameWithoutExtension(const std::string& filePath) {
    std::filesystem::path pathObject(filePath);

    // stem() returns filename without extension
    return pathObject.stem().string();
}
