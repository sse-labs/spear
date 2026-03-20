/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_ILPUTIL_H
#define SPEAR_ILPUTIL_H

#include <string>

#include "CoinFinite.hpp"
#include "CoinPackedMatrix.hpp"
#include "ILP/ILPBuilder.h"

class ILPUtil {
public:
    static void printILPModelHumanReadable(std::string funcname, std::string loopname, const ILPModel &model);

private:
    static std::string boundToString(double value);
    static std::string formatLinearExpr(const CoinPackedMatrix &matrix, int row);
};

#endif  // SPEAR_ILPUTIL_H