/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <unordered_set>
#include <utility>
#include <unordered_map>
#include <vector>
#include <iostream>

#include "ILP/ILPBuilder.h"

#include "HLAC/hlac.h"
#include "ILP/ILPSolver.h"
#include "ILP/ILPUtil.h"
#include "Logger.h"
#include "PassUtil.h"

void ILPBuilder::applyEdgeFeasibilityBounds(ILPModel &model, HLAC::FunctionNode *func) {
    // Iterate over the edges in the function node
    for (auto &edgeUP : func->Edges) {
        auto *edge = edgeUP.get();
        if (!edge) {
            continue;
        }

        // Check that the currently viewed edge has a valid ILPIndex
        const int col = edge->ilpIndex;
        if (col < 0 || col >= static_cast<int>(model.col_ub.size())) {
            Logger::getInstance().log(
                "Warning: invalid ilpIndex while applying feasibility bound.",
                LOGLEVEL::ERROR);
            // If the index is invalid, throw everything against the wall and ignore the edge...
            continue;
        }

        // If the edge is not feasible
        if (!edge->feasibility) {
            // We limit the value domain of the corresponding column to 0.0 <= x_i <= 0.0 to restrict the usage of this
            // edge entirely
            model.col_lb[col] = 0.0;
            model.col_ub[col] = 0.0;
        }
    }

    // Apply the feasibility constrain creation to all sub-LoopNodes
    for (auto &nodeUP : func->Nodes) {
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(nodeUP.get())) {
            applyEdgeFeasibilityBounds(model, loopNode);
        }
    }
}

void ILPBuilder::applyEdgeFeasibilityBounds(ILPModel &model, HLAC::LoopNode *loopNode) {
    // Iterate over the edges in the loop node
    for (auto &edgeUP : loopNode->Edges) {
        auto *edge = edgeUP.get();
        if (!edge) {
            continue;
        }

        // Check that the currently viewed edge has a valid ILPIndex
        const int col = edge->ilpIndex;
        if (col < 0 || col >= static_cast<int>(model.col_ub.size())) {
            Logger::getInstance().log(
                "Warning: invalid ilpIndex while applying feasibility bound.",
                LOGLEVEL::ERROR);
            continue;
            // If the index is invalid, throw everything against the wall and ignore the edge...
        }

        // If the edge is not feasible
        if (!edge->feasibility) {
            // We limit the value domain of the corresponding column to 0.0 <= x_i <= 0.0 to restrict the usage of this
            // edge entirely
            model.col_lb[col] = 0.0;
            model.col_ub[col] = 0.0;
        }
    }

    // Apply the feasibility constrain creation to all sub-LoopNodes
    for (auto &nodeUP : loopNode->Nodes) {
        if (auto *innerLoop = dynamic_cast<HLAC::LoopNode *>(nodeUP.get())) {
            applyEdgeFeasibilityBounds(model, innerLoop);
        }
    }
}

void ILPBuilder::appendGraphConstraints(
    ILPModel &model,
    const std::vector<std::unique_ptr<HLAC::GenericNode>> &nodes,
    const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
    const std::vector<int> *invocationCols) {
    std::unordered_map<HLAC::GenericNode*, std::vector<int>> incomingEdgesPerNode;
    std::unordered_map<HLAC::GenericNode*, std::vector<int>> outgoingEdgesPerNode;

    // Build the indices for all contained edges
    ILPUtil::buildIncidenceMaps(edges, incomingEdgesPerNode, outgoingEdgesPerNode);

    /**
     * We need to add constraints for all nodes in this scope.
     * For each node we need to add a flow constraint that ensures that the number of incoming edges
     * equals the number of outgoing edges.
     *
     */
    for (const auto &nodeUP : nodes) {
        auto *node = nodeUP.get();
        CoinPackedVector row;
        std::unordered_set<int> usedCols;

        /**
         * Additionally, we need to handle our virtual nodes that represent entry and exit points of functions and loops.
         * For these nodes, we need to add the constraint that they will be executed at least once.
         *
         */
        if (auto *virtualNode = dynamic_cast<HLAC::VirtualNode*>(node)) {
            if (virtualNode->isEntry) {
                for (int col : outgoingEdgesPerNode[node]) {
                    ILPUtil::insertUnique(row, usedCols, col, 1.0);
                }

                if (invocationCols == nullptr) {
                    // Top-level function entry: exactly one entry
                    ILPUtil::appendRow(model, row, 1.0, 1.0);
                } else {
                    // Loop-internal entry: equals outer loop invocation count
                    for (int col : *invocationCols) {
                        ILPUtil::insertUnique(row, usedCols, col, -1.0);
                    }
                    ILPUtil::appendRow(model, row, 0.0, 0.0);
                }

                continue;
            }

            if (virtualNode->isExit) {
                for (int col : incomingEdgesPerNode[node]) {
                    ILPUtil::insertUnique(row, usedCols, col, 1.0);
                }

                if (invocationCols == nullptr) {
                    // Top-level function exit: exactly one completed path
                    ILPUtil::appendRow(model, row, 1.0, 1.0);
                } else {
                    // Loop-internal exit: equals outer loop invocation count
                    for (int col : *invocationCols) {
                        ILPUtil::insertUnique(row, usedCols, col, -1.0);
                    }
                    ILPUtil::appendRow(model, row, 0.0, 0.0);
                }

                continue;
            }
        }

        /**
         * Create the constrain for incoming and outgoing edges
         * Set the coefficient in the matrix for each incoming edge to 1
         * and for each outgoing edge to -1. This originates from transforming the following term
         *
         * e.g
         *
         *      x_1 + x_2 = x_3 + x_4
         * <=>  x_1 + x_2 - x_3 - x_4 = 0
         *
         *
         */
        for (int col : incomingEdgesPerNode[node]) {
            ILPUtil::insertUnique(row, usedCols, col, 1.0);
        }

        for (int col : outgoingEdgesPerNode[node]) {
            ILPUtil::insertUnique(row, usedCols, col, -1.0);
        }

        // Each edge constraint has to equal 0, as the amount of incoming flow has to equal the amount of outgoing flow
        ILPUtil::appendRow(model, row, 0.0, 0.0);


        if (auto *loopNode = dynamic_cast<HLAC::LoopNode*>(node)) {
            const std::vector<int> outerIncoming = incomingEdgesPerNode[node];

            // Add all internal flow constraints of the loop.
            appendGraphConstraints(model, loopNode->Nodes, loopNode->Edges, &outerIncoming);

            // Add bound constraint for the loop.
            appendLoopBoundConstraint(model, loopNode, outerIncoming);
        }
    }
}

