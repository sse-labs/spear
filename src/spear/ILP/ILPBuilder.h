
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_ILPBUILDER_H
#define SPEAR_ILPBUILDER_H

#include <unordered_set>


#include "CoinFinite.hpp"
#include "CoinPackedMatrix.hpp"
#include "CoinPackedVector.hpp"
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

    static int assignEdgeIndicesFunction(HLAC::FunctionNode *func, int nextIndex);

    static int assignEdgeIndicesLoop(HLAC::LoopNode *loopNode, int nextIndex);

    static void buildIncidenceMaps(const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
                             std::unordered_map<HLAC::GenericNode *, std::vector<int>> &incoming,
                             std::unordered_map<HLAC::GenericNode *, std::vector<int>> &outgoing);
    static void appendRow(ILPModel &model, const CoinPackedVector &row, double lb, double ub);
    static void insertUnique(CoinPackedVector &row, std::unordered_set<int> &used, int col, double coeff,
                      const std::string &context);
    static void appendGraphConstraints(ILPModel &model, const std::vector<std::unique_ptr<HLAC::GenericNode>> &nodes,
                                const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
                                const std::vector<int> *invocationCols);
    static void appendLoopBoundConstraint(ILPModel &model,
                               HLAC::LoopNode *loopNode,
                               const std::vector<int> &invocationCols);
    static void fillObjectiveFunction(ILPModel &model, HLAC::FunctionNode *func);
    static void fillObjectiveFunction(ILPModel &model, HLAC::LoopNode *loopNode);

    static ILPModel buildMonolithicILP(HLAC::FunctionNode *func);

    static ILPModel buildClusteredILP(HLAC::FunctionNode *func);

    static std::optional<std::pair<double, std::vector<double>>> solveModel(ILPModel model);

    static ILPModel appendLoopNodeContents(ILPModel model, HLAC::LoopNode *node);
};

#endif  // SPEAR_ILPBUILDER_H
