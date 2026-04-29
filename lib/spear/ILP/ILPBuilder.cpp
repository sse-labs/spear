/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <cmath>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <algorithm>

#include "ILP/ILPBuilder.h"

#include "HLAC/hlac.h"
#include "ILP/ILPDebug.h"
#include "ILP/ILPSolver.h"
#include "ILP/ILPUtil.h"
#include "Logger.h"
#include "PassUtil.h"

void ILPBuilder::applyEdgeFeasibilityBounds(ILPModel &model, HLAC::FunctionNode *func) {
    for (auto &edgeUP : func->Edges) {
        auto *edge = edgeUP.get();
        if (!edge) {
            continue;
        }

        const int column = edge->ilpIndex;
        if (column < 0 || column >= static_cast<int>(model.col_ub.size())) {
            Logger::getInstance().log("Warning: invalid ilpIndex while applying feasibility bound.", LOGLEVEL::ERROR);
            continue;
        }

        // If the edge is infeasible we enforce the respective variable to be 0 by setting
        // its upper and lower bound to 0
        if (!edge->feasibility) {
            model.col_lb[column] = 0.0;
            model.col_ub[column] = 0.0;
        }
    }

    // Call the function recursively for contained loop nodes
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

        const int column = edge->ilpIndex;
        if (column < 0 || column >= static_cast<int>(model.col_ub.size())) {
            Logger::getInstance().log("Warning: invalid ilpIndex while applying feasibility bound.", LOGLEVEL::ERROR);
            continue;
        }

        // If the edge is infeasible we enforce the respective variable to be 0 by setting
        // its upper and lower bound to 0
        if (!edge->feasibility) {
            model.col_lb[column] = 0.0;
            model.col_ub[column] = 0.0;
        }
    }

    // Call the function recursively for contained loop nodes
    for (auto &nodeUP : loopNode->Nodes) {
        if (auto *innerLoop = dynamic_cast<HLAC::LoopNode *>(nodeUP.get())) {
            applyEdgeFeasibilityBounds(model, innerLoop);
        }
    }
}

