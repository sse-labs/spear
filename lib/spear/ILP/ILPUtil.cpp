/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>
#include <unordered_map>
#include <utility>

#include "ILP/ILPUtil.h"
#include "HLAC/util.h"

#include <CoinPackedVector.hpp>

#include "Logger.h"

namespace {
constexpr double zeroTolerance = 1.0e-15;

double getBestClusteredExitEnergyForSuccessor(
    HLAC::LoopNode *loopNode,
    HLAC::GenericNode *successorNode,
    const std::unordered_map<HLAC::LoopNode *, std::vector<std::pair<ILPResult, HLAC::VirtualNode *>>> &loopMapping,
    bool &foundMatchingExit) {

    foundMatchingExit = false;

    if (loopNode == nullptr || successorNode == nullptr) {
        return -std::numeric_limits<double>::infinity();
    }

    auto loopMappingIterator = loopMapping.find(loopNode);
    if (loopMappingIterator == loopMapping.end()) {
        Logger::getInstance().log(
            "Warning: LoopNode "
            + loopNode->getDotName()
            + " not found in loop mapping, skipping loop exit edge.",
            LOGLEVEL::WARNING);

        return -std::numeric_limits<double>::infinity();
    }

    double bestExitEnergy = -std::numeric_limits<double>::infinity();

    for (const auto &[loopResult, virtualExitNode] : loopMappingIterator->second) {
        if (virtualExitNode == nullptr) {
            continue;
        }

        if (virtualExitNode->successor != successorNode) {
            continue;
        }

        foundMatchingExit = true;
        bestExitEnergy = std::max(bestExitEnergy, loopResult.optimalValue);
    }

    return bestExitEnergy;
}
}  // namespace

std::string ILPUtil::boundToString(double value) {
    if (value <= -COIN_DBL_MAX / 2) {
        return "-inf";
    }

    if (value >= COIN_DBL_MAX / 2) {
        return "inf";
    }

    std::ostringstream oss;
    oss << std::setprecision(12) << value;
    return oss.str();
}

std::string ILPUtil::formatLinearExpr(const CoinPackedMatrix &matrix, int row) {
    std::ostringstream oss;

    const int *indices = matrix.getIndices();
    const double *elements = matrix.getElements();
    const CoinBigIndex *starts = matrix.getVectorStarts();
    const int *lengths = matrix.getVectorLengths();

    bool first = true;
    CoinBigIndex start = starts[row];
    CoinBigIndex end = start + lengths[row];

    for (CoinBigIndex k = start; k < end; ++k) {
        double coefficient = elements[k];
        int column = indices[k];

        if (std::abs(coefficient) < zeroTolerance) {
            continue;
        }

        if (!first) {
            oss << (coefficient >= 0.0 ? " + " : " - ");
        } else if (coefficient < 0.0) {
            oss << "-";
        }

        double absoluteCoefficient = std::abs(coefficient);
        if (std::abs(absoluteCoefficient - 1.0) > zeroTolerance) {
            oss << absoluteCoefficient << "*";
        }

        oss << "x" << column;
        first = false;
    }

    if (first) {
        oss << "0";
    }

    return oss.str();
}

