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

std::pair<std::map<HLAC::GenericNode*, double>, std::map<HLAC::GenericNode*, HLAC::GenericNode*>>
    ILPUtil::longestPathDAG(HLAC::FunctionNode *func, std::map<HLAC::LoopNode *, double> loopMapping) {
    const double NEG_INF = -std::numeric_limits<double>::infinity();

    auto sortedNodes = func->getTopologicalOrdering();
    auto &edges = func->Edges;
    auto start = sortedNodes.front();  // Assuming the first node in topological order is the start node


    std::map<HLAC::GenericNode*, double> distance;
    std::map<HLAC::GenericNode*, HLAC::GenericNode*> parent;

    auto adjacentList = HLAC::Util::createAdjacentList(sortedNodes, edges);

    for (auto* node : sortedNodes) {
        distance[node] = NEG_INF;
        parent[node] = nullptr;
    }

    distance[start] = 0.0;

    for (const auto &node : sortedNodes) {
        if (distance[node] == NEG_INF) {
            continue;  // Unreachable node
        }

        for (const auto &edgeUP : adjacentList[node]) {
            // Check if the edge is feasible. If not ignore this possible path.
            // We assume here, that there is always another viable path...
            if (edgeUP->feasibility) {
                auto destinationNode = edgeUP->destination;
                double candidateEnergy = distance[node];

                // If we have a loopNode use the result from the given clustered solving
                if (auto candidateLoopNode = dynamic_cast<HLAC::LoopNode*>(destinationNode)) {
                    candidateEnergy += loopMapping[candidateLoopNode];
                } else {
                    candidateEnergy += destinationNode->getEnergy();
                }

                if (candidateEnergy > distance[destinationNode]) {
                    distance[destinationNode] = candidateEnergy;
                    parent[destinationNode] = node;
                }
            }
        }
    }

    return std::make_pair(distance, parent);
}