void ILPBuilder::appendGraphConstraints(ILPModel &model, const std::vector<std::unique_ptr<HLAC::GenericNode>> &nodes,
                                        const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
                                        const std::vector<int> *invocationCols) {
    // Create the incoming and outgoing edge mappings
    std::unordered_map<HLAC::GenericNode *, std::vector<int>> incomingEdgesPerNode;
    std::unordered_map<HLAC::GenericNode *, std::vector<int>> outgoingEdgesPerNode;

    // Calculate the incoming and outgoing mappins
    ILPUtil::buildIncidenceMaps(edges, incomingEdgesPerNode, outgoingEdgesPerNode,
                                static_cast<int>(model.col_lb.size()));

    // For each node in the considered scope...
    for (const auto &nodeUP : nodes) {
        auto *node = nodeUP.get();

        const std::vector<int> incomingEdges =
            incomingEdgesPerNode.contains(node) ? incomingEdgesPerNode[node] : std::vector<int>{};

        const std::vector<int> outgoingEdges =
                outgoingEdgesPerNode.contains(node) ? outgoingEdgesPerNode[node] : std::vector<int>{};

        // Handle virtual nodes
        if (auto *virtualNode = dynamic_cast<HLAC::VirtualNode *>(node)) {
            /**
             * For entry nodes we want to ensure that outgoing edges are called exactly one time
             */
            if (virtualNode->isEntry) {
                std::unordered_map<int, double> coefficientsByColumn;

                // Calculate the coefficient of the outgoing edges
                for (int column : outgoingEdges) {
                    ILPUtil::insertOrAccumulate(coefficientsByColumn, column, 1.0);
                }

                if (invocationCols == nullptr) {
                    // Case that entry node is not used for loop invocation, e.g. in the function entry node or in a
                    // loop without external invocation
                    CoinPackedVector row = ILPUtil::createRowFromCoefficients(coefficientsByColumn);
                    ILPUtil::appendRow(model, row, 1.0, 1.0);
                } else {
                    // Case that the entry node is called multiple times
                    for (int column : *invocationCols) {
                        ILPUtil::insertOrAccumulate(coefficientsByColumn, column, -1.0);
                    }

                    CoinPackedVector row = ILPUtil::createRowFromCoefficients(coefficientsByColumn);
                    ILPUtil::appendRow(model, row, 0.0, 0.0);
                }

                continue;
            }

            /**
             * For exit nodes we want to ensure that incoming edges are called exactly one time
             */
            if (virtualNode->isExit) {
                std::unordered_map<int, double> coefficientsByColumn;

                for (int column : incomingEdges) {
                    ILPUtil::insertOrAccumulate(coefficientsByColumn, column, 1.0);
                }

                if (invocationCols == nullptr) {
                    // Case that exit node is not used for loop invocation, e.g. in the function exit node or in a
                    // loop without external invocation
                    CoinPackedVector row = ILPUtil::createRowFromCoefficients(coefficientsByColumn);
                    ILPUtil::appendRow(model, row, 1.0, 1.0);
                } else {
                    // Case that the exit node is called multiple times
                    for (int column : *invocationCols) {
                        ILPUtil::insertOrAccumulate(coefficientsByColumn, column, -1.0);
                    }

                    CoinPackedVector row = ILPUtil::createRowFromCoefficients(coefficientsByColumn);
                    ILPUtil::appendRow(model, row, 0.0, 0.0);
                }

                continue;
            }
        }

        /**
         * For any other node we have to ensure that the amount of incoming edges is equal to the amount of
         * outgoing edges
         * e.g          x_1 + x_2 = x_3 + x_4
         * Which is expressed in cbc via
         *              x_1 + x_2 - x_3 - x_4 = 0
         */
        std::unordered_map<int, double> coefficientsByColumn;

        for (int column : incomingEdges) {
            ILPUtil::insertOrAccumulate(coefficientsByColumn, column, 1.0);
        }

        for (int column : outgoingEdges) {
            ILPUtil::insertOrAccumulate(coefficientsByColumn, column, -1.0);
        }

        CoinPackedVector row = ILPUtil::createRowFromCoefficients(coefficientsByColumn);
        ILPUtil::appendRow(model, row, 0.0, 0.0);

        /**
         * For loops we have to create seperate graph constrains for the contained graph
         * additionally we append loop bound constrains
         */
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(node)) {
            const std::vector<int> incomingColumns =
                    incomingEdgesPerNode.contains(node) ? incomingEdgesPerNode[node] : std::vector<int>{};

            const std::vector<int> invocationColumns =
                    collectExternalLoopInvocationColumns(loopNode, edges, incomingColumns);

            if (invocationColumns.empty()) {
                Logger::getInstance().log("Loop invocation debug: no external invocation columns for loop " +
                                                  loopNode->getDotName(),
                                          LOGLEVEL::ERROR);
            }

            appendGraphConstraints(model, loopNode->Nodes, loopNode->Edges, &invocationColumns);
            appendLoopBoundConstraint(model, loopNode, invocationColumns);
        }
    }
}

std::optional<ILPResult> ILPBuilder::solveClusteredLoopModel(const ILPModel &ilpModel, HLAC::LoopNode *loopNode) {
    // Create a new solver
    ILPSolver modelSolver(ilpModel);

    // Get the optimal solution and path
    auto optimalSolution = modelSolver.getSolvedModelValue();
    auto optimalPath = modelSolver.getSolvedSolution();

    // If solution and path exist return it
    if (optimalPath.has_value() && optimalSolution.has_value()) {
        return std::make_optional<ILPResult>(optimalSolution.value(), optimalPath.value());
    }

    // Otherwise evaluate the reason no result was generated
    const ILPSolverStatus solverStatus = modelSolver.getStatus();

    if (solverStatus == ILPSolverStatus::INFEASIBLE) {
        Logger::getInstance().log(
                "Clustered ILP: loop " + loopNode->getDotName() +
                        " is infeasible under feasibility constraints. Treating it as unreachable with energy 0.0.",
                LOGLEVEL::WARNING);

        return std::make_optional<ILPResult>(0.0, std::vector<double>(ilpModel.matrix.getNumCols(), 0.0));
    }

    Logger::getInstance().log("Clustered ILP failed for loop " + loopNode->getDotName() + " with status " +
                                      modelSolver.getStatusString(),
                              LOGLEVEL::ERROR);

    // Eventually return a nullopt if the solver fails
    return std::nullopt;
}

