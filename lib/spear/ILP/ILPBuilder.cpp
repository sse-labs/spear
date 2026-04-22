/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <unordered_set>
#include <utility>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>

#include "ILP/ILPBuilder.h"

#include "HLAC/hlac.h"
#include "ILP/ILPSolver.h"
#include "ILP/ILPUtil.h"
#include "Logger.h"
#include "PassUtil.h"

namespace {

struct ILPRowTerm {
    int columnIndex;
    double coefficient;
};

std::string ilpBoundToString(double value, bool isLowerBound) {
    if (isLowerBound && value <= -COIN_DBL_MAX / 2.0) {
        return "-inf";
    }

    if (!isLowerBound && value >= COIN_DBL_MAX / 2.0) {
        return "+inf";
    }

    std::ostringstream outputStream;
    outputStream << value;
    return outputStream.str();
}

std::string ilpTermsToString(const std::vector<ILPRowTerm> &rowTerms) {
    std::ostringstream outputStream;

    if (rowTerms.empty()) {
        return "0";
    }

    for (std::size_t termIndex = 0; termIndex < rowTerms.size(); ++termIndex) {
        const double coefficient = rowTerms[termIndex].coefficient;
        const int columnIndex = rowTerms[termIndex].columnIndex;

        if (termIndex > 0) {
            outputStream << (coefficient >= 0.0 ? " + " : " - ");
        } else if (coefficient < 0.0) {
            outputStream << "-";
        }

        outputStream << std::abs(coefficient) << "*x" << columnIndex;
    }

    return outputStream.str();
}

std::string ilpRowToString(const CoinPackedVector &row) {
    std::ostringstream outputStream;

    const int numberOfElements = row.getNumElements();
    const int *indices = row.getIndices();
    const double *elements = row.getElements();

    if (numberOfElements == 0) {
        return "0";
    }

    for (int elementIndex = 0; elementIndex < numberOfElements; ++elementIndex) {
        const double coefficient = elements[elementIndex];
        const int columnIndex = indices[elementIndex];

        if (elementIndex > 0) {
            outputStream << (coefficient >= 0.0 ? " + " : " - ");
        } else if (coefficient < 0.0) {
            outputStream << "-";
        }

        outputStream << std::abs(coefficient) << "*x" << columnIndex;
    }

    return outputStream.str();
}

void dumpILPModel(const ILPModel &model, const std::string &label) {
    Logger::getInstance().log("========== ILP DUMP BEGIN: " + label + " ==========", LOGLEVEL::INFO);

    Logger::getInstance().log(
        "ILP summary: variables=" + std::to_string(model.matrix.getNumCols()) +
        ", constraints=" + std::to_string(model.matrix.getNumRows()),
        LOGLEVEL::INFO);

    Logger::getInstance().log(
        std::string("ILP matrix ordering: ") + (model.matrix.isColOrdered() ? "column-ordered" : "row-ordered"),
        LOGLEVEL::INFO);

    Logger::getInstance().log("Variables:", LOGLEVEL::INFO);
    for (int columnIndex = 0; columnIndex < model.matrix.getNumCols(); ++columnIndex) {
        std::ostringstream outputStream;
        outputStream << "  x" << columnIndex
                     << ": bounds=["
                     << ilpBoundToString(model.col_lb[columnIndex], true)
                     << ", "
                     << ilpBoundToString(model.col_ub[columnIndex], false)
                     << "], obj=" << model.obj[columnIndex];
        Logger::getInstance().log(outputStream.str(), LOGLEVEL::INFO);
    }

    Logger::getInstance().log("Constraints:", LOGLEVEL::INFO);
    for (int rowIndex = 0; rowIndex < model.matrix.getNumRows(); ++rowIndex) {
        const CoinPackedVector row = model.matrix.getVector(rowIndex);

        std::ostringstream outputStream;
        outputStream << "  c" << rowIndex << ": ";

        const double lowerBound = model.row_lb[rowIndex];
        const double upperBound = model.row_ub[rowIndex];

        if (lowerBound <= -COIN_DBL_MAX / 2.0 && upperBound < COIN_DBL_MAX / 2.0) {
            outputStream << ilpRowToString(row) << " <= " << upperBound;
        } else if (upperBound >= COIN_DBL_MAX / 2.0 && lowerBound > -COIN_DBL_MAX / 2.0) {
            outputStream << ilpRowToString(row) << " >= " << lowerBound;
        } else {
            outputStream << lowerBound << " <= " << ilpRowToString(row) << " <= " << upperBound;
        }

        Logger::getInstance().log(outputStream.str(), LOGLEVEL::INFO);
    }

    Logger::getInstance().log("========== ILP DUMP END: " + label + " ==========", LOGLEVEL::INFO);
}

std::string basicBlockToDebugString(const llvm::BasicBlock *basicBlock) {
    if (basicBlock == nullptr) {
        return "<null>";
    }

    std::string output;
    llvm::raw_string_ostream outputStream(output);
    basicBlock->printAsOperand(outputStream, false);
    return outputStream.str();
}

std::string genericNodeToDebugString(const HLAC::GenericNode *genericNode) {
    if (genericNode == nullptr) {
        return "<null>";
    }

    std::ostringstream outputStream;

    if (const auto *normalNode = dynamic_cast<const HLAC::Node *>(genericNode)) {
        outputStream << "Node(" << basicBlockToDebugString(normalNode->block) << ")";
        return outputStream.str();
    }

    if (const auto *callNode = dynamic_cast<const HLAC::CallNode *>(genericNode)) {
        std::string calledFunctionName = "<indirect>";
        if (callNode->calledFunction != nullptr) {
            calledFunctionName = callNode->calledFunction->getName().str();
        }
        outputStream << "CallNode(" << calledFunctionName << ")";
        return outputStream.str();
    }

    if (const auto *virtualNode = dynamic_cast<const HLAC::VirtualNode *>(genericNode)) {
        if (virtualNode->isEntry) {
            return "VirtualEntry";
        }

        if (virtualNode->isExit) {
            return "VirtualExit";
        }

        return "VirtualNode";
    }

    if (dynamic_cast<const HLAC::LoopNode *>(genericNode) != nullptr) {
        return "LoopNode";
    }

    if (dynamic_cast<const HLAC::FunctionNode *>(genericNode) != nullptr) {
        return "FunctionNode";
    }

    return "GenericNode";
}

bool rowTouchesInterestingColumns(const CoinPackedVector &row, const std::unordered_set<int> &interestingColumns) {
    const int numberOfElements = row.getNumElements();
    const int *indices = row.getIndices();

    for (int elementIndex = 0; elementIndex < numberOfElements; ++elementIndex) {
        if (interestingColumns.count(indices[elementIndex]) != 0) {
            return true;
        }
    }

    return false;
}

std::string packedRowToString(const CoinPackedVector &row) {
    std::ostringstream outputStream;

    const int numberOfElements = row.getNumElements();
    const int *indices = row.getIndices();
    const double *elements = row.getElements();

    if (numberOfElements == 0) {
        return "0";
    }

    for (int elementIndex = 0; elementIndex < numberOfElements; ++elementIndex) {
        const double coefficient = elements[elementIndex];
        const int columnIndex = indices[elementIndex];

        if (elementIndex > 0) {
            outputStream << (coefficient >= 0.0 ? " + " : " - ");
        } else if (coefficient < 0.0) {
            outputStream << "-";
        }

        outputStream << std::abs(coefficient) << "*x" << columnIndex;
    }

    return outputStream.str();
}

void logInterestingRow(
    const std::string &label,
    const CoinPackedVector &row,
    double lowerBound,
    double upperBound,
    const std::unordered_set<int> &interestingColumns) {

    if (!rowTouchesInterestingColumns(row, interestingColumns)) {
        return;
    }

    std::ostringstream outputStream;
    outputStream << label << ": ";

    if (lowerBound <= -COIN_DBL_MAX / 2.0 && upperBound < COIN_DBL_MAX / 2.0) {
        outputStream << packedRowToString(row) << " <= " << upperBound;
    } else if (upperBound >= COIN_DBL_MAX / 2.0 && lowerBound > -COIN_DBL_MAX / 2.0) {
        outputStream << packedRowToString(row) << " >= " << lowerBound;
    } else {
        outputStream << lowerBound << " <= " << packedRowToString(row) << " <= " << upperBound;
    }

    Logger::getInstance().log(outputStream.str(), LOGLEVEL::INFO);
}

std::string basicBlockToString(const llvm::BasicBlock* basicBlock) {
    if (basicBlock == nullptr) {
        return "<null>";
    }

    std::string output;
    llvm::raw_string_ostream stream(output);
    basicBlock->printAsOperand(stream, false);
    return output;
}

std::string edgeEndpointToString(const HLAC::GenericNode* genericNode) {
    if (genericNode == nullptr) {
        return "<null>";
    }

    if (const auto* normalNode = dynamic_cast<const HLAC::Node*>(genericNode)) {
        return "Node(" + basicBlockToString(normalNode->block) + ")";
    }

    if (const auto* virtualNode = dynamic_cast<const HLAC::VirtualNode*>(genericNode)) {
        if (virtualNode->isEntry) {
            return "VirtualEntry";
        }

        if (virtualNode->isExit) {
            return "VirtualExit";
        }

        return "VirtualNode";
    }

    if (const auto* callNode = dynamic_cast<const HLAC::CallNode*>(genericNode)) {
        std::string calledFunctionName = "<indirect>";

        if (callNode->calledFunction != nullptr) {
            calledFunctionName = callNode->calledFunction->getName().str();
        }

        return "CallNode(" + calledFunctionName + ")";
    }

    if (dynamic_cast<const HLAC::LoopNode*>(genericNode) != nullptr) {
        return "LoopNode";
    }

    if (dynamic_cast<const HLAC::FunctionNode*>(genericNode) != nullptr) {
        return "FunctionNode";
    }

    return "GenericNode";
}

std::string edgeToString(const HLAC::Edge* edge) {
    if (edge == nullptr) {
        return "<null edge>";
    }

    std::ostringstream outputStream;
    outputStream << edgeEndpointToString(edge->soure)
                 << " -> "
                 << edgeEndpointToString(edge->destination)
                 << " [col=" << edge->ilpIndex << "]";

    return outputStream.str();
}

std::string intVectorToString(const std::vector<int>& values) {
    std::ostringstream outputStream;
    outputStream << "[";

    for (std::size_t valueIndex = 0; valueIndex < values.size(); ++valueIndex) {
        if (valueIndex > 0) {
            outputStream << ", ";
        }

        outputStream << values[valueIndex];
    }

    outputStream << "]";
    return outputStream.str();
}

void dumpLoopVariableMeaningRecursive(HLAC::LoopNode *loopNode) {
    if (loopNode == nullptr) {
        return;
    }

    Logger::getInstance().log(
        "  x" + std::to_string(loopNode->invocationIlpIndex) +
        " = invocation(" + loopNode->getDotName() + ")",
        LOGLEVEL::INFO);

    for (const auto &edgeUniquePointer : loopNode->Edges) {
        const HLAC::Edge *edge = edgeUniquePointer.get();
        if (edge == nullptr) {
            continue;
        }

        Logger::getInstance().log(
            "  x" + std::to_string(edge->ilpIndex) + " = " + edgeToString(edge),
            LOGLEVEL::INFO);
    }

    for (const auto &nodeUniquePointer : loopNode->Nodes) {
        auto *innerLoop = dynamic_cast<HLAC::LoopNode *>(nodeUniquePointer.get());
        if (innerLoop != nullptr) {
            dumpLoopVariableMeaningRecursive(innerLoop);
        }
    }
}

void dumpVariableMeaning(HLAC::FunctionNode *functionNode) {
    Logger::getInstance().log("========== ILP VARIABLE MEANINGS BEGIN ==========", LOGLEVEL::INFO);

    for (const auto &edgeUniquePointer : functionNode->Edges) {
        const HLAC::Edge *edge = edgeUniquePointer.get();
        if (edge == nullptr) {
            continue;
        }

        Logger::getInstance().log(
            "  x" + std::to_string(edge->ilpIndex) + " = " + edgeToString(edge),
            LOGLEVEL::INFO);
    }

    for (const auto &nodeUniquePointer : functionNode->Nodes) {
        auto *loopNode = dynamic_cast<HLAC::LoopNode *>(nodeUniquePointer.get());
        if (loopNode != nullptr) {
            dumpLoopVariableMeaningRecursive(loopNode);
        }
    }

    Logger::getInstance().log("========== ILP VARIABLE MEANINGS END ==========", LOGLEVEL::INFO);
}

}  // namespace

