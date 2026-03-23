/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_ILPBUILDER_H
#define SPEAR_ILPBUILDER_H

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CoinPackedMatrix.hpp"
#include "CoinPackedVector.hpp"

namespace HLAC {
class FunctionNode;
class LoopNode;
class Edge;
class GenericNode;
}  // namespace HLAC

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

    static std::map<HLAC::LoopNode *, ILPModel> buildClusteredILP(HLAC::FunctionNode *func);

    static std::optional<std::pair<double, std::vector<double>>> solveModel(ILPModel model);

 private:
    static int assignEdgeIndicesFunction(HLAC::FunctionNode *func, int nextIndex);
    static int assignEdgeIndicesFunction(HLAC::LoopNode *loop, int nextIndex);

     static int assignEdgeIndicesLoop(HLAC::LoopNode *loopNode, int nextIndex);

    static void buildIncidenceMaps(
        const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
        std::unordered_map<HLAC::GenericNode *, std::vector<int>> &incoming,
        std::unordered_map<HLAC::GenericNode *, std::vector<int>> &outgoing);

    static void appendRow(ILPModel &model, const CoinPackedVector &row, double lb, double ub);

    static void insertUnique(CoinPackedVector &row, std::unordered_set<int> &used, int col, double coeff,
                             const std::string &context);
    static void applyEdgeFeasibilityBounds(ILPModel &model, HLAC::FunctionNode *func);

    static void applyEdgeFeasibilityBounds(ILPModel &model, HLAC::LoopNode *loopNode);
    static void appendGraphConstraints(
        ILPModel &model,
        const std::vector<std::unique_ptr<HLAC::GenericNode>> &nodes,
        const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
        const std::vector<int> *invocationCols);

    static void appendLoopBoundConstraint(ILPModel &model,
                                          HLAC::LoopNode *loopNode,
                                          const std::vector<int> &invocationCols);

    static void fillObjectiveFunction(ILPModel &model, HLAC::FunctionNode *func);
    static void fillObjectiveFunction(ILPModel &model, HLAC::LoopNode *loopNode);

    static ILPModel appendLoopNodeContents(ILPModel model, HLAC::LoopNode *node);
    static void appendLoopBoundConstraint(ILPModel &model, HLAC::LoopNode *loopNode);

    static ILPModel buildMonolithicILP(HLAC::LoopNode *loop);
};

#endif  // SPEAR_ILPBUILDER_H