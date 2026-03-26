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
    static void printILPModelHumanReadable(std::string funcname, const ILPModel &model);

    static std::pair<std::unordered_map<HLAC::GenericNode*, double>, std::unordered_map<HLAC::GenericNode*, HLAC::GenericNode*>>
     longestPathDAG(HLAC::FunctionNode *func,
                   const std::unordered_map<HLAC::LoopNode *, std::pair<double, std::vector<double>>> &loopMapping);

private:
    static std::string boundToString(double value);
    static std::string formatLinearExpr(const CoinPackedMatrix &matrix, int row);

};

#endif  // SPEAR_ILPUTIL_H