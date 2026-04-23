/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "HLAC/hlac.h"
#include <utility>
#include <fstream>
#include <string>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "HLAC/util.h"

#include "ILP/ILPBuilder.h"
#include "ILP/ILPClusterCache.h"
#include "ILP/ILPTypes.h"
#include "ILP/ILPUtil.h"

#include <CbcModel.hpp>
#include <OsiClpSolverInterface.hpp>

#include "Logger.h"

namespace HLAC {
void hlac::makeFunction(llvm::Function* function, llvm::FunctionAnalysisManager *fam) {
    auto fnptr = FunctionNode::makeNode(function, fam, registry, this);
    functions.emplace_back(std::move(fnptr));
}

void hlac::printDotRepresentation() {
    std::filesystem::create_directories("./dot");

    for (auto &fn : functions) {
        std::string filename = "./dot/" + fn->name + ".dot";
        std::ofstream out(filename);

        fn->printDotRepresentation(out);
    }
}

void hlac::printDotRepresentationWithSolution(FunctionNode *FN, std::vector<double> result, std::string appendName) {
    std::filesystem::create_directories("./dot");

    // Why the fuck do we run in a loop here? We write dot with different input results from other functions...
    // Remove this loop
    std::string filename = "./dot/" + FN->name + "_solution_" + appendName + ".dot";
    std::ofstream out(filename);

    FN->printDotRepresentationWithSolution(out, result);
}

void hlac::printDotRepresentationWithSolution(
    FunctionNode *FN,
    std::vector<Edge *> takenEdges,
    std::string appendName) {
    std::filesystem::create_directories("./dot");

    int maxIndex = Util::getMaxEdgeIndexInFunction(FN);
    if (maxIndex < 0) {
        maxIndex = 0;
    }

    std::vector<double> result(maxIndex + 1, 0.0);
    std::unordered_set<Edge *> takenSet;

    // Mark taken edges
    for (auto *edge : takenEdges) {
        if (edge) {
            takenSet.insert(edge);
        }
    }

    for (const auto &edgeUP : FN->Edges) {
        Edge *edge = edgeUP.get();
        if (!edge) {
            continue;
        }

        if (edge->ilpIndex < 0 || edge->ilpIndex >= static_cast<int>(result.size())) {
            continue;
        }

        if (takenSet.find(edge) != takenSet.end()) {
            result[edge->ilpIndex] = 1.0;
        }
    }

    for (const auto &nodeUP : FN->Nodes) {
        if (auto *loopNode = dynamic_cast<LoopNode *>(nodeUP.get())) {
            Util::markTakenEdgesInLoop(loopNode, takenSet, result);
        }
    }

    std::string filename = "./dot/" + FN->name + "_solution_" + appendName + ".dot";
    std::ofstream out(filename);

    FN->printDotRepresentationWithSolution(out, result);
}

std::map<std::string, double> hlac::getEnergy() {
    // assume we are executing in postorder!!!
    for (auto &functionNode : functions) {
        functionNode->getEnergy();
    }

    return FunctionEnergyCache;
}

double hlac::getEnergyPerFunction(std::string functionName, bool isRecursive) {
    if (FunctionEnergyCache.contains(functionName)) {
        return FunctionEnergyCache[functionName];
    }

    /**
     * We land here if we encounter a call to a function that has not been analyzed yet.
     * This can happen if we have recursive calls or if we call functions that are not defined in the program
     * (e.g., library functions).
     *
     * Currently both cases return a 0.0.
     * We could insert a fallback value here
     */

    if (isRecursive) {
        Logger::getInstance().log("Trying to get energy of " + functionName +
            " which is currently being analyzed! This is likely due to recursion. Returning 0.0 for this call.",
            LOGLEVEL::WARNING);
        return 0.0;
    } else {
        if (!Util::starts_with(functionName, "__psr")
            && !Util::starts_with(functionName, "__clang")) {
            Logger::getInstance().log("Trying to get energy of " + functionName +
            " which has not been analyzed yet!", LOGLEVEL::ERROR);
        }
        return 0.0;
    }
}

std::optional<ILPModel> hlac::buildMonolithicILP(FunctionNode *functionNode) {
    // ignore phasar hooks
    if (!Util::starts_with(functionNode->function->getName().str(), "__psr")
        && !Util::starts_with(functionNode->function->getName().str(), "__clang")) {
        auto ilpModel = ILPBuilder::buildMonolithicILP(functionNode);

        return ilpModel;
    }

    return std::nullopt;
}

std::optional<ClusteredILPModel> hlac::buildClusteredILPS(FunctionNode *functionNode) {
    // ignore phasar hooks
    if (!Util::starts_with(functionNode->function->getName().str(), "__psr")
        && !Util::starts_with(functionNode->function->getName().str(), "__clang")) {
        auto ilpModelMapping = ILPBuilder::buildClusteredILP(functionNode);

        return ilpModelMapping;
    } else {
        // std::cout << "Ignored ILP building for function " << functionNode->name << "\n";
    }

    return std::nullopt;
}


std::optional<ILPResult> hlac::solveMonolithicIlp(ILPModel &model, std::string fname) {
    auto solvedModel = ILPBuilder::solveModel(model);

    return solvedModel;
}

std::optional<DAGLongestPathSolution> hlac::DAGLongestPath(FunctionNode *functionNode,
    std::unordered_map<LoopNode *, ILPResult> clusteredResult) {
    if (!Util::starts_with(functionNode->function->getName().str(), "__psr")
            && !Util::starts_with(functionNode->function->getName().str(), "__clang")) {
        auto [distances, predecessors] =
            ILPUtil::longestPathDAG(functionNode, clusteredResult);

        auto funcNode = functionNode;

        // auto entryNode = funcNode->Nodes[funcNode->entryIndex];
        auto exitNode = funcNode->Nodes[funcNode->exitIndex].get();

        double exitEnergy = distances[exitNode];

        std::vector<Edge *> takenEdges = Util::findTakenEdges(exitNode, predecessors, funcNode->Edges, clusteredResult);

        return std::make_optional<DAGLongestPathSolution>({exitEnergy, takenEdges});
    }

    return std::nullopt;
}

ILPClusteredLoopResult hlac::solveClusteredIlps(ILPLoopModelMapping loopModelMapping) {
    ILPClusterCache &cache = ILPClusterCache::getInstance();

    // We need to solve the ILP for each loop and then combine the results to get the overall energy and path
    // for the function
    std::unordered_map<LoopNode *, ILPResult> loopEnergyMapping;
    loopEnergyMapping.reserve(loopModelMapping.size());

    // std::cout << "Clustered results for " << name << ":\n";

    for (const auto &[loopNode, model] : loopModelMapping) {
        if (cache.entryExists(loopNode->hash)) {
            auto cachedResult = cache.getEntry(loopNode->hash);
            if (cachedResult.has_value()) {
                loopEnergyMapping.emplace(loopNode, cachedResult.value());
            }
        } else {
            auto solvedModel = ILPBuilder::solveModel(model);

            if (solvedModel.has_value()) {
                // std::cout << "Loop " << loopNode->loop->getName().str() << " -> " << objectiveValue << std::endl;
                cache.setEntry(loopNode->hash, solvedModel.value());
                loopEnergyMapping.emplace(loopNode, std::move(*solvedModel));
            }
        }
    }

    return loopEnergyMapping;
}

HLAC::FunctionNode * HLAC::hlac::getFunctionByName(std::string name) {
    for (auto &func : functions) {
        if (func->function->getName() == name) {
            return func.get();
        }
    }

    return nullptr;
}

}  // namespace HLAC
