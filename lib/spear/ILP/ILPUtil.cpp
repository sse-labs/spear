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

void ILPUtil::printILPModelHumanReadable(std::string funcname, std::string loopname, const ILPModel &model) {
    const CoinPackedMatrix &matrix = model.matrix;

    const int numRows = matrix.getNumRows();
    const int numCols = matrix.getNumCols();

    std::cout << "================ ILP Model - "<< funcname << "(" << loopname << ")" <<  " ================\n\n";

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