void ILPBuilder::appendLoopBoundConstraint(
    ILPModel &model, HLAC::LoopNode *loopNode,
    const std::vector<int> &invocationCols) {
    // Create an empty row
    CoinPackedVector row;
    // Where we store the columns used by the loop
    std::unordered_set<int> usedCols;

    if (loopNode->backEdge == nullptr) {
        Logger::getInstance().log(
            "Warning: Loop " + loopNode->getDotName() + " has no backedge, skipping loop bound constraint.",
            LOGLEVEL::ERROR);
        return;
    }

    const int backCol = loopNode->backEdge->ilpIndex;
    const auto lb = static_cast<double>(loopNode->bounds.getLowerBound());
    const auto ub = static_cast<double>(loopNode->bounds.getUpperBound());

    // Upper bound:
    // x_back - ub * sum(invocations) <= 0
    CoinPackedVector upperBoundRow;
    std::unordered_set<int> upperBoundUsedCols;

    ILPUtil::insertUnique(upperBoundRow, upperBoundUsedCols, backCol, 1.0);

    for (int col : invocationCols) {
        ILPUtil::insertUnique(upperBoundRow, upperBoundUsedCols, col, -ub);
    }

    ILPUtil::appendRow(model, upperBoundRow, -COIN_DBL_MAX, 0.0);

    // Lower bound:
    // x_back - lb * sum(invocations) >= 0
    CoinPackedVector lowerBoundRow;
    std::unordered_set<int> lowerBoundUsedCols;

    ILPUtil::insertUnique(lowerBoundRow, lowerBoundUsedCols, backCol, 1.0);

    for (int col : invocationCols) {
        ILPUtil::insertUnique(lowerBoundRow, lowerBoundUsedCols, col, -lb);
    }

    ILPUtil::appendRow(model, lowerBoundRow, 0.0, COIN_DBL_MAX);
}

void ILPBuilder::fillObjectiveFunction(ILPModel &model, HLAC::FunctionNode *func) {
    // For all edges in this functionnode set the objective vector values
    for (auto &edgeUP : func->Edges) {
        auto *edge = edgeUP.get();

        auto indexIterator = func->nodeLookup.find(edge->destination);
        if (indexIterator == func->nodeLookup.end()) {
            Logger::getInstance().log(
                "Warning: edge with destination node not found in node lookup while filling objective function.",
                LOGLEVEL::ERROR);
            continue;
        }

        model.obj[edge->ilpIndex] = func->nodeEnergy[indexIterator->second];
    }

    // Then we need to check all contained loopnodes
    for (auto &nodeUP : func->Nodes) {
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode*>(nodeUP.get())) {
            // Fill the objective function for all loopnodes contained in the function
            fillObjectiveFunction(model, loopNode);
        }
    }
}

void ILPBuilder::fillObjectiveFunction(ILPModel &model, HLAC::LoopNode *loopNode) {
    // For all edges in the loopnode set the objective vector values
    for (auto &edgeUP : loopNode->Edges) {
        auto *edge = edgeUP.get();
        model.obj[edge->ilpIndex] = edge->destination->getEnergy();
    }

    // Check all contained loopnodes recursively
    for (auto &nodeUP : loopNode->Nodes) {
        if (auto *innerLoop = dynamic_cast<HLAC::LoopNode*>(nodeUP.get())) {
            fillObjectiveFunction(model, innerLoop);
        }
    }
}