void ILPBuilder::appendLoopBoundConstraint(ILPModel &model, HLAC::LoopNode *loopNode,
                                           const std::vector<int> &invocationCols) {
    if (loopNode == nullptr) {
        Logger::getInstance().log("Loop bound debug: loopNode is null.", LOGLEVEL::ERROR);
        return;
    }

    // If the loop has multiple invocation columns we cannot calculate the value accordingly.
    if (invocationCols.size() != 1) {
        Logger::getInstance().log("Loop bound fallback: loop " + loopNode->getDotName() + " has " +
                                          std::to_string(invocationCols.size()) +
                                          " invocation columns, expected exactly one.",
                                  LOGLEVEL::WARNING);
        return;
    }

    // Query the bounds of the loop
    const double lowerBoundAsDouble = static_cast<double>(loopNode->bounds.getLowerBound());
    const double upperBoundAsDouble = static_cast<double>(loopNode->bounds.getUpperBound());

    // Calculate the times the backedges of the loop will be executed. (Bound - 1)
    const double lowerBackedgeFactor = std::max(0.0, lowerBoundAsDouble - 1.0);
    const double upperBackedgeFactor = std::max(0.0, upperBoundAsDouble - 1.0);

    // Get the invocation column
    const int invocationColumn = invocationCols.front();
    std::unordered_map<int, double> backedgeCoefficientsByColumn;

    // Iterate over the backedges in the loopnode
    for (HLAC::Edge *backEdge : loopNode->backEdges) {
        if (backEdge == nullptr) {
            continue;
        }

        if (backEdge->ilpIndex < 0 || backEdge->ilpIndex >= static_cast<int>(model.col_lb.size())) {
            Logger::getInstance().log("Loop bound debug: invalid backedge ilpIndex in loop " + loopNode->getDotName(),
                                      LOGLEVEL::ERROR);
            continue;
        }

        // Insert the backedge coefficient into the map with a factor of 1.0 as we want to express that the backedge is
        // executed once per loop iteration
        ILPUtil::insertOrAccumulate(backedgeCoefficientsByColumn, backEdge->ilpIndex, 1.0);
    }

    if (backedgeCoefficientsByColumn.empty()) {
        Logger::getInstance().log("Loop bound debug: loop " + loopNode->getDotName() + " has no valid backedges.",
                                  LOGLEVEL::ERROR);
        return;
    }

    /**
     * Insert a constraint that enforces that the backedge variables is called at least lowerBackedgeFactor times
     */
    std::unordered_map<int, double> lowerBoundCoefficientsByColumn = backedgeCoefficientsByColumn;
    ILPUtil::insertOrAccumulate(lowerBoundCoefficientsByColumn, invocationColumn, -lowerBackedgeFactor);
    CoinPackedVector lowerBoundRow = ILPUtil::createRowFromCoefficients(lowerBoundCoefficientsByColumn);
    ILPUtil::appendRow(model, lowerBoundRow, 0.0, COIN_DBL_MAX);

    /**
     * Insert a constraint that enforces that the backedge variables is called at most upperBackedgeFactor times
     */
    std::unordered_map<int, double> upperBoundCoefficientsByColumn = backedgeCoefficientsByColumn;
    ILPUtil::insertOrAccumulate(upperBoundCoefficientsByColumn, invocationColumn, -upperBackedgeFactor);
    CoinPackedVector upperBoundRow = ILPUtil::createRowFromCoefficients(upperBoundCoefficientsByColumn);
    ILPUtil::appendRow(model, upperBoundRow, -COIN_DBL_MAX, 0.0);
}