void ILPBuilder::applyEdgeFeasibilityBounds(ILPModel &model, HLAC::FunctionNode *func) {
    for (auto &edgeUniquePointer : func->Edges) {
        auto *edge = edgeUniquePointer.get();
        if (edge == nullptr) {
            continue;
        }

        const int columnIndex = edge->ilpIndex;
        if (columnIndex < 0 || columnIndex >= static_cast<int>(model.col_ub.size())) {
            Logger::getInstance().log(
                "Warning: invalid ilpIndex while applying feasibility bound.",
                LOGLEVEL::ERROR);
            continue;
        }

        if (!edge->feasibility) {
            model.col_lb[columnIndex] = 0.0;
            model.col_ub[columnIndex] = 0.0;
        }
    }

    for (auto &nodeUniquePointer : func->Nodes) {
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(nodeUniquePointer.get())) {
            applyEdgeFeasibilityBounds(model, loopNode);
        }
    }
}

void ILPBuilder::applyEdgeFeasibilityBounds(ILPModel &model, HLAC::LoopNode *loopNode) {
    for (auto &edgeUniquePointer : loopNode->Edges) {
        auto *edge = edgeUniquePointer.get();
        if (edge == nullptr) {
            continue;
        }

        const int columnIndex = edge->ilpIndex;
        if (columnIndex < 0 || columnIndex >= static_cast<int>(model.col_ub.size())) {
            Logger::getInstance().log(
                "Warning: invalid ilpIndex while applying feasibility bound.",
                LOGLEVEL::ERROR);
            continue;
        }

        if (!edge->feasibility) {
            model.col_lb[columnIndex] = 0.0;
            model.col_ub[columnIndex] = 0.0;
        }
    }

    for (auto &nodeUniquePointer : loopNode->Nodes) {
        if (auto *innerLoop = dynamic_cast<HLAC::LoopNode *>(nodeUniquePointer.get())) {
            applyEdgeFeasibilityBounds(model, innerLoop);
        }
    }
}

