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
    if (Util::starts_with(functionName, "__psr") || Util::starts_with(functionName, "__clang")) {
        // We return 0.0 for phasar hooks, as they are not relevant for our analysis and we do not want to log them
        return 0.0;
    }

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

std::optional<std::unordered_map<HLAC::LoopNode *, std::vector<std::pair<ILPModel, HLAC::VirtualNode *>>>>
hlac::buildClusteredILPS(FunctionNode *functionNode) {
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

std::optional<DAGLongestPathSolution> hlac::DAGLongestPath(
    FunctionNode *functionNode,
    std::unordered_map<HLAC::LoopNode *, std::vector<std::pair<ILPResult, HLAC::VirtualNode *>>> clusteredResult) {

    if (!Util::starts_with(functionNode->function->getName().str(), "__psr")
        && !Util::starts_with(functionNode->function->getName().str(), "__clang")) {

        if (functionNode->function->getName().str() == "LZ4_compress_fast_extState") {
            int test = 0;
        }

        auto [distances, predecessors] = ILPUtil::longestPathDAG(functionNode, clusteredResult);

        auto funcNode = functionNode;
        auto exitNode = funcNode->Nodes[funcNode->exitIndex].get();

        for (HLAC::GenericNode *node : funcNode->topologicalSortedRepresentationOfNodes) {
            for (const auto &edgeUP : funcNode->Edges) {
                HLAC::Edge *edge = edgeUP.get();

                if (edge == nullptr || edge->soure != node || edge->destination == nullptr) {
                    continue;
                }

                bool destinationInTopo = false;
                for (HLAC::GenericNode *topologicalNode : funcNode->topologicalSortedRepresentationOfNodes) {
                    if (topologicalNode == edge->destination) {
                        destinationInTopo = true;
                        break;
                    }
                }

                if (destinationInTopo) {
                    continue;
                }

                bool destinationInNodes = false;
                for (const auto &nodeUP : funcNode->Nodes) {
                    if (nodeUP.get() == edge->destination) {
                        destinationInNodes = true;
                        break;
                    }
                }

                std::cout << "[BROKEN EDGE] "
                          << edge->soure->getDotName()
                          << " -> "
                          << edge->destination->getDotName()
                          << " feasible="
                          << edge->feasibility
                          << " destinationInNodes="
                          << destinationInNodes
                          << "\n";
            }
        }

        auto exitDistanceIterator = distances.find(exitNode);

        if (exitDistanceIterator == distances.end()) {
            std::cout << "[ERROR] Exit node missing in DAG distances: "
                      << exitNode->getDotName()
                      << "\n";

            double maximumEnergy = -std::numeric_limits<double>::infinity();
            GenericNode *maximumEnergyNode = nullptr;

            for (const auto &distanceEntry : distances) {
                if (distanceEntry.second > maximumEnergy) {
                    maximumEnergy = distanceEntry.second;
                    maximumEnergyNode = distanceEntry.first;
                }
            }

            if (maximumEnergyNode == nullptr) {
                return std::nullopt;
            }

            for (const auto &nodeEntry : distances) {
                std::cout << "Node: " << nodeEntry.first->getDotName() << ": \n";
                if (auto nn = dynamic_cast<HLAC::Node *>(nodeEntry.first)) {
                    std::cout << nn->block->getName().str() << "\n";
                }
            }

            std::cout << "[INFO] Falling back to maximum-distance node: "
                      << maximumEnergyNode->getDotName()
                      << " energy="
                      << maximumEnergy
                      << "\n";

            std::vector<Edge *> takenEdges = Util::findTakenEdges(
                maximumEnergyNode,
                predecessors,
                funcNode->Edges,
                clusteredResult);

            return std::make_optional<DAGLongestPathSolution>({maximumEnergy, takenEdges});
        }

        double exitEnergy = exitDistanceIterator->second;

        std::vector<Edge *> takenEdges = Util::findTakenEdges(
            exitNode,
            predecessors,
            funcNode->Edges,
            clusteredResult);

        return std::make_optional<DAGLongestPathSolution>({exitEnergy, takenEdges});
    }

    return std::nullopt;
}

std::unordered_map<HLAC::LoopNode *, std::vector<std::pair<ILPResult, HLAC::VirtualNode *>>> hlac::solveClusteredIlps(
    std::unordered_map<HLAC::LoopNode *, std::vector<std::pair<ILPModel, HLAC::VirtualNode *>>> loopModelMapping) {

    ILPClusterCache &cache = ILPClusterCache::getInstance();

    std::unordered_map<HLAC::LoopNode *, std::vector<std::pair<ILPResult, HLAC::VirtualNode *>>> loopEnergyMapping;
    loopEnergyMapping.reserve(loopModelMapping.size());

    for (const auto &[loopNode, loopModels] : loopModelMapping) {
        if (loopNode == nullptr) {
            continue;
        }

        std::vector<std::pair<ILPResult, HLAC::VirtualNode *>> exitResults;
        exitResults.reserve(loopModels.size());

        for (const auto &[model, exitNode] : loopModels) {
            if (exitNode == nullptr) {
                continue;
            }

            // TODO: Enable caching again.
            //
            // Important: The cache key must include the exit node or another stable exit identifier.
            // Otherwise, different exit-specific ILPs for the same loop overwrite each other.
            //
            // const std::string cacheKey =
            //     std::to_string(loopNode->hash) + "_exit_" + std::to_string(exitNode->hash);

            std::optional<ILPResult> solvedModel = ILPBuilder::solveClusteredLoopModel(model, loopNode);

            if (!solvedModel.has_value()) {
                continue;
            }

            // TODO: Cache enabling.
            // cache.setEntry(cacheKey, solvedModel.value());

            exitResults.emplace_back(std::move(solvedModel.value()), exitNode);
        }

        if (!exitResults.empty()) {
            loopEnergyMapping.emplace(loopNode, std::move(exitResults));
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
