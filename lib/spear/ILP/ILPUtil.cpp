/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ILP/ILPUtil.h"

#include <CoinPackedVector.hpp>

#include "Logger.h"


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
        double coeff = elements[k];
        int col = indices[k];

        if (std::abs(coeff) < 1e-15) {
            continue;
        }

        if (!first) {
            oss << (coeff >= 0.0 ? " + " : " - ");
        } else if (coeff < 0.0) {
            oss << "-";
        }

        double absCoeff = std::abs(coeff);
        if (std::abs(absCoeff - 1.0) > 1e-15) {
            oss << absCoeff << "*";
        }

        oss << "x" << col;
        first = false;
    }

    if (first) {
        oss << "0";
    }

    return oss.str();
}

void ILPUtil::printILPModelHumanReadable(std::string funcname, const ILPModel &model) {
    const CoinPackedMatrix &matrix = model.matrix;

    const int numRows = matrix.getNumRows();
    const int numCols = matrix.getNumCols();

    std::cout << "================ Monolithic ILP Model - " << funcname << " ================\n\n";

    std::cout << "Objective:\n";
    std::cout << "  maximize ";

    bool firstObj = true;
    for (int j = 0; j < numCols; ++j) {
        double coeff = model.obj[j];

        if (std::abs(coeff) < 1e-15) {
            continue;
        }

        if (!firstObj) {
            std::cout << (coeff >= 0.0 ? " + " : " - ");
        } else if (coeff < 0.0) {
            std::cout << "-";
        }

        double absCoeff = std::abs(coeff);
        if (std::abs(absCoeff - 1.0) > 1e-15) {
            std::cout << absCoeff << "*";
        }

        std::cout << "x" << j;
        firstObj = false;
    }

    if (firstObj) {
        std::cout << "0";
    }

    std::cout << "\n\n";

    std::cout << "Constraints:\n";
    for (int i = 0; i < numRows; ++i) {
        std::string expr = formatLinearExpr(matrix, i);

        double lb = model.row_lb[i];
        double ub = model.row_ub[i];

        bool hasLb = lb > -COIN_DBL_MAX / 2;
        bool hasUb = ub < COIN_DBL_MAX / 2;

        std::cout << "  c" << i << ": ";

        if (hasLb && hasUb) {
            if (std::abs(lb - ub) < 1e-12) {
                std::cout << expr << " = " << boundToString(lb);
            } else {
                std::cout << boundToString(lb) << " <= " << expr << " <= " << boundToString(ub);
            }
        } else if (hasLb) {
            std::cout << expr << " >= " << boundToString(lb);
        } else if (hasUb) {
            std::cout << expr << " <= " << boundToString(ub);
        } else {
            std::cout << "-inf <= " << expr << " <= inf";
        }

        std::cout << "\n";
    }

    std::cout << "\nVariable bounds:\n";
    for (int j = 0; j < numCols; ++j) {
        std::cout << "  " << boundToString(model.col_lb[j]) << " <= x" << j << " <= " << boundToString(model.col_ub[j])
                  << "\n";
    }

    std::cout << "\n===========================================\n";
}