int ILPBuilder::assignLoopInvocationIndices(HLAC::LoopNode *loopNode, int nextColumn) {
    if (loopNode == nullptr) {
        return nextColumn;
    }

    loopNode->invocationIlpIndex = nextColumn;
    ++nextColumn;

    for (auto &nodeUniquePointer : loopNode->Nodes) {
        if (auto *innerLoop = dynamic_cast<HLAC::LoopNode *>(nodeUniquePointer.get())) {
            nextColumn = assignLoopInvocationIndices(innerLoop, nextColumn);
        }
    }

    return nextColumn;
}

int ILPBuilder::assignLoopInvocationIndices(HLAC::FunctionNode *functionNode, int nextColumn) {
    if (functionNode == nullptr) {
        return nextColumn;
    }

    for (auto &nodeUniquePointer : functionNode->Nodes) {
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(nodeUniquePointer.get())) {
            nextColumn = assignLoopInvocationIndices(loopNode, nextColumn);
        }
    }

    return nextColumn;
}

void ILPBuilder::appendGraphConstraints(
    ILPModel &model,
    const std::vector<std::unique_ptr<HLAC::GenericNode>> &nodes,
    const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
    int invocationCol) {
    std::unordered_map<HLAC::GenericNode *, std::vector<int>> incomingEdgesPerNode;
    std::unordered_map<HLAC::GenericNode *, std::vector<int>> outgoingEdgesPerNode;

    const std::unordered_set<int> interestingColumns = {
        6, 16, 20, 26, 30, 36,
        17, 18,
        27, 28,
        37, 38,
        39, 40, 41,
        11, 15,
        24, 25,
        34, 35
    };

    ILPUtil::buildIncidenceMaps(
        edges,
        incomingEdgesPerNode,
        outgoingEdgesPerNode,
        static_cast<int>(model.col_lb.size()));

    for (const auto &nodeUniquePointer : nodes) {
        auto *node = nodeUniquePointer.get();
        CoinPackedVector row;
        std::unordered_set<int> usedColumns;

        if (auto *virtualNode = dynamic_cast<HLAC::VirtualNode *>(node)) {
            if (virtualNode->isEntry) {
                for (int columnIndex : outgoingEdgesPerNode[node]) {
                    ILPUtil::insertUnique(row, usedColumns, columnIndex, 1.0);
                }

                if (invocationCol < 0) {
                    logInterestingRow(
                        "Flow row [virtual entry top-level] for " + genericNodeToDebugString(node),
                        row,
                        1.0,
                        1.0,
                        interestingColumns);

                    ILPUtil::appendRow(model, row, 1.0, 1.0);
                } else {
                    ILPUtil::insertUnique(row, usedColumns, invocationCol, -1.0);

                    logInterestingRow(
                        "Flow row [virtual entry nested] for " + genericNodeToDebugString(node),
                        row,
                        0.0,
                        0.0,
                        interestingColumns);

                    ILPUtil::appendRow(model, row, 0.0, 0.0);
                }

                continue;
            }

            if (virtualNode->isExit) {
                for (int columnIndex : incomingEdgesPerNode[node]) {
                    ILPUtil::insertUnique(row, usedColumns, columnIndex, 1.0);
                }

                if (invocationCol < 0) {
                    logInterestingRow(
                        "Flow row [virtual exit top-level] for " + genericNodeToDebugString(node),
                        row,
                        1.0,
                        1.0,
                        interestingColumns);

                    ILPUtil::appendRow(model, row, 1.0, 1.0);
                } else {
                    ILPUtil::insertUnique(row, usedColumns, invocationCol, -1.0);

                    logInterestingRow(
                        "Flow row [virtual exit nested] for " + genericNodeToDebugString(node),
                        row,
                        0.0,
                        0.0,
                        interestingColumns);

                    ILPUtil::appendRow(model, row, 0.0, 0.0);
                }

                continue;
            }
        }

        for (int columnIndex : incomingEdgesPerNode[node]) {
            ILPUtil::insertUnique(row, usedColumns, columnIndex, 1.0);
        }

        for (int columnIndex : outgoingEdgesPerNode[node]) {
            ILPUtil::insertUnique(row, usedColumns, columnIndex, -1.0);
        }

        logInterestingRow(
            "Flow row [normal] for " + genericNodeToDebugString(node),
            row,
            0.0,
            0.0,
            interestingColumns);

        ILPUtil::appendRow(model, row, 0.0, 0.0);

        if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(node)) {
            const int loopInvocationColumn = loopNode->invocationIlpIndex;

            CoinPackedVector incomingCouplingRow;
            std::unordered_set<int> incomingUsedColumns;
            for (int incomingColumn : incomingEdgesPerNode[node]) {
                ILPUtil::insertUnique(incomingCouplingRow, incomingUsedColumns, incomingColumn, 1.0);
            }
            ILPUtil::insertUnique(incomingCouplingRow, incomingUsedColumns, loopInvocationColumn, -1.0);
            ILPUtil::appendRow(model, incomingCouplingRow, 0.0, 0.0);

            CoinPackedVector outgoingCouplingRow;
            std::unordered_set<int> outgoingUsedColumns;
            for (int outgoingColumn : outgoingEdgesPerNode[node]) {
                ILPUtil::insertUnique(outgoingCouplingRow, outgoingUsedColumns, outgoingColumn, 1.0);
            }
            ILPUtil::insertUnique(outgoingCouplingRow, outgoingUsedColumns, loopInvocationColumn, -1.0);
            ILPUtil::appendRow(model, outgoingCouplingRow, 0.0, 0.0);

            appendGraphConstraints(model, loopNode->Nodes, loopNode->Edges, loopInvocationColumn);
            appendLoopBoundConstraint(model, loopNode, loopInvocationColumn);
            continue;
        }
    }
}