void ILPUtil::printILPModelHumanReadable(std::string funcname, const ILPModel &model) {
    const CoinPackedMatrix &matrix = model.matrix;

    const int numberOfRows = matrix.getNumRows();
    const int numberOfColumns = matrix.getNumCols();

    std::cout << "================ Monolithic ILP Model - " << funcname << " ================\n\n";

    std::cout << "Objective:\n";
    std::cout << "  maximize ";

    bool firstObjectiveCoefficient = true;
    for (int column = 0; column < numberOfColumns; ++column) {
        double coefficient = model.obj[column];

        if (std::abs(coefficient) < zeroTolerance) {
            continue;
        }

        if (!firstObjectiveCoefficient) {
            std::cout << (coefficient >= 0.0 ? " + " : " - ");
        } else if (coefficient < 0.0) {
            std::cout << "-";
        }

        double absoluteCoefficient = std::abs(coefficient);
        if (std::abs(absoluteCoefficient - 1.0) > zeroTolerance) {
            std::cout << absoluteCoefficient << "*";
        }

        std::cout << "x" << column;
        firstObjectiveCoefficient = false;
    }

    if (firstObjectiveCoefficient) {
        std::cout << "0";
    }

    std::cout << "\n\n";

    std::cout << "Constraints:\n";
    for (int row = 0; row < numberOfRows; ++row) {
        std::string expression = formatLinearExpr(matrix, row);

        double lowerBound = model.row_lb[row];
        double upperBound = model.row_ub[row];

        bool hasLowerBound = lowerBound > -COIN_DBL_MAX / 2;
        bool hasUpperBound = upperBound < COIN_DBL_MAX / 2;

        std::cout << "  c" << row << ": ";

        if (hasLowerBound && hasUpperBound) {
            if (std::abs(lowerBound - upperBound) < 1e-12) {
                std::cout << expression << " = " << boundToString(lowerBound);
            } else {
                std::cout << boundToString(lowerBound) << " <= "
                          << expression << " <= "
                          << boundToString(upperBound);
            }
        } else if (hasLowerBound) {
            std::cout << expression << " >= " << boundToString(lowerBound);
        } else if (hasUpperBound) {
            std::cout << expression << " <= " << boundToString(upperBound);
        } else {
            std::cout << "-inf <= " << expression << " <= inf";
        }

        std::cout << "\n";
    }

    std::cout << "\nVariable bounds:\n";
    for (int column = 0; column < numberOfColumns; ++column) {
        std::cout << "  "
                  << boundToString(model.col_lb[column])
                  << " <= x" << column
                  << " <= " << boundToString(model.col_ub[column])
                  << "\n";
    }

    std::cout << "\n===========================================\n";
}

void ILPUtil::printILPModelHumanReadable(std::string funcname, std::string loopname, const ILPModel &model) {
    const CoinPackedMatrix &matrix = model.matrix;

    const int numberOfRows = matrix.getNumRows();
    const int numberOfColumns = matrix.getNumCols();

    std::cout << "================ Clustered ILP Model - " << funcname << "(" << loopname << ")"
              << " ================\n\n";

    std::cout << "Objective:\n";
    std::cout << "  maximize ";

    bool firstObjectiveCoefficient = true;
    for (int column = 0; column < numberOfColumns; ++column) {
        double coefficient = model.obj[column];

        if (std::abs(coefficient) < zeroTolerance) {
            continue;
        }

        if (!firstObjectiveCoefficient) {
            std::cout << (coefficient >= 0.0 ? " + " : " - ");
        } else if (coefficient < 0.0) {
            std::cout << "-";
        }

        double absoluteCoefficient = std::abs(coefficient);
        if (std::abs(absoluteCoefficient - 1.0) > zeroTolerance) {
            std::cout << absoluteCoefficient << "*";
        }

        std::cout << "x" << column;
        firstObjectiveCoefficient = false;
    }

    if (firstObjectiveCoefficient) {
        std::cout << "0";
    }

    std::cout << "\n\n";

    std::cout << "Constraints:\n";
    for (int row = 0; row < numberOfRows; ++row) {
        std::string expression = formatLinearExpr(matrix, row);

        double lowerBound = model.row_lb[row];
        double upperBound = model.row_ub[row];

        bool hasLowerBound = lowerBound > -COIN_DBL_MAX / 2;
        bool hasUpperBound = upperBound < COIN_DBL_MAX / 2;

        std::cout << "  c" << row << ": ";

        if (hasLowerBound && hasUpperBound) {
            if (std::abs(lowerBound - upperBound) < 1e-12) {
                std::cout << expression << " = " << boundToString(lowerBound);
            } else {
                std::cout << boundToString(lowerBound) << " <= "
                          << expression << " <= "
                          << boundToString(upperBound);
            }
        } else if (hasLowerBound) {
            std::cout << expression << " >= " << boundToString(lowerBound);
        } else if (hasUpperBound) {
            std::cout << expression << " <= " << boundToString(upperBound);
        } else {
            std::cout << "-inf <= " << expression << " <= inf";
        }

        std::cout << "\n";
    }

    std::cout << "\nVariable bounds:\n";
    for (int column = 0; column < numberOfColumns; ++column) {
        std::cout << "  "
                  << boundToString(model.col_lb[column])
                  << " <= x" << column
                  << " <= " << boundToString(model.col_ub[column])
                  << "\n";
    }

    std::cout << "\n===========================================\n";
}