std::optional<ILPResult> ILPBuilder::solveModel(ILPModel ilpModel) {
    // Create a new solver on the model
    ILPSolver modelSolver(std::move(ilpModel));

    // Solve the model and query optimal solution and path
    auto optimalSolution = modelSolver.getSolvedModelValue();
    auto optimalPath = modelSolver.getSolvedSolution();

    // Validate that the solver found a solution...
    if (optimalPath.has_value() && optimalSolution.has_value()) {
        return std::make_optional<ILPResult>(optimalSolution.value(), optimalPath.value());
    }

    return std::nullopt;
}

void ILPBuilder::appendEqualityConstraint(ILPModel &model, int col) {
    CoinPackedVector row;
    std::unordered_set<int> usedCols;
    double value = 1.0;

    // Add a simulated constrain that enforces the entry edge in the respective loopnode to be executed once
    // This is needed for clustered ILP solving where we need to assume that the loopnode is being executed
    ILPUtil::insertUnique(row, usedCols, col, 1.0);
    ILPUtil::appendRow(model, row, value, value);
}

ILPModel ILPBuilder::buildMonolithicILP(HLAC::LoopNode *loop) {
    // The loop is part of a function graph whose edges already carry stable global ids.
    // Therefore we must not renumber the loop edges locally here.
    const int maxEdgeIndex = ILPUtil::getMaxEdgeIndex(loop);
    const int invocationCol = maxEdgeIndex + 1;
    const int numVars = invocationCol + 1;

    // We assume that the variable entrying this loop is invocationCol
    const std::vector<int> invocationCols = {invocationCol};

    // Create empty model storage.
    ILPModel model{
        .matrix = CoinPackedMatrix(false, 0, 0),
        .row_lb = {},
        .row_ub = {},
        .col_lb = std::vector<double>(numVars, 0.0),
        .col_ub = std::vector<double>(numVars, COIN_DBL_MAX),
        .obj = std::vector<double>(numVars, 0.0)
    };

    // Encode edge feasibility
    applyEdgeFeasibilityBounds(model, loop);

    // Append all flow and loop constraints recursively.
    appendGraphConstraints(model, loop->Nodes, loop->Edges, &invocationCols);

    // As we are already in a loop, we need to append all loop bound constrains right here
    // We calculate loop constrains via the scale of how often the loopnode will be entered,
    // This works perfectly fine for monolithic ILP calculation, where all constrains exit.
    // However, for clustered ILP construction, we have to assume that the top level loop(and only the top level loop)
    // Will be entered exactly one time.
    appendLoopBoundConstraint(model, loop, invocationCols);

    // Loop entry simulation variable
    appendEqualityConstraint(model, invocationCol);

    // Fill objective recursively with energy cost
    fillObjectiveFunction(model, loop);

    return model;
}

ILPModel ILPBuilder::buildMonolithicILP(HLAC::FunctionNode *func) {
    // Assign global ILP column indices to every edge recursively.
    const int numVars = ILPUtil::assignEdgeIndicesFunction(func, 0);

    // Create empty model storage.
    ILPModel model{
        .matrix = CoinPackedMatrix(false, 0, 0),
        .row_lb = {},
        .row_ub = {},
        .col_lb = std::vector<double>(numVars, 0.0),
        .col_ub = std::vector<double>(numVars, COIN_DBL_MAX),
        .obj = std::vector<double>(numVars, 0.0)
    };

    // Encode edge feasibility
    applyEdgeFeasibilityBounds(model, func);

    // Append all flow and loop constraints recursively.
    appendGraphConstraints(model, func->Nodes, func->Edges, nullptr);

    // Fill objective recursively with energy cost
    fillObjectiveFunction(model, func);

    return model;
}

std::unordered_map<HLAC::LoopNode *, ILPModel> ILPBuilder::buildClusteredILP(HLAC::FunctionNode *func) {
    /**
     * In contrary to the monolithic ILP, we do not build one big ILP for the whole function,
     * but instead we build separate ILPs for each loop in the function and use a generic graph algorithm for finding the
     * worst path in the function
     */
    std::unordered_map<HLAC::LoopNode *, ILPModel> resultMapping;

    // Assign stable global ids once for the complete function graph.
    ILPUtil::assignEdgeIndicesFunction(func, 0);

    for (auto &nodeUP : func->Nodes) {
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode*>(nodeUP.get())) {
            // Build the ILP for the loop node
            ILPModel loopModel = buildMonolithicILP(loopNode);

            // ILPUtil::printILPModelHumanReadable(func->name, loopNode->loop->getName().str(), loopModel);

            // Store the model in the result mapping
            resultMapping[loopNode] = loopModel;
        }
    }

    return resultMapping;
}