void ILPBuilder::appendLoopBoundConstraint(
    ILPModel& model,
    HLAC::LoopNode* loopNode,
    int invocationCol) {
    if (loopNode == nullptr) {
        Logger::getInstance().log(
            "Loop bound debug: loopNode is null, skipping loop bound constraint.",
            LOGLEVEL::ERROR);
        return;
    }

    if (loopNode->backEdges.empty()) {
        Logger::getInstance().log(
            "Warning: Loop " + loopNode->getDotName() + " has no backedge, skipping loop bound constraint.",
            LOGLEVEL::ERROR);
        return;
    }

    const double lowerBound = static_cast<double>(loopNode->bounds.getLowerBound());
    const double upperBound = static_cast<double>(loopNode->bounds.getUpperBound());

    std::string functionName = "<unknown>";
    if (loopNode->parentFunction != nullptr) {
        functionName = loopNode->parentFunction->name;
    }

    const bool isDebugFunction = (functionName == "escrypt_PBKDF2_SHA256");
    const std::unordered_set<int> interestingColumns = {
        6, 16, 20, 26, 30, 36,
        17, 18,
        27, 28,
        37, 38,
        39, 40, 41,
        11, 15,
        24, 25,
        34, 35
    };

    Logger::getInstance().log(
        "Loop bound debug for function " + functionName +
        ", loop " + loopNode->getDotName() +
        ", header=" + basicBlockToString(loopNode->loop != nullptr ? loopNode->loop->getHeader() : nullptr) +
        ", bounds=[" + std::to_string(loopNode->bounds.getLowerBound()) +
        ", " + std::to_string(loopNode->bounds.getUpperBound()) + "]",
        LOGLEVEL::INFO);

    Logger::getInstance().log(
        "Loop bound debug: invocation column = x" + std::to_string(invocationCol),
        LOGLEVEL::INFO);

    for (std::size_t backEdgeIndex = 0; backEdgeIndex < loopNode->backEdges.size(); ++backEdgeIndex) {
        HLAC::Edge *backEdge = loopNode->backEdges[backEdgeIndex];
        Logger::getInstance().log(
            "Loop bound debug: backedge[" + std::to_string(backEdgeIndex) + "] = " + edgeToString(backEdge),
            LOGLEVEL::INFO);
    }

    for (HLAC::Edge *backEdge : loopNode->backEdges) {
        if (backEdge == nullptr) {
            continue;
        }

        const int backEdgeColumn = backEdge->ilpIndex;

        CoinPackedVector upperBoundRow;
        std::unordered_set<int> upperBoundUsedColumns;

        ILPUtil::insertUnique(upperBoundRow, upperBoundUsedColumns, backEdgeColumn, 1.0);
        ILPUtil::insertUnique(upperBoundRow, upperBoundUsedColumns, invocationCol, -upperBound);

        Logger::getInstance().log(
            "Loop bound debug upper row: x" + std::to_string(backEdgeColumn) +
            " - " + std::to_string(static_cast<long long>(upperBound)) +
            " * x" + std::to_string(invocationCol) + " <= 0",
            LOGLEVEL::INFO);

        ILPUtil::appendRow(model, upperBoundRow, -COIN_DBL_MAX, 0.0);

        if (isDebugFunction) {
            logInterestingRow(
                "Loop bound row",
                upperBoundRow,
                -COIN_DBL_MAX,
                0.0,
                interestingColumns);
        }

        CoinPackedVector lowerBoundRow;
        std::unordered_set<int> lowerBoundUsedColumns;

        ILPUtil::insertUnique(lowerBoundRow, lowerBoundUsedColumns, backEdgeColumn, 1.0);
        ILPUtil::insertUnique(lowerBoundRow, lowerBoundUsedColumns, invocationCol, -lowerBound);

        Logger::getInstance().log(
            "Loop bound debug lower row: x" + std::to_string(backEdgeColumn) +
            " - " + std::to_string(static_cast<long long>(lowerBound)) +
            " * x" + std::to_string(invocationCol) + " >= 0",
            LOGLEVEL::INFO);

        ILPUtil::appendRow(model, lowerBoundRow, 0.0, COIN_DBL_MAX);

        if (isDebugFunction) {
            logInterestingRow(
                "Loop bound row",
                lowerBoundRow,
                0.0,
                COIN_DBL_MAX,
                interestingColumns);
        }
    }
}