static HLAC::GenericNode *findVirtualEntryNode(HLAC::FunctionNode *functionNode) {
    if (functionNode == nullptr) {
        return nullptr;
    }

    std::unordered_map<HLAC::GenericNode *, std::size_t> incomingEdgeCount;

    // Initialize incoming edge counter for all nodes
    for (const auto &nodePointer : functionNode->Nodes) {
        if (nodePointer != nullptr) {
            incomingEdgeCount[nodePointer.get()] = 0;
        }
    }

    // Count incoming edges
    for (const auto &edgePointer : functionNode->Edges) {
        if (edgePointer == nullptr || edgePointer->destination == nullptr) {
            continue;
        }

        ++incomingEdgeCount[edgePointer->destination];
    }

    std::vector<HLAC::GenericNode *> entryCandidates;

    // Collect all nodes with zero incoming edges
    for (const auto &[node, incomingCount] : incomingEdgeCount) {
        if (incomingCount == 0) {
            entryCandidates.push_back(node);
        }
    }

    if (entryCandidates.empty()) {
        Logger::getInstance().log(
            "Function has no node without incoming edges. Cannot determine virtual entry node.",
            LOGLEVEL::ERROR);
        return nullptr;
    }

    if (entryCandidates.size() > 1) {
        std::ostringstream oss;
        oss << "Function has multiple nodes without incoming edges ("
            << entryCandidates.size()
            << "). Candidates:\n";

        for (auto *node : entryCandidates) {
            if (node != nullptr) {
                oss << "  - " << node->name << "\n";
            } else {
                oss << "  - <null>\n";
            }
        }

        Logger::getInstance().log(oss.str(), LOGLEVEL::ERROR);
    }

    HLAC::GenericNode *virtualEntryCandidate = nullptr;

    for (auto *node : entryCandidates) {
        if (node == nullptr || node->nodeType != HLAC::NodeType::VIRTUALNODE) {
            continue;
        }

        auto *virtualNode = dynamic_cast<HLAC::VirtualNode *>(node);

        if (!virtualNode->isEntry) {
            continue;
        }

        if (virtualEntryCandidate != nullptr) {
            Logger::getInstance().log(
                "Function has multiple virtual entry nodes among entry candidates. Cannot uniquely determine virtual entry node.",
                LOGLEVEL::ERROR);
            return nullptr;
        }

        virtualEntryCandidate = node;
    }

    if (virtualEntryCandidate != nullptr) {
        return virtualEntryCandidate;
    }

    if (entryCandidates.size() == 1) {
        return entryCandidates.front();
    }

    Logger::getInstance().log(
        "Function has multiple nodes without incoming edges, but none is marked as virtual entry node.",
        LOGLEVEL::ERROR);

    return nullptr;
}

static std::vector<bool> findReachableNodesFromStart(
    HLAC::GenericNode *startNode,
    const std::unordered_map<HLAC::GenericNode *, std::size_t> &nodeToIndex,
    const std::vector<std::vector<HLAC::Edge *>> &adjacency,
    std::size_t numberOfNodes) {

    std::vector<bool> reachable(numberOfNodes, false);

    if (startNode == nullptr) {
        return reachable;
    }

    auto startIterator = nodeToIndex.find(startNode);
    if (startIterator == nodeToIndex.end()) {
        return reachable;
    }

    std::vector<HLAC::GenericNode *> worklist;
    worklist.push_back(startNode);
    reachable[startIterator->second] = true;

    while (!worklist.empty()) {
        HLAC::GenericNode *currentNode = worklist.back();
        worklist.pop_back();

        const std::size_t currentIndex = nodeToIndex.at(currentNode);

        for (HLAC::Edge *edge : adjacency[currentIndex]) {
            if (edge == nullptr || !edge->feasibility || edge->destination == nullptr) {
                continue;
            }

            auto destinationIterator = nodeToIndex.find(edge->destination);
            if (destinationIterator == nodeToIndex.end()) {
                continue;
            }

            const std::size_t destinationIndex = destinationIterator->second;

            if (reachable[destinationIndex]) {
                continue;
            }

            reachable[destinationIndex] = true;
            worklist.push_back(edge->destination);
        }
    }

    return reachable;
}