void ILPBuilder::fillObjectiveFunction(ILPModel &model, HLAC::FunctionNode *func) {
    // For each edge
    for (auto &edgeUP : func->Edges) {
        auto *edge = edgeUP.get();

        // Search for the edge destination in our direct node cache.
        // If the value is not found calculate it
        auto cacheIterator = func->directNodeEnergyCache.find(edge->destination);
        if (cacheIterator != func->directNodeEnergyCache.end()) {
            model.obj[edge->ilpIndex] = cacheIterator->second;
        } else {
            model.obj[edge->ilpIndex] = edge->destination->getEnergy();
        }
    }

    // Call the fillObjectiveFunction recursively for contained loop nodes
    for (auto &nodeUP : func->Nodes) {
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(nodeUP.get())) {
            fillObjectiveFunction(model, loopNode);
        }
    }
}

void ILPBuilder::fillObjectiveFunction(ILPModel &model, HLAC::LoopNode *loopNode) {
    for (auto &edgeUP : loopNode->Edges) {
        auto *edge = edgeUP.get();
        model.obj[edge->ilpIndex] = edge->destination->getEnergy();
    }

    // Call the fillObjectiveFunction recursively for contained loop nodes
    for (auto &nodeUP : loopNode->Nodes) {
        if (auto *innerLoop = dynamic_cast<HLAC::LoopNode *>(nodeUP.get())) {
            fillObjectiveFunction(model, innerLoop);
        }
    }
}

std::optional<ILPResult> ILPBuilder::solveModel(const ILPModel &ilpModel) {
    // Create a new solver
    ILPSolver modelSolver(ilpModel);

    // Get the optimal solution and path
    auto optimalSolution = modelSolver.getSolvedModelValue();
    auto optimalPath = modelSolver.getSolvedSolution();

    // If solution and path exist return it
    if (optimalPath.has_value() && optimalSolution.has_value()) {
        return std::make_optional<ILPResult>(optimalSolution.value(), optimalPath.value());
    }

    // Otherwise evaluate the reason no result was generated
    auto solverStatus = modelSolver.getStatus();
    auto statusString = modelSolver.getStatusString();

    std::cout << "[ILP ERROR] Solver failed.\n";
    std::cout << "  Status: " << statusString << "\n";
    std::cout << "  Variables: " << ilpModel.matrix.getNumCols() << "\n";
    std::cout << "  Constraints: " << ilpModel.matrix.getNumRows() << "\n";

    if (solverStatus == ILPSolverStatus::INFEASIBLE) {
        std::cout << "  Reason: Model is infeasible (constraints contradict each other).\n";
    } else if (solverStatus == ILPSolverStatus::UNBOUNDED) {
        std::cout << "  Reason: Model is unbounded (objective can grow indefinitely).\n";
    } else if (solverStatus == ILPSolverStatus::TIME_LIMIT) {
        std::cout << "  Reason: Solver hit time limit.\n";
    } else if (solverStatus == ILPSolverStatus::NUMERICAL_ISSUES) {
        std::cout << "  Reason: Numerical instability detected.\n";
    } else {
        std::cerr << "  Reason: Unknown solver failure.\n";
    }

    return std::nullopt;
}

void ILPBuilder::appendEqualityConstraint(ILPModel &model, int column) {
    /**
     * Create a constrain that limits the given column variable to be executed exactly once
     */
    std::unordered_map<int, double> coefficientsByColumn;
    ILPUtil::insertOrAccumulate(coefficientsByColumn, column, 1.0);

    CoinPackedVector row = ILPUtil::createRowFromCoefficients(coefficientsByColumn);
    ILPUtil::appendRow(model, row, 1.0, 1.0);
}

