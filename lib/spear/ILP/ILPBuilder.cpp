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

#include "ILP/ILPBuilder.h"

#include "HLAC/hlac.h"
#include "ILP/ILPSolver.h"
#include "ILP/ILPUtil.h"
#include "Logger.h"
#include "PassUtil.h"
#include "ILP/ILPDebug.h"

void ILPBuilder::applyEdgeFeasibilityBounds(ILPModel &model, HLAC::FunctionNode *func) {
    for (auto &edgeUP : func->Edges) {
        auto *edge = edgeUP.get();
        if (!edge) {
            continue;
        }

        const int column = edge->ilpIndex;
        if (column < 0 || column >= static_cast<int>(model.col_ub.size())) {
            Logger::getInstance().log(
                "Warning: invalid ilpIndex while applying feasibility bound.",
                LOGLEVEL::ERROR);
            continue;
        }

        if (!edge->feasibility) {
            model.col_lb[column] = 0.0;
            model.col_ub[column] = 0.0;
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

        const int column = edge->ilpIndex;
        if (column < 0 || column >= static_cast<int>(model.col_ub.size())) {
            Logger::getInstance().log(
                "Warning: invalid ilpIndex while applying feasibility bound.",
                LOGLEVEL::ERROR);
            continue;
        }

        if (!edge->feasibility) {
            model.col_lb[column] = 0.0;
            model.col_ub[column] = 0.0;
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

    std::unordered_map<HLAC::GenericNode *, std::vector<int>> incomingEdgesPerNode;
    std::unordered_map<HLAC::GenericNode *, std::vector<int>> outgoingEdgesPerNode;

    ILPUtil::buildIncidenceMaps(
        edges,
        incomingEdgesPerNode,
        outgoingEdgesPerNode,
        static_cast<int>(model.col_lb.size()));

    if (invocationCols != nullptr) {
        std::unordered_map<int, double> exitCoefficientsByColumn;
        bool hasVirtualExit = false;

        for (const auto &nodeUP : nodes) {
            auto *virtualNode = dynamic_cast<HLAC::VirtualNode *>(nodeUP.get());

            if (virtualNode == nullptr || !virtualNode->isExit) {
                continue;
            }

            hasVirtualExit = true;

            const std::vector<int> incomingEdges =
                incomingEdgesPerNode.contains(virtualNode)
                    ? incomingEdgesPerNode[virtualNode]
                    : std::vector<int>{};

            for (int column : incomingEdges) {
                ILPUtil::insertOrAccumulate(exitCoefficientsByColumn, column, 1.0);
            }
        }

        if (hasVirtualExit) {
            for (int column : *invocationCols) {
                ILPUtil::insertOrAccumulate(exitCoefficientsByColumn, column, -1.0);
            }

            CoinPackedVector exitRow = ILPUtil::createRowFromCoefficients(exitCoefficientsByColumn);
            ILPUtil::appendRow(model, exitRow, 0.0, 0.0);
        }
    }

    for (const auto &nodeUP : nodes) {
        auto *node = nodeUP.get();

        const std::vector<int> incomingEdges =
            incomingEdgesPerNode.contains(node) ? incomingEdgesPerNode[node] : std::vector<int>{};

        const std::vector<int> outgoingEdges =
            outgoingEdgesPerNode.contains(node) ? outgoingEdgesPerNode[node] : std::vector<int>{};

        if (auto *virtualNode = dynamic_cast<HLAC::VirtualNode *>(node)) {
            if (virtualNode->isEntry) {
                std::unordered_map<int, double> coefficientsByColumn;

                for (int column : outgoingEdges) {
                    ILPUtil::insertOrAccumulate(coefficientsByColumn, column, 1.0);
                }

                if (invocationCols == nullptr) {
                    CoinPackedVector row = ILPUtil::createRowFromCoefficients(coefficientsByColumn);
                    ILPUtil::appendRow(model, row, 1.0, 1.0);
                } else {
                    for (int column : *invocationCols) {
                        ILPUtil::insertOrAccumulate(coefficientsByColumn, column, -1.0);
                    }

                    CoinPackedVector row = ILPUtil::createRowFromCoefficients(coefficientsByColumn);
                    ILPUtil::appendRow(model, row, 0.0, 0.0);
                }

                continue;
            }

            if (virtualNode->isExit) {
                if (invocationCols == nullptr) {
                    std::unordered_map<int, double> coefficientsByColumn;

                    for (int column : incomingEdges) {
                        ILPUtil::insertOrAccumulate(coefficientsByColumn, column, 1.0);
                    }

                    CoinPackedVector row = ILPUtil::createRowFromCoefficients(coefficientsByColumn);
                    ILPUtil::appendRow(model, row, 1.0, 1.0);
                }

                continue;
            }
        }

        std::unordered_map<int, double> coefficientsByColumn;

        for (int column : incomingEdges) {
            ILPUtil::insertOrAccumulate(coefficientsByColumn, column, 1.0);
        }

        for (int column : outgoingEdges) {
            ILPUtil::insertOrAccumulate(coefficientsByColumn, column, -1.0);
        }

        CoinPackedVector row = ILPUtil::createRowFromCoefficients(coefficientsByColumn);
        ILPUtil::appendRow(model, row, 0.0, 0.0);

        if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(node)) {
            const std::vector<int> incomingColumns =
                incomingEdgesPerNode.contains(node) ? incomingEdgesPerNode[node] : std::vector<int>{};

            const std::vector<int> invocationColumns =
                collectExternalLoopInvocationColumns(loopNode, edges, incomingColumns);

            if (invocationColumns.empty()) {
                Logger::getInstance().log(
                    "Loop invocation debug: no external invocation columns for loop " + loopNode->getDotName(),
                    LOGLEVEL::ERROR);
            }

            appendGraphConstraints(model, loopNode->Nodes, loopNode->Edges, &invocationColumns);
            appendLoopBoundConstraint(model, loopNode, invocationColumns);
        }
    }
}

std::optional<ILPResult> ILPBuilder::solveClusteredLoopModel(
    const ILPModel &ilpModel,
    HLAC::LoopNode *loopNode) {

    ILPSolver modelSolver(ilpModel);

    auto optimalSolution = modelSolver.getSolvedModelValue();
    auto optimalPath = modelSolver.getSolvedSolution();

    if (optimalPath.has_value() && optimalSolution.has_value()) {
        return std::make_optional<ILPResult>(optimalSolution.value(), optimalPath.value());
    }

    const ILPSolverStatus solverStatus = modelSolver.getStatus();

    if (solverStatus == ILPSolverStatus::INFEASIBLE) {
        Logger::getInstance().log(
            "Clustered ILP: loop " + loopNode->getDotName()
            + " is infeasible under feasibility constraints. Treating it as unreachable with energy 0.0.",
            LOGLEVEL::WARNING);

        return std::make_optional<ILPResult>(
            0.0,
            std::vector<double>(ilpModel.matrix.getNumCols(), 0.0));
    }

    Logger::getInstance().log(
        "Clustered ILP failed for loop " + loopNode->getDotName()
        + " with status " + modelSolver.getStatusString(),
        LOGLEVEL::ERROR);

    return std::nullopt;
}

void ILPBuilder::appendLoopBoundConstraint(
    ILPModel &model,
    HLAC::LoopNode *loopNode,
    const std::vector<int> &invocationCols) {

    if (loopNode == nullptr) {
        Logger::getInstance().log(
            "Loop bound debug: loopNode is null.",
            LOGLEVEL::ERROR);
        return;
    }

    if (invocationCols.size() != 1) {
        Logger::getInstance().log(
            "Loop bound fallback: loop " + loopNode->getDotName()
            + " has " + std::to_string(invocationCols.size())
            + " invocation columns, expected exactly one.",
            LOGLEVEL::WARNING);
        return;
    }

    const double lowerBoundAsDouble = static_cast<double>(loopNode->bounds.getLowerBound());
    const double upperBoundAsDouble = static_cast<double>(loopNode->bounds.getUpperBound());

    const double lowerBackedgeFactor = std::max(0.0, lowerBoundAsDouble - 1.0);
    const double upperBackedgeFactor = std::max(0.0, upperBoundAsDouble - 1.0);

    const int invocationColumn = invocationCols.front();

    std::unordered_map<int, double> backedgeCoefficientsByColumn;

    for (HLAC::Edge *backEdge : loopNode->backEdges) {
        if (backEdge == nullptr) {
            continue;
        }

        if (backEdge->ilpIndex < 0 || backEdge->ilpIndex >= static_cast<int>(model.col_lb.size())) {
            Logger::getInstance().log(
                "Loop bound debug: invalid backedge ilpIndex in loop " + loopNode->getDotName(),
                LOGLEVEL::ERROR);
            continue;
        }

        ILPUtil::insertOrAccumulate(backedgeCoefficientsByColumn, backEdge->ilpIndex, 1.0);
    }

    if (backedgeCoefficientsByColumn.empty()) {
        Logger::getInstance().log(
            "Loop bound debug: loop " + loopNode->getDotName() + " has no valid backedges.",
            LOGLEVEL::ERROR);
        return;
    }

    {
        std::unordered_map<int, double> lowerBoundCoefficientsByColumn = backedgeCoefficientsByColumn;

        ILPUtil::insertOrAccumulate(
            lowerBoundCoefficientsByColumn,
            invocationColumn,
            -lowerBackedgeFactor);

        CoinPackedVector lowerBoundRow = ILPUtil::createRowFromCoefficients(lowerBoundCoefficientsByColumn);
        ILPUtil::appendRow(model, lowerBoundRow, 0.0, COIN_DBL_MAX);
    }

    {
        std::unordered_map<int, double> upperBoundCoefficientsByColumn = backedgeCoefficientsByColumn;

        ILPUtil::insertOrAccumulate(
            upperBoundCoefficientsByColumn,
            invocationColumn,
            -upperBackedgeFactor);

        CoinPackedVector upperBoundRow = ILPUtil::createRowFromCoefficients(upperBoundCoefficientsByColumn);
        ILPUtil::appendRow(model, upperBoundRow, -COIN_DBL_MAX, 0.0);
    }

    // Logger::getInstance().log(
    //     "Loop bound debug: loop=" + loopNode->getDotName()
    //     + " lower=" + std::to_string(loopNode->bounds.getLowerBound())
    //     + " upper=" + std::to_string(loopNode->bounds.getUpperBound())
    //     + " lowerBackedgeFactor=" + std::to_string(lowerBackedgeFactor)
    //     + " upperBackedgeFactor=" + std::to_string(upperBackedgeFactor)
    //     + " invocations=" + std::to_string(invocationCols.size())
    //     + " backedges=" + std::to_string(loopNode->backEdges.size()),
    //     LOGLEVEL::INFO);
}

void ILPBuilder::fillObjectiveFunction(ILPModel &model, HLAC::FunctionNode *func) {
    for (auto &edgeUP : func->Edges) {
        auto *edge = edgeUP.get();

        auto cacheIterator = func->directNodeEnergyCache.find(edge->destination);
        if (cacheIterator != func->directNodeEnergyCache.end()) {
            model.obj[edge->ilpIndex] = cacheIterator->second;
        } else {
            model.obj[edge->ilpIndex] = edge->destination->getEnergy();
        }
    }

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

    for (auto &nodeUP : loopNode->Nodes) {
        if (auto *innerLoop = dynamic_cast<HLAC::LoopNode *>(nodeUP.get())) {
            fillObjectiveFunction(model, innerLoop);
        }
    }
}

std::optional<ILPResult> ILPBuilder::solveModel(const ILPModel &ilpModel) {
    ILPSolver modelSolver(ilpModel);

    auto optimalSolution = modelSolver.getSolvedModelValue();
    auto optimalPath = modelSolver.getSolvedSolution();

    if (optimalPath.has_value() && optimalSolution.has_value()) {
        return std::make_optional<ILPResult>(optimalSolution.value(), optimalPath.value());
    }

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
    std::unordered_map<int, double> coefficientsByColumn;
    ILPUtil::insertOrAccumulate(coefficientsByColumn, column, 1.0);

    CoinPackedVector row = ILPUtil::createRowFromCoefficients(coefficientsByColumn);
    ILPUtil::appendRow(model, row, 1.0, 1.0);
}

ILPModel ILPBuilder::buildMonolithicILP(HLAC::LoopNode *loop) {
    const int maxEdgeIndex = ILPUtil::getMaxEdgeIndex(loop);

    const int invocationColumn = maxEdgeIndex + 1;
    const int variableCount = invocationColumn + 1;

    const std::vector<int> invocationColumns = {invocationColumn};

    ILPModel model{
        .matrix = CoinPackedMatrix(false, 0, 0),
        .row_lb = {},
        .row_ub = {},
        .col_lb = std::vector<double>(variableCount, 0.0),
        .col_ub = std::vector<double>(variableCount, COIN_DBL_MAX),
        .obj = std::vector<double>(variableCount, 0.0)
    };

    applyEdgeFeasibilityBounds(model, loop);

    appendGraphConstraints(model, loop->Nodes, loop->Edges, &invocationColumns);

    appendLoopBoundConstraint(model, loop, invocationColumns);

    appendEqualityConstraint(model, invocationColumn);

    fillObjectiveFunction(model, loop);

    if (loop->parentFunction->name == "LZ4_decompress_safe") {
        //ILPDebug::dumpILPModel(model, loop->Edges, loop->loop->getName().str());
    }

    return model;
}

/**
 * TODO: Rename this function for the sake of sanity
 */
std::vector<std::pair<ILPModel, HLAC::VirtualNode *>> ILPBuilder::buildClusteredILP(HLAC::LoopNode *loop) {
    std::vector<std::pair<ILPModel, HLAC::VirtualNode *>> models;

    for (auto exitIdx : loop->exits) {
        const int maxEdgeIndex = ILPUtil::getMaxEdgeIndex(loop);

        const int invocationColumn = maxEdgeIndex + 1;
        const int variableCount = invocationColumn + 1;

        const std::vector<int> invocationColumns = {invocationColumn};

        ILPModel model{
            .matrix = CoinPackedMatrix(false, 0, 0),
            .row_lb = {},
            .row_ub = {},
            .col_lb = std::vector<double>(variableCount, 0.0),
            .col_ub = std::vector<double>(variableCount, COIN_DBL_MAX),
            .obj = std::vector<double>(variableCount, 0.0)
        };

        applyEdgeFeasibilityBounds(model, loop);

        appendGraphConstraints(model, loop->Nodes, loop->Edges, &invocationColumns);

        appendLoopBoundConstraint(model, loop, invocationColumns);

        appendEqualityConstraint(model, invocationColumn);

        fillObjectiveFunction(model, loop);

        appendExitConstrains(model, loop, exitIdx);

        if (loop->parentFunction->name == "main") {
            //ILPDebug::dumpILPModel(model, loop->Edges, loop->loop->getName().str());
        }

        auto exitNode = loop->Nodes[exitIdx].get();

        models.push_back(std::make_pair(model, dynamic_cast<HLAC::VirtualNode *>(exitNode)));
    }

    return models;
}

void ILPBuilder::appendExitConstrains(ILPModel &model, HLAC::LoopNode *loopNode, int exitIdx) {
    if (loopNode == nullptr) {
        Logger::getInstance().log(
            "Exit constraint debug: loopNode is null.",
            LOGLEVEL::ERROR);
        return;
    }

    if (exitIdx < 0 || exitIdx >= static_cast<int>(loopNode->Nodes.size())) {
        Logger::getInstance().log(
            "Exit constraint debug: invalid exit index in loop " + loopNode->getDotName(),
            LOGLEVEL::ERROR);
        return;
    }

    HLAC::GenericNode *exitNodeToConsider = loopNode->Nodes[exitIdx].get();
    int activeColumn = -1;
    std::vector<int> disabledColumns;

    for (auto &edgeUniquePointer : loopNode->Edges) {
        HLAC::Edge *edge = edgeUniquePointer.get();

        if (edge == nullptr || edge->destination != exitNodeToConsider) {
            continue;
        }

        if (edge->ilpIndex < 0 || edge->ilpIndex >= static_cast<int>(model.col_lb.size())) {
            Logger::getInstance().log(
                "Exit constraint debug: invalid exit edge ilpIndex in loop " + loopNode->getDotName(),
                LOGLEVEL::ERROR);
            continue;
        }

        activeColumn = edge->ilpIndex;
    }

    for (auto &edgeUniquePointer : loopNode->Edges) {
        HLAC::Edge *edge = edgeUniquePointer.get();

        if (edge == nullptr || edge->destination == exitNodeToConsider) {
            continue;
        }

        auto *virtualDestination = dynamic_cast<HLAC::VirtualNode *>(edge->destination);

        if (virtualDestination == nullptr || !virtualDestination->isExit) {
            continue;
        }

        if (edge->ilpIndex < 0 || edge->ilpIndex >= static_cast<int>(model.col_lb.size())) {
            Logger::getInstance().log(
                "Exit constraint debug: invalid disabled exit edge ilpIndex in loop " + loopNode->getDotName(),
                LOGLEVEL::ERROR);
            continue;
        }

        disabledColumns.push_back(edge->ilpIndex);
    }

    if (activeColumn < 0) {
        Logger::getInstance().log(
            "Exit constraint debug: no active exit edge found in loop " + loopNode->getDotName(),
            LOGLEVEL::ERROR);
        return;
    }

    model.col_lb[activeColumn] = 1.0;
    model.col_ub[activeColumn] = 1.0;

    for (int disabledColumn : disabledColumns) {
        model.col_lb[disabledColumn] = 0.0;
        model.col_ub[disabledColumn] = 0.0;
    }
}

std::vector<int> ILPBuilder::collectExternalLoopInvocationColumns(
    HLAC::LoopNode *loopNode,
    const std::vector<std::unique_ptr<HLAC::Edge>> &parentEdges,
    const std::vector<int> &incomingColumns) {

    std::vector<int> invocationColumns;

    for (int incomingColumn : incomingColumns) {
        HLAC::Edge *matchingEdge = nullptr;

        for (const auto &edgeUniquePointer : parentEdges) {
            HLAC::Edge *edge = edgeUniquePointer.get();

            if (edge != nullptr && edge->ilpIndex == incomingColumn) {
                matchingEdge = edge;
                break;
            }
        }

        if (matchingEdge == nullptr) {
            continue;
        }

        if (matchingEdge->destination != loopNode) {
            continue;
        }

        if (matchingEdge->soure == loopNode) {
            continue;
        }

        invocationColumns.push_back(incomingColumn);
    }

    return invocationColumns;
}

ILPModel ILPBuilder::buildMonolithicILP(HLAC::FunctionNode *func) {
    const int variableCount = ILPUtil::assignEdgeIndicesFunction(func, 0);

    ILPModel model{
        .matrix = CoinPackedMatrix(false, 0, 0),
        .row_lb = {},
        .row_ub = {},
        .col_lb = std::vector<double>(variableCount, 0.0),
        .col_ub = std::vector<double>(variableCount, COIN_DBL_MAX),
        .obj = std::vector<double>(variableCount, 0.0)
    };

    applyEdgeFeasibilityBounds(model, func);

    appendGraphConstraints(model, func->Nodes, func->Edges, nullptr);

    fillObjectiveFunction(model, func);

    return model;
}

std::unordered_map<HLAC::LoopNode *, std::vector<std::pair<ILPModel, HLAC::VirtualNode *>>>
ILPBuilder::buildClusteredILP(HLAC::FunctionNode *func) {
    std::unordered_map<HLAC::LoopNode *, std::vector<std::pair<ILPModel, HLAC::VirtualNode *>>> resultMapping;

    ILPUtil::assignEdgeIndicesFunction(func, 0);

    for (auto &nodeUP : func->Nodes) {
        auto *loopNode = dynamic_cast<HLAC::LoopNode *>(nodeUP.get());
        if (loopNode == nullptr) {
            continue;
        }

        auto loopModel = buildClusteredILP(loopNode);
        resultMapping[loopNode] = loopModel;
    }

    return resultMapping;
}

void ILPUtil::insertOrAccumulate(std::unordered_map<int, double> &coefficientsByColumn, int column, double coefficient) {
    coefficientsByColumn[column] += coefficient;
}

CoinPackedVector ILPUtil::createRowFromCoefficients(const std::unordered_map<int, double> &coefficientsByColumn) {

    CoinPackedVector row;

    for (const auto &[column, coefficient] : coefficientsByColumn) {
        if (std::abs(coefficient) > 1e-12) {
            row.insert(column, coefficient);
        }
    }

    return row;
}