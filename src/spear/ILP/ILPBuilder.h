
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_ILPBUILDER_H
#define SPEAR_ILPBUILDER_H

#include "CoinPackedVector.hpp"
#include "CoinFinite.hpp"
#include "CoinPackedMatrix.hpp"
#include "HLAC/hlac.h"

struct ILPModel {
    CoinPackedMatrix matrix;
    std::vector<double> row_lb;
    std::vector<double> row_ub;
    std::vector<double> col_lb;
    std::vector<double> col_ub;
    std::vector<double> obj;
};

class ILPBuilder {
 public:
    ILPBuilder();

    static ILPModel buildMonolithicILP(HLAC::FunctionNode *func);

    static ILPModel buildClusteredILP(HLAC::FunctionNode *func);
};

#endif  // SPEAR_ILPBUILDER_H