ILPModel ILPBuilder::buildMonolithicILP(HLAC::LoopNode *loop) {
    // Find the max edge index
    const int maxEdgeIndex = ILPUtil::getMaxEdgeIndex(loop);

    // Construct the invocation column index
    const int invocationColumn = maxEdgeIndex + 1;
    const int variableCount = invocationColumn + 1;

    const std::vector<int> invocationColumns = {invocationColumn};

    // Construct the ILPModel for solving
    ILPModel model{.matrix = CoinPackedMatrix(false, 0, 0),
                   .row_lb = {},
                   .row_ub = {},
                   .col_lb = std::vector<double>(variableCount, 0.0),
                   .col_ub = std::vector<double>(variableCount, COIN_DBL_MAX),
                   .obj = std::vector<double>(variableCount, 0.0)};

    // Apply feasibility constrains
    applyEdgeFeasibilityBounds(model, loop);

    // Append graph constrains
    appendGraphConstraints(model, loop->Nodes, loop->Edges, &invocationColumns);

    // Append loop bound constrains
    appendLoopBoundConstraint(model, loop, invocationColumns);

    // Append variable constraints so each variable is called once
    appendEqualityConstraint(model, invocationColumn);

    // Fill the objective function with energy values
    fillObjectiveFunction(model, loop);

    return model;
}

std::vector<int>
ILPBuilder::collectExternalLoopInvocationColumns(HLAC::LoopNode *loopNode,
                                                 const std::vector<std::unique_ptr<HLAC::Edge>> &parentEdges,
                                                 const std::vector<int> &incomingColumns) {
    std::vector<int> invocationColumns;

    // Iterate over the incoming columns
    for (int incomingColumn : incomingColumns) {
        HLAC::Edge *matchingEdge = nullptr;

        // Find the parent edge corresponding to the incoming column
        for (const auto &edgeUniquePointer : parentEdges) {
            HLAC::Edge *edge = edgeUniquePointer.get();

            if (edge != nullptr && edge->ilpIndex == incomingColumn) {
                matchingEdge = edge;
                break;
            }
        }

        // Validate the edge
        if (matchingEdge == nullptr) {
            continue;
        }

        // Ignore the edge if the destination is not our loopnode
        if (matchingEdge->destination != loopNode) {
            continue;
        }

        // Ignore the edge if the source is our loopnode
        if (matchingEdge->soure == loopNode) {
            continue;
        }

        // In any other case push the column into the invocation columns vector as it represents an external
        // invocation of the loop
        invocationColumns.push_back(incomingColumn);
    }

    return invocationColumns;
}

ILPModel ILPBuilder::buildMonolithicILP(HLAC::FunctionNode *func) {
    const int variableCount = ILPUtil::assignEdgeIndicesFunction(func, 0);

    // Construct the ILPModel for solving
    ILPModel model{.matrix = CoinPackedMatrix(false, 0, 0),
                   .row_lb = {},
                   .row_ub = {},
                   .col_lb = std::vector<double>(variableCount, 0.0),
                   .col_ub = std::vector<double>(variableCount, COIN_DBL_MAX),
                   .obj = std::vector<double>(variableCount, 0.0)};

    // Apply feasibility constrains to the model
    applyEdgeFeasibilityBounds(model, func);

    // Apply flow constrains
    appendGraphConstraints(model, func->Nodes, func->Edges, nullptr);

    // Fill the objective function
    fillObjectiveFunction(model, func);

    return model;
}

std::unordered_map<HLAC::LoopNode *, ILPModel> ILPBuilder::buildClusteredILP(HLAC::FunctionNode *func) {
    std::unordered_map<HLAC::LoopNode *, ILPModel> resultMapping;

    ILPUtil::assignEdgeIndicesFunction(func, 0);

    for (auto &nodeUP : func->Nodes) {
        auto *loopNode = dynamic_cast<HLAC::LoopNode *>(nodeUP.get());
        if (loopNode == nullptr) {
            continue;
        }

        ILPModel loopModel = buildMonolithicILP(loopNode);
        resultMapping[loopNode] = loopModel;
    }

    return resultMapping;
}
