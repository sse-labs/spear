
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_ILPDEBUG_H
#define SPEAR_ILPDEBUG_H

/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef ILP_ILPDEBUG_H
#define ILP_ILPDEBUG_H

#include <memory>
#include <string>
#include <vector>

#include <coin/CoinPackedMatrix.hpp>
#include <coin/CoinPackedVector.hpp>

#include "HLAC/hlac.h"

class ILPDebug {
public:
    static std::string basicBlockToDebugString(const llvm::BasicBlock *basicBlock);
    static std::string genericNodeToDebugString(const HLAC::GenericNode *genericNode);
    static std::string debugGenericNodeToString(const HLAC::GenericNode *genericNode);
    static std::string edgeToDebugString(const HLAC::Edge *edge);
    static std::string debugEdgeToString(const HLAC::Edge *edge);
    static std::string integerVectorToString(const std::vector<int> &values);
    static std::string debugRowToString(
        const CoinPackedVector &row,
        double lowerBound,
        double upperBound,
        const std::vector<std::unique_ptr<HLAC::Edge>> &edges);
    static std::string formatBound(double boundValue);
    static std::string formatCoefficient(double coefficientValue);

    static const HLAC::Edge *findEdgeByIlpIndex(
        const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
        int ilpIndex);

    static void dumpILPModel(
        const ILPModel &model,
        const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
        const std::string &name);
};

#endif

#endif //SPEAR_ILPDEBUG_H
