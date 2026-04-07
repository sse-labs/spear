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
#include <unordered_set>
#include <vector>
#include <unordered_map>
#include <utility>

#include "ILP/ILPUtil.h"
#include "HLAC/util.h"

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

    std::cout << "================ Monolithic ILP Model - "<< funcname <<  " ================\n\n";

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
                std::cout << boundToString(lb) << " <= "
                          << expr << " <= "
                          << boundToString(ub);
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
        std::cout << "  "
                  << boundToString(model.col_lb[j])
                  << " <= x" << j
                  << " <= " << boundToString(model.col_ub[j])
                  << "\n";
    }

    std::cout << "\n===========================================\n";
}

void ILPUtil::printILPModelHumanReadable(std::string funcname, std::string loopname, const ILPModel &model) {
    const CoinPackedMatrix &matrix = model.matrix;

    const int numRows = matrix.getNumRows();
    const int numCols = matrix.getNumCols();

    std::cout << "================ Clustered ILP Model - "<< funcname << "(" << loopname << ")"
    <<  " ================\n\n";

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
                std::cout << boundToString(lb) << " <= "
                          << expr << " <= "
                          << boundToString(ub);
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
        std::cout << "  "
                  << boundToString(model.col_lb[j])
                  << " <= x" << j
                  << " <= " << boundToString(model.col_ub[j])
                  << "\n";
    }

    std::cout << "\n===========================================\n";
}

ILPLongestPathDAGSolution ILPUtil::longestPathDAG(
    HLAC::FunctionNode *func,
    const std::unordered_map<HLAC::LoopNode*, ILPResult> &loopMapping) {
    const double NEG_INF = -std::numeric_limits<double>::infinity();

    // Query the topological sorting of the underlying functionNode
    const auto &nodes = func->topologicalSortedRepresentationOfNodes;
    // Abort the longest path search early if no nodes exist
    if (nodes.empty()) {
        // This case should never occur in the field...
        return {};
    }

    // We iterate over the nodes and precalculate the energy of loopnodes from out clustered loop mapping
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        // Get the next node
        HLAC::GenericNode *node = func->topologicalSortedRepresentationOfNodes[i];

        // Check if its a loopnode
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode*>(node)) {
            // Get the loopnode from the mapping
            try {
                auto ln = loopMapping.at(loopNode);
                func->nodeEnergy[i] = ln.optimalValue;
            } catch (const std::out_of_range &e) {
                // If the loopnode could not be found in the mapping, perform a quick fallback and zero the value
                Logger::getInstance().log(
                    "Warning: LoopNode "
                    + loopNode->getDotName() + " not found in loop mapping, using 0.0 as energy value.",
                    LOGLEVEL::WARNING);
                func->nodeEnergy[i] = 0.0;
            }
        }
    }

    const std::size_t n = nodes.size();
    HLAC::GenericNode *start = nodes.front();

    // Query HLAC representation that can be used for DAG longest path search
    const auto &nodeToIndex = func->nodeLookup;
    const auto &nodeEnergy = func->nodeEnergy;
    const auto &adjacency = func->adjacencyRepresentation;

    // Create a distance vector that maps the index of the node to its calculated distance to the startnode
    // (= energy cost along the path)
    std::vector<double> distance(n, NEG_INF);

    // Create a vector that saves the parent node for each node, so we can trace the path
    std::vector<HLAC::GenericNode*> parent(n, nullptr);

    // Init the cost of the start node
    distance[nodeToIndex.at(start)] = start->getEnergy();

    // Iterate over the nodes in des graph
    for (std::size_t u = 0; u < n; ++u) {
        // Get the distance of the current node to the start node.
        // If it is negative infinity, we have not found a path to this
        const double du = distance[u];
        if (du == NEG_INF) {
            continue;
        }

        HLAC::GenericNode *uNode = nodes[u];

        // Check all adjacent nodes
        for (HLAC::Edge *edge : adjacency[u]) {
            // Ignore the edge if its not feasible
            if (!edge->feasibility) {
                continue;
            }

            // Find the destination node
            auto it = nodeToIndex.find(edge->destination);
            if (it == nodeToIndex.end()) {
                continue;
            }

            // Destination vertex
            const std::size_t v = it->second;
            // Compute the candidate energy for reaching node v via u.
            const double candidateEnergy = du + nodeEnergy[v];

            // Relaxation step: update if we found a better (higher energy) path.
            if (candidateEnergy > distance[v]) {
                distance[v] = candidateEnergy;
                parent[v] = uNode;
            }
        }
    }

    /// Conver the calculated distances and parents into the expected mapping format for the return type
    std::unordered_map<HLAC::GenericNode*, double> distanceMap;
    std::unordered_map<HLAC::GenericNode*, HLAC::GenericNode*> parentMap;
    distanceMap.reserve(n);
    parentMap.reserve(n);

    for (std::size_t i = 0; i < n; ++i) {
        distanceMap.emplace(nodes[i], distance[i]);
        parentMap.emplace(nodes[i], parent[i]);
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
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode*>(nodeUP.get())) {
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
        if (auto *innerLoop = dynamic_cast<HLAC::LoopNode*>(nodeUP.get())) {
            // Index the subloops edges
            nextIndex = assignEdgeIndicesLoop(innerLoop, nextIndex);
        }
    }

    // Return the last index for the recursive behavior
    return nextIndex;
}

void ILPUtil::buildIncidenceMaps(
    const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
    std::unordered_map<HLAC::GenericNode*, std::vector<int>> &incoming,
    std::unordered_map<HLAC::GenericNode*, std::vector<int>> &outgoing) {
    // Check that all incoming and outgoing vectors are empty
    incoming.clear();
    outgoing.clear();

    // Check each edge contained in this scope
    for (const auto &edgeUP : edges) {
        // Get the current edge
        auto *edge = edgeUP.get();

        if (!edge) {
            continue;
        }

        // Check that the index of the edge is valid
        if (edge->ilpIndex < 0) {
            Logger::getInstance().log(
                "Error: edge without valid ilpIndex encountered.",
                LOGLEVEL::ERROR);
            continue;
        }

        // Add the nodes to the respective vectors
        incoming[edge->destination].push_back(edge->ilpIndex);
        outgoing[edge->soure].push_back(edge->ilpIndex);
    }
}

void ILPUtil::insertUnique(
    CoinPackedVector &row,
    std::unordered_set<int> &used,
    int col,
    double coeff) {

    // Check that we do not add columns that are already contained in the vector
    if (!used.insert(col).second) {
        Logger::getInstance().log(
            "Warning: duplicate column " + std::to_string(col) + " while building row",
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
