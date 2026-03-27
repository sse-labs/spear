/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "HLAC/hlac.h"
#include <utility>
#include <fstream>
#include <string>
#include <map>

#include "CbcModel.hpp"
#include "HLAC/util.h"
#include "OsiClpSolverInterface.hpp"

#include "ILP/ILPBuilder.h"
#include "ILP/ILPClusterCache.h"
#include "ILP/ILPTypes.h"
#include "ILP/ILPUtil.h"

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

void hlac::printDotRepresentationWithSolution(FunctionNode *FN, std::vector<Edge *> takenEdges, std::string appendName) {
    std::filesystem::create_directories("./dot");

    /**
     * TODO:
     * Use clustered result of clustered analysis that yields mapping ilpindex
     *
     */

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

double hlac::getEnergyPerFunction(std::string functionName) {
    if (FunctionEnergyCache.contains(functionName)) {
        return FunctionEnergyCache[functionName];
    }

    llvm::errs() << "Trying to get energy of " << functionName << " which has not been analyzed yet!\n";
    return -300000;
}

std::map<std::string, ILPModel> hlac::buildMonolithicILPS() {
    std::map<std::string, ILPModel> resultMapping;

    // Assume we iterate in postOrder
    for (auto &functionNode : functions) {
        // ignore phasar hooks
        if (!Util::starts_with(functionNode->function->getName().str(), "__psr") && !Util::starts_with(functionNode->function->getName().str(), "__clang")) {
            auto ilpModel = ILPBuilder::buildMonolithicILP(functionNode.get());

            resultMapping[functionNode->name] = ilpModel;

        } else {
            // std::cout << "Ignored ILP building for function " << functionNode->name << "\n";
        }
    }

    return resultMapping;
}

std::unordered_map<std::string, std::unordered_map<LoopNode *, ILPModel>> hlac::buildClusteredILPS() {
    std::unordered_map<std::string, std::unordered_map<LoopNode *, ILPModel>> resultMapping;

    // Assume we iterate in postOrder
    for (auto &functionNode : functions) {
        // ignore phasar hooks
        if (!Util::starts_with(functionNode->function->getName().str(), "__psr") && !Util::starts_with(functionNode->function->getName().str(), "__clang")) {
            auto ilpModelMapping = ILPBuilder::buildClusteredILP(functionNode.get());

            resultMapping[functionNode->name] = ilpModelMapping;

        } else {
            // std::cout << "Ignored ILP building for function " << functionNode->name << "\n";
        }
    }

    return resultMapping;
}


std::map<std::string, ILPResult>
hlac::solveMonolithicIlps(const std::map<std::string, ILPModel> &modelMapping) {
    std::map<std::string, ILPResult> result;

    for (const auto &[name, model] : modelMapping) {
        auto solvedModel = ILPBuilder::solveModel(model);

        if (solvedModel.has_value()) {
            result.emplace(name, std::move(*solvedModel));
        }
    }

    return result;
}

std::unordered_map<std::string, DAGLongestPathSolution> hlac::DAGLongestPath(std::unordered_map<std::string, std::unordered_map<HLAC::LoopNode *, ILPResult>> clusteredResult) {
    std::unordered_map<std::string, DAGLongestPathSolution> result;

    for (auto &functionNode : functions) {
        if (!Util::starts_with(functionNode->function->getName().str(), "__psr") && !Util::starts_with(functionNode->function->getName().str(), "__clang")) {
            auto [distances, predecessors] = ILPUtil::longestPathDAG(functionNode.get(), clusteredResult[functionNode->name]);

            auto funcNode = functionNode.get();

            // auto entryNode = funcNode->Nodes[funcNode->entryIndex];
            auto exitNode = funcNode->Nodes[funcNode->exitIndex].get();

            double exitEnergy = distances[exitNode];

            std::vector<Edge *> takenEdges = Util::findTakenEdges(exitNode, predecessors, funcNode->Edges);

            result[functionNode->name] = {exitEnergy, takenEdges};
        }
    }

    return result;
}

std::unordered_map<std::string, ILPClusteredLoopResult> hlac::solveClusteredIlps(
    const std::unordered_map<std::string, ILPLoopModelMapping> &modelMapping) {

    std::unordered_map<std::string, std::unordered_map<LoopNode *, ILPResult>> result;
    result.reserve(modelMapping.size());

    ILPClusterCache &cache = ILPClusterCache::getInstance();

    for (const auto &[name, loopModelMapping] : modelMapping) {
        // We need to solve the ILP for each loop and then combine the results to get the overall energy and path for the function
        std::unordered_map<LoopNode *, ILPResult> loopEnergyMapping;
        loopEnergyMapping.reserve(loopModelMapping.size());

        // std::cout << "Clustered results for " << name << ":\n";

        for (const auto &[loopNode, model] : loopModelMapping) {

            if (cache.entryExists(loopNode->hash)) {
                auto cachedResult = cache.getEntry(loopNode->hash);
                if (cachedResult.has_value()) {
                    loopEnergyMapping.emplace(loopNode, cachedResult.value());
                }
                continue;
            } else {
                auto solvedModel = ILPBuilder::solveModel(model);

                if (solvedModel.has_value()) {
                    // std::cout << "Loop " << loopNode->loop->getName().str() << " -> " << objectiveValue << std::endl;
                    cache.setEntry(loopNode->hash, solvedModel.value());
                    loopEnergyMapping.emplace(loopNode, std::move(*solvedModel));
                }
            }


        }

        result.emplace(name, std::move(loopEnergyMapping));
    }

    return result;
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