ILPLongestPathDAGSolution ILPUtil::longestPathDAG(
    HLAC::FunctionNode *func,
    std::unordered_map<HLAC::LoopNode *, std::vector<std::pair<ILPResult, HLAC::VirtualNode *>>> &loopMapping) {

    const double negativeInfinity = -std::numeric_limits<double>::infinity();

    const auto &nodes = func->topologicalSortedRepresentationOfNodes;
    if (nodes.empty()) {
        return {};
    }

    const std::size_t numberOfNodes = nodes.size();

    const auto &nodeToIndex = func->nodeLookup;
    const auto &adjacency = func->adjacencyRepresentation;
    auto &nodeEnergy = func->nodeEnergy;

    HLAC::GenericNode *start = findVirtualEntryNode(func);

    if (start == nullptr) {
        Logger::getInstance().log(
            "Could not determine unique virtual entry node for function " + func->name,
            LOGLEVEL::ERROR);
        return {};
    }

    auto startIterator = nodeToIndex.find(start);
    if (startIterator == nodeToIndex.end()) {
        Logger::getInstance().log(
            "Virtual entry node is not contained in node lookup for function " + func->name,
            LOGLEVEL::ERROR);
        return {};
    }

    const std::vector<bool> reachable = findReachableNodesFromStart(
        start,
        nodeToIndex,
        adjacency,
        numberOfNodes);

    std::size_t unreachableNodeCount = 0;
    for (bool isReachable : reachable) {
        if (!isReachable) {
            ++unreachableNodeCount;
        }
    }

    if (unreachableNodeCount > 0) {
        Logger::getInstance().log(
            "Ignoring "
            + std::to_string(unreachableNodeCount)
            + " unreachable nodes in DAG longest path for function "
            + func->name,
            LOGLEVEL::WARNING);
    }

    std::vector<double> distance(numberOfNodes, negativeInfinity);
    std::vector<HLAC::GenericNode *> parent(numberOfNodes, nullptr);

    distance[startIterator->second] = nodeEnergy[startIterator->second];

    for (HLAC::GenericNode *currentNode : nodes) {
        if (currentNode == nullptr) {
            continue;
        }

        auto currentIterator = nodeToIndex.find(currentNode);
        if (currentIterator == nodeToIndex.end()) {
            continue;
        }

        const std::size_t currentIndex = currentIterator->second;

        if (!reachable[currentIndex]) {
            continue;
        }

        const double currentDistance = distance[currentIndex];
        if (currentDistance == negativeInfinity) {
            continue;
        }

        for (HLAC::Edge *edge : adjacency[currentIndex]) {
            if (edge == nullptr || !edge->feasibility || edge->destination == nullptr) {
                continue;
            }

            auto destinationIterator = nodeToIndex.find(edge->destination);
            if (destinationIterator == nodeToIndex.end()) {
                continue;
            }

            const std::size_t destinationIndex = destinationIterator->second;

            if (!reachable[destinationIndex]) {
                continue;
            }

            double edgeContribution = nodeEnergy[destinationIndex];

            if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(currentNode)) {
                bool foundMatchingExit = false;
                const double bestExitEnergy = getBestClusteredExitEnergyForSuccessor(
                    loopNode,
                    edge->destination,
                    loopMapping,
                    foundMatchingExit);

                if (!foundMatchingExit) {
                    continue;
                }

                edgeContribution = bestExitEnergy + nodeEnergy[destinationIndex];
            }

            const double candidateEnergy = currentDistance + edgeContribution;

            if (candidateEnergy > distance[destinationIndex]) {
                distance[destinationIndex] = candidateEnergy;
                parent[destinationIndex] = currentNode;
            }
        }
    }

    std::unordered_map<HLAC::GenericNode *, double> distanceMap;
    std::unordered_map<HLAC::GenericNode *, HLAC::GenericNode *> parentMap;

    distanceMap.reserve(numberOfNodes);
    parentMap.reserve(numberOfNodes);

    for (HLAC::GenericNode *node : nodes) {
        if (node == nullptr) {
            continue;
        }

        auto nodeIterator = nodeToIndex.find(node);
        if (nodeIterator == nodeToIndex.end()) {
            continue;
        }

        const std::size_t nodeIndex = nodeIterator->second;

        if (!reachable[nodeIndex]) {
            continue;
        }

        distanceMap.emplace(node, distance[nodeIndex]);
        parentMap.emplace(node, parent[nodeIndex]);
    }

    return {std::move(distanceMap), std::move(parentMap)};
}