void ILPUtil::printILPModelHumanReadable(std::string funcname, std::string loopname, const ILPModel &model) {
    const CoinPackedMatrix &matrix = model.matrix;

    const int numRows = matrix.getNumRows();
    const int numCols = matrix.getNumCols();

    std::cout << "================ Clustered ILP Model - " << funcname << "(" << loopname << ")"
              << " ================\n\n";

    std::cout << "Objective:\n";
    std::cout << "  maximize ";

    bool firstObj = true;
    for (int j = 0; j < numCols; ++j) {
        double coeff = model.obj[j];

        if (std::abs(coeff) < 1e-15) {
            continue;
        }

        if (!firstObj) {
            std::cout << (coeff >= 0.0 ? " + " : " - ");
        } else if (coeff < 0.0) {
            std::cout << "-";
        }

        double absCoeff = std::abs(coeff);
        if (std::abs(absCoeff - 1.0) > 1e-15) {
            std::cout << absCoeff << "*";
        }

        std::cout << "x" << j;
        firstObj = false;
    }

    if (firstObj) {
        std::cout << "0";
    }

    std::cout << "\n\n";

    std::cout << "Constraints:\n";
    for (int i = 0; i < numRows; ++i) {
        std::string expr = formatLinearExpr(matrix, i);

        double lb = model.row_lb[i];
        double ub = model.row_ub[i];

        bool hasLb = lb > -COIN_DBL_MAX / 2;
        bool hasUb = ub < COIN_DBL_MAX / 2;

        std::cout << "  c" << i << ": ";

        if (hasLb && hasUb) {
            if (std::abs(lb - ub) < 1e-12) {
                std::cout << expr << " = " << boundToString(lb);
            } else {
                std::cout << boundToString(lb) << " <= " << expr << " <= " << boundToString(ub);
            }
        } else if (hasLb) {
            std::cout << expr << " >= " << boundToString(lb);
        } else if (hasUb) {
            std::cout << expr << " <= " << boundToString(ub);
        } else {
            std::cout << "-inf <= " << expr << " <= inf";
        }

        std::cout << "\n";
    }

    std::cout << "\nVariable bounds:\n";
    for (int j = 0; j < numCols; ++j) {
        std::cout << "  " << boundToString(model.col_lb[j]) << " <= x" << j << " <= " << boundToString(model.col_ub[j])
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
        Logger::getInstance().log("Function has no node without incoming edges. Cannot determine virtual entry node.",
                                  LOGLEVEL::ERROR);
        return nullptr;
    }

    if (entryCandidates.size() > 1) {
        std::ostringstream oss;
        oss << "Function has multiple nodes without incoming edges (" << entryCandidates.size() << "). Candidates:\n";

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
            Logger::getInstance().log("Function has multiple virtual entry nodes among entry candidates. Cannot "
                                      "uniquely determine virtual entry node.",
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

static std::vector<bool>
findReachableNodesFromStart(HLAC::GenericNode *startNode,
                            const std::unordered_map<HLAC::GenericNode *, std::size_t> &nodeToIndex,
                            const std::vector<std::vector<HLAC::Edge *>> &adjacency, std::size_t numberOfNodes) {
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

ILPLongestPathDAGSolution ILPUtil::longestPathDAG(HLAC::FunctionNode *func,
                                                  const std::unordered_map<HLAC::LoopNode *, ILPResult> &loopMapping) {
    // Define the default value in the DAG search
    const double NEG_INF = -std::numeric_limits<double>::infinity();

    // Get the topological ordering of the current function
    const auto &nodes = func->topologicalSortedRepresentationOfNodes;
    if (nodes.empty()) {
        return {};
    }

    const std::size_t numberOfNodes = nodes.size();

    // Query the node lookup and adjacency representation from the function for efficient access during the DAG search
    const auto &nodeToIndex = func->nodeLookup;
    const auto &adjacency = func->adjacencyRepresentation;
    auto &nodeEnergy = func->nodeEnergy;

    // Iterate over the nodes
    for (HLAC::GenericNode *node : nodes) {
        // Validate the node
        if (node == nullptr) {
            continue;
        }

        // Find the index of the node
        auto nodeIterator = nodeToIndex.find(node);
        if (nodeIterator == nodeToIndex.end()) {
            continue;
        }

        const std::size_t nodeIndex = nodeIterator->second;

        // If we find a loopnode, we query the loop mapping for the optimal value and use it as energy value for
        // the DAG search
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(node)) {
            try {
                const auto loopResult = loopMapping.at(loopNode);
                nodeEnergy[nodeIndex] = loopResult.optimalValue;
            } catch (const std::out_of_range &) {
                Logger::getInstance().log("Warning: LoopNode " + loopNode->getDotName() +
                                                  " not found in loop mapping, using 0.0 as energy value.",
                                          LOGLEVEL::WARNING);
                nodeEnergy[nodeIndex] = 0.0;
            }
        }
    }

    // Find the entry node where we start the search from
    HLAC::GenericNode *start = findVirtualEntryNode(func);

    if (start == nullptr) {
        Logger::getInstance().log("Could not determine unique virtual entry node for function " + func->name,
                                  LOGLEVEL::ERROR);
        return {};
    }

    auto startIterator = nodeToIndex.find(start);
    if (startIterator == nodeToIndex.end()) {
        Logger::getInstance().log("Virtual entry node is not contained in node lookup for function " + func->name,
                                  LOGLEVEL::ERROR);
        return {};
    }

    // Find all nodes reachable from the startnode
    const std::vector<bool> reachable = findReachableNodesFromStart(start, nodeToIndex, adjacency, numberOfNodes);

    std::size_t unreachableNodeCount = 0;
    for (bool isReachable : reachable) {
        if (!isReachable) {
            ++unreachableNodeCount;
        }
    }

    if (unreachableNodeCount > 0) {
        Logger::getInstance().log("Ignoring " + std::to_string(unreachableNodeCount) +
                                          " unreachable nodes in DAG longest path for function " + func->name,
                                  LOGLEVEL::WARNING);
    }

    // Initialize the node distances to the negative default value
    std::vector<double> distance(numberOfNodes, NEG_INF);
    // Init the parent list
    std::vector<HLAC::GenericNode *> parent(numberOfNodes, nullptr);

    distance[startIterator->second] = nodeEnergy[startIterator->second];

    // Iterate over the nodes
    for (HLAC::GenericNode *currentNode : nodes) {
        if (currentNode == nullptr) {
            continue;
        }

        // Find the current node index
        auto currentIterator = nodeToIndex.find(currentNode);
        if (currentIterator == nodeToIndex.end()) {
            continue;
        }

        const std::size_t currentIndex = currentIterator->second;

        // Check that the node is reachable
        if (!reachable[currentIndex]) {
            continue;
        }

        // Get the distance to the current node
        const double currentDistance = distance[currentIndex];
        if (currentDistance == NEG_INF) {
            continue;
        }

        // Iterate over the edges adjacent to this node
        for (HLAC::Edge *edge : adjacency[currentIndex]) {
            // Do not consider the edge if its invalid or infeasible
            if (edge == nullptr || !edge->feasibility || edge->destination == nullptr) {
                continue;
            }

            // Find the destination node
            auto destinationIterator = nodeToIndex.find(edge->destination);
            if (destinationIterator == nodeToIndex.end()) {
                continue;
            }

            const std::size_t destinationIndex = destinationIterator->second;

            // Check that the destination node is reachable
            if (!reachable[destinationIndex]) {
                continue;
            }

            // Calculate the energy of the current distance + the energy of the destination
            const double candidateEnergy = currentDistance + nodeEnergy[destinationIndex];

            // If the calculated energy is larger than the current distance to the destination node update it
            if (candidateEnergy > distance[destinationIndex]) {
                distance[destinationIndex] = candidateEnergy;
                parent[destinationIndex] = currentNode;
            }
        }
    }

    // Calculate the output maps
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

void ILPUtil::buildIncidenceMaps(const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
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

        // Validate the ilp index of the edge
        if (edge->ilpIndex < 0 || edge->ilpIndex >= numberOfVariables) {
            throw std::runtime_error("Invalid edge ilpIndex " + std::to_string(edge->ilpIndex) + " for model with " +
                                     std::to_string(numberOfVariables) + " variables.");
        }

        // Validate the validity of the edge
        if (edge->destination == nullptr || edge->soure == nullptr) {
            throw std::runtime_error("Edge with null source or destination encountered.");
        }

        // Fill incoming and outgoing mapping via the source and destination of the edge.
        // Store the ilp index of the edge
        incoming[edge->destination].push_back(edge->ilpIndex);
        outgoing[edge->soure].push_back(edge->ilpIndex);
    }
}

void ILPUtil::insertUnique(CoinPackedVector &row, std::unordered_set<int> &used, int col, double coeff) {
    // Check that we do not add columns that are already contained in the vector
    if (!used.insert(col).second) {
        Logger::getInstance().log("Warning: duplicate column " + std::to_string(col) + " while building row",
                                  LOGLEVEL::WARNING);
        return;
    }

    // Inser the row and the respective coefficient into the vector
    row.insert(col, coeff);
}

void ILPUtil::appendRow(ILPModel &model, const CoinPackedVector &row, double lb, double ub) {
    model.matrix.appendRow(row);
    model.row_lb.push_back(lb);
    model.row_ub.push_back(ub);
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

void ILPUtil::insertOrAccumulate(std::unordered_map<int, double> &coefficientsByColumn, int column,
                                 double coefficient) {
    coefficientsByColumn[column] += coefficient;
}

CoinPackedVector ILPUtil::createRowFromCoefficients(const std::unordered_map<int, double> &coefficientsByColumn) {
    CoinPackedVector row;

    for (const auto &[column, coefficient] : coefficientsByColumn) {
        // If the coefficient falls below this value we do not add it to the final row
        if (std::abs(coefficient) > 1e-12) {
            row.insert(column, coefficient);
        }
    }

    return row;
}
