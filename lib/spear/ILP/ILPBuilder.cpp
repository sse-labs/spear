/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "ILP/ILPBuilder.h"
#include "ILP/ILPSolver.h"
#include  "ILP/ILPUtil.h"
#include "HLAC/hlac.h"

#include <unordered_set>

void ILPBuilder::applyEdgeFeasibilityBounds(ILPModel &model, HLAC::FunctionNode *func) {
    for (auto &edgeUP : func->Edges) {
        auto *edge = edgeUP.get();
        if (!edge) {
            continue;
        }

        const int col = edge->ilpIndex;
        if (col < 0 || col >= static_cast<int>(model.col_ub.size())) {
            std::cerr << "Warning: invalid ilpIndex while applying feasibility bound.\n";
            continue;
        }

        if (!edge->feasibility) {
            model.col_lb[col] = 0.0;
            model.col_ub[col] = 0.0;
        }
    }

    for (auto &nodeUP : func->Nodes) {
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(nodeUP.get())) {
            applyEdgeFeasibilityBounds(model, loopNode);
        }
    }
}

void ILPBuilder::applyEdgeFeasibilityBounds(ILPModel &model, HLAC::LoopNode *loopNode) {
    for (auto &edgeUP : loopNode->Edges) {
        auto *edge = edgeUP.get();
        if (!edge) {
            continue;
        }

        const int col = edge->ilpIndex;
        if (col < 0 || col >= static_cast<int>(model.col_ub.size())) {
            std::cerr << "Warning: invalid ilpIndex while applying feasibility bound.\n";
            continue;
        }

        if (!edge->feasibility) {
            model.col_lb[col] = 0.0;
            model.col_ub[col] = 0.0;
        }
    }

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

    // Print the mapping
    /*for (auto &e : edges) {
        std::cout << "Edge from " << e->soure->getDotName() << " to " << e->destination->getDotName()
                  << " with ILP index " << e->ilpIndex << std::endl;
    }*/

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


        for (int col : incomingEdgesPerNode[node]) {
            ILPUtil::insertUnique(row, usedCols, col, 1.0);
        }

        for (int col : outgoingEdgesPerNode[node]) {
            ILPUtil::insertUnique(row, usedCols, col, -1.0);
        }

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

void ILPBuilder::appendLoopBoundConstraint(ILPModel &model,HLAC::LoopNode *loopNode, const std::vector<int> &invocationCols) {
    CoinPackedVector row;
    std::unordered_set<int> usedCols;

    if (loopNode->backEdge == nullptr) {
        std::cerr << "Warning: Loop " << loopNode->getDotName()
                  << " has no backedge, skipping loop bound constraint.\n";
        return;
    }

    const int backCol = loopNode->backEdge->ilpIndex;
    const double lb = static_cast<double>(loopNode->bounds.getLowerBound());
    const double ub = static_cast<double>(loopNode->bounds.getUpperBound());

    // Upper bound:
    // x_back - ub * sum(invocations) <= 0
    {
        CoinPackedVector row;
        std::unordered_set<int> usedCols;

        ILPUtil::insertUnique(row, usedCols, backCol, 1.0);

        for (int col : invocationCols) {
            ILPUtil::insertUnique(row, usedCols, col, -ub);
        }

        ILPUtil::appendRow(model, row, -COIN_DBL_MAX, 0.0);
    }

    // Lower bound:
    // x_back - lb * sum(invocations) >= 0
    {
        CoinPackedVector row;
        std::unordered_set<int> usedCols;

        ILPUtil::insertUnique(row, usedCols, backCol, 1.0);

        for (int col : invocationCols) {
            ILPUtil::insertUnique(row, usedCols, col, -lb);
        }

        ILPUtil::appendRow(model, row, 0.0, COIN_DBL_MAX);
    }
}

void ILPBuilder::fillObjectiveFunction(ILPModel &model, HLAC::FunctionNode *func) {
    // For all edges in this functionnode set the objective vector values
    for (auto &edgeUP : func->Edges) {
        auto *edge = edgeUP.get();
        model.obj[edge->ilpIndex] = edge->destination->getEnergy();
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
    ILPSolver modelSolver(ilpModel);
    auto optimalSolution = modelSolver.getSolvedModelValue();
    auto optimalPath = modelSolver.getSolvedSolution();

    if (optimalPath.has_value() && optimalSolution.has_value()) {
        return std::make_optional<ILPResult>(optimalSolution.value(), optimalPath.value());
    }

    return std::nullopt;
}

void ILPBuilder::appendEqualityConstraint(ILPModel &model, int col) {
    CoinPackedVector row;
    std::unordered_set<int> usedCols;
    double value = 1.0;

    ILPUtil::insertUnique(row, usedCols, col, 1.0);
    ILPUtil::appendRow(model, row, value, value);
}

ILPModel ILPBuilder::buildMonolithicILP(HLAC::LoopNode *loop) {
    // std::cout << "Building ILP for function " << func->function->getName().str() << std::endl;

    // The loop is part of a function graph whose edges already carry stable global ids.
    // Therefore we must not renumber the loop edges locally here.
    const int maxEdgeIndex = ILPUtil::getMaxEdgeIndex(loop);
    const int invocationCol = maxEdgeIndex + 1;
    const int numVars = invocationCol + 1;

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

    // Append backedge simulation variable
    appendEqualityConstraint(model, invocationCol);

    // Fill objective recursively with energy cost
    fillObjectiveFunction(model, loop);

    return model;
}

ILPModel ILPBuilder::buildMonolithicILP(HLAC::FunctionNode *func) {
    // std::cout << "Building ILP for function " << func->function->getName().str() << std::endl;

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

    // ILPUtil::printILPModelHumanReadable(func->name, model);

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