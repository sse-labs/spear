/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "ILP/ILPUtil.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#include <iostream>

#include "HLAC/util.h"

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

    std::cout << "================ Clustered ILP Model - "<< funcname << "(" << loopname << ")" <<  " ================\n\n";

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

std::pair<std::unordered_map<HLAC::GenericNode*, double>,
          std::unordered_map<HLAC::GenericNode*, HLAC::GenericNode*>>
ILPUtil::longestPathDAG(
    HLAC::FunctionNode *func,
    const std::unordered_map<HLAC::LoopNode*, std::pair<double, std::vector<double>>> &loopMapping) {

    (void)loopMapping;

    const double NEG_INF = -std::numeric_limits<double>::infinity();

    const auto &nodes = func->topologicalSortedRepresentationOfNodes;
    if (nodes.empty()) {
        return {};
    }

    const std::size_t n = nodes.size();
    HLAC::GenericNode *start = nodes.front();

    const auto &nodeToIndex = func->nodeLookup;
    const auto &nodeEnergy = func->nodeEnergy;
    const auto &adjacency = func->adjacencyRepresentation;

    std::vector<double> distance(n, NEG_INF);
    std::vector<HLAC::GenericNode*> parent(n, nullptr);

    distance[nodeToIndex.at(start)] = 0.0;

    for (std::size_t u = 0; u < n; ++u) {
        const double du = distance[u];
        if (du == NEG_INF) {
            continue;
        }

        HLAC::GenericNode *uNode = nodes[u];

        for (HLAC::Edge *edge : adjacency[u]) {
            if (!edge->feasibility) {
                continue;
            }

            auto it = nodeToIndex.find(edge->destination);
            if (it == nodeToIndex.end()) {
                continue;
            }

            const std::size_t v = it->second;
            const double candidateEnergy = du + nodeEnergy[v];

            if (candidateEnergy > distance[v]) {
                distance[v] = candidateEnergy;
                parent[v] = uNode;
            }
        }
    }

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