int ILPUtil::assignEdgeIndicesFunction(HLAC::FunctionNode *func, int nextIndex) {
    // Iterate over the edges in this function and assign them an index
    for (auto &edgeUP : func->Edges) {
        edgeUP->ilpIndex = nextIndex++;
    }

    for (auto &nodeUP : func->Nodes) {
        // For loopnodes we need to consider the contained edges. Therefore, we need to index them as well
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(nodeUP.get())) {
            nextIndex = assignEdgeIndicesLoop(loopNode, nextIndex);
        }
    }

    // For the recursive behavior we need to return the last used index
    return nextIndex;
}

int ILPUtil::assignEdgeIndicesLoop(HLAC::LoopNode *loopNode, int nextIndex) {
    // Index the edges contained in this loop
    for (auto &edgeUP : loopNode->Edges) {
        edgeUP->ilpIndex = nextIndex++;
    }

    for (auto &nodeUP : loopNode->Nodes) {
        if (auto *innerLoop = dynamic_cast<HLAC::LoopNode *>(nodeUP.get())) {
            // Index the subloops edges
            nextIndex = assignEdgeIndicesLoop(innerLoop, nextIndex);
        }
    }

    // Return the last index for the recursive behavior
    return nextIndex;
}

void ILPUtil::buildIncidenceMaps(
    const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
    std::unordered_map<HLAC::GenericNode *, std::vector<int>> &incoming,
    std::unordered_map<HLAC::GenericNode *, std::vector<int>> &outgoing,
    int numberOfVariables) {

    incoming.clear();
    outgoing.clear();

    for (const auto &edgeUP : edges) {
        HLAC::Edge *edge = edgeUP.get();

        if (edge == nullptr) {
            continue;
        }

        if (edge->ilpIndex < 0 || edge->ilpIndex >= numberOfVariables) {
            throw std::runtime_error(
                "Invalid edge ilpIndex " + std::to_string(edge->ilpIndex) +
                " for model with " + std::to_string(numberOfVariables) + " variables.");
        }

        if (edge->destination == nullptr || edge->soure == nullptr) {
            throw std::runtime_error("Edge with null source or destination encountered.");
        }

        incoming[edge->destination].push_back(edge->ilpIndex);
        outgoing[edge->soure].push_back(edge->ilpIndex);
    }
}

void ILPUtil::insertUnique(
    CoinPackedVector &row,
    std::unordered_set<int> &used,
    int column,
    double coefficient) {

    // Check that we do not add columns that are already contained in the vector
    if (!used.insert(column).second) {
        Logger::getInstance().log(
            "Warning: duplicate column " + std::to_string(column) + " while building row",
            LOGLEVEL::WARNING);
        return;
    }

    // Insert the row and the respective coefficient into the vector
    row.insert(column, coefficient);
}

void ILPUtil::appendRow(ILPModel &model, const CoinPackedVector &row, double lowerBound, double upperBound) {
    model.matrix.appendRow(row);
    model.row_lb.push_back(lowerBound);
    model.row_ub.push_back(upperBound);
}

int ILPUtil::getMaxEdgeIndex(HLAC::LoopNode *loopNode) {
    // Init the maxindex at -1 as all ILPindices are positive and 0
    int maxIndex = -1;

    // Iterate over the edges in the loopNode
    for (auto &edgeUP : loopNode->Edges) {
        auto *edge = edgeUP.get();

        // Check if the ILPIndex is larger than our last maxIndex
        if (edge && edge->ilpIndex > maxIndex) {
            maxIndex = edge->ilpIndex;
        }
    }

    // Check if, the maxIndex is located in one of the sub loopnodes
    for (auto &nodeUP : loopNode->Nodes) {
        if (auto *innerLoop = dynamic_cast<HLAC::LoopNode *>(nodeUP.get())) {
            int innerMax = getMaxEdgeIndex(innerLoop);
            if (innerMax > maxIndex) {
                maxIndex = innerMax;
            }
        }
    }

    return maxIndex;
}