void ILPBuilder::fillObjectiveFunction(ILPModel &model, HLAC::FunctionNode *func) {
    for (auto &edgeUniquePointer : func->Edges) {
        auto *edge = edgeUniquePointer.get();

        auto cacheIterator = func->directNodeEnergyCache.find(edge->destination);
        if (cacheIterator != func->directNodeEnergyCache.end()) {
            model.obj[edge->ilpIndex] = cacheIterator->second;
        } else {
            model.obj[edge->ilpIndex] = edge->destination->getEnergy();
        }
    }

    for (auto &nodeUniquePointer : func->Nodes) {
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(nodeUniquePointer.get())) {
            fillObjectiveFunction(model, loopNode);
        }
    }
}

void ILPBuilder::fillObjectiveFunction(ILPModel &model, HLAC::LoopNode *loopNode) {
    for (auto &edgeUniquePointer : loopNode->Edges) {
        auto *edge = edgeUniquePointer.get();
        model.obj[edge->ilpIndex] = edge->destination->getEnergy();
    }

    // Keep artificial invocation variables cost-neutral.
    if (loopNode->invocationIlpIndex >= 0 && loopNode->invocationIlpIndex < static_cast<int>(model.obj.size())) {
        model.obj[loopNode->invocationIlpIndex] = 0.0;
    }

    for (auto &nodeUniquePointer : loopNode->Nodes) {
        if (auto *innerLoop = dynamic_cast<HLAC::LoopNode*>(nodeUniquePointer.get())) {
            fillObjectiveFunction(model, innerLoop);
        }
    }
}

std::optional<ILPResult> ILPBuilder::solveModel(const ILPModel& ilpModel, const std::string &debugLabel) {
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

    if (debugLabel == "escrypt_PBKDF2_SHA256") {
        dumpILPModel(ilpModel, debugLabel);
    }

    return std::nullopt;
}

void ILPBuilder::appendEqualityConstraint(ILPModel &model, int col) {
    CoinPackedVector row;
    std::unordered_set<int> usedCols;
    const double value = 1.0;

    ILPUtil::insertUnique(row, usedCols, col, 1.0);
    ILPUtil::appendRow(model, row, value, value);
}

ILPModel ILPBuilder::buildMonolithicILP(HLAC::LoopNode *loop) {
    const int maxEdgeIndex = ILPUtil::getMaxEdgeIndex(loop);
    const int invocationCol = maxEdgeIndex + 1;
    const int numVars = invocationCol + 1;

    loop->invocationIlpIndex = invocationCol;

    ILPModel model{
        .matrix = CoinPackedMatrix(false, 0, 0),
        .row_lb = {},
        .row_ub = {},
        .col_lb = std::vector<double>(numVars, 0.0),
        .col_ub = std::vector<double>(numVars, COIN_DBL_MAX),
        .obj = std::vector<double>(numVars, 0.0)
    };

    applyEdgeFeasibilityBounds(model, loop);
    appendGraphConstraints(model, loop->Nodes, loop->Edges, invocationCol);
    appendLoopBoundConstraint(model, loop, invocationCol);
    appendEqualityConstraint(model, invocationCol);
    fillObjectiveFunction(model, loop);

    return model;
}

ILPModel ILPBuilder::buildMonolithicILP(HLAC::FunctionNode *func) {
    const int numberOfEdgeVariables = ILPUtil::assignEdgeIndicesFunction(func, 0);
    const int numVars = assignLoopInvocationIndices(func, numberOfEdgeVariables);

    ILPModel model{
        .matrix = CoinPackedMatrix(false, 0, 0),
        .row_lb = {},
        .row_ub = {},
        .col_lb = std::vector<double>(numVars, 0.0),
        .col_ub = std::vector<double>(numVars, COIN_DBL_MAX),
        .obj = std::vector<double>(numVars, 0.0)
    };

    applyEdgeFeasibilityBounds(model, func);
    appendGraphConstraints(model, func->Nodes, func->Edges, -1);
    fillObjectiveFunction(model, func);

    return model;
}

std::unordered_map<HLAC::LoopNode *, ILPModel> ILPBuilder::buildClusteredILP(HLAC::FunctionNode *func) {
    std::unordered_map<HLAC::LoopNode *, ILPModel> resultMapping;

    const int numberOfEdgeVariables = ILPUtil::assignEdgeIndicesFunction(func, 0);
    assignLoopInvocationIndices(func, numberOfEdgeVariables);

    for (auto &nodeUniquePointer : func->Nodes) {
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode*>(nodeUniquePointer.get())) {
            ILPModel loopModel = buildMonolithicILP(loopNode);
            resultMapping[loopNode] = loopModel;
        }
    }

    return resultMapping;
}