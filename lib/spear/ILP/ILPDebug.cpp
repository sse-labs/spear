/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "ILP/ILPDebug.h"

#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <llvm/IR/BasicBlock.h>
#include <llvm/Support/raw_ostream.h>

#include "Logger.h"

std::string ILPDebug::basicBlockToDebugString(const llvm::BasicBlock *basicBlock) {
    if (basicBlock == nullptr) {
        return "<null>";
    }

    std::string output;
    llvm::raw_string_ostream outputStream(output);
    basicBlock->printAsOperand(outputStream, false);
    return outputStream.str();
}

std::string ILPDebug::genericNodeToDebugString(const HLAC::GenericNode *genericNode) {
    if (genericNode == nullptr) {
        return "<null>";
    }

    std::ostringstream outputStream;

    if (const auto *normalNode = dynamic_cast<const HLAC::Node *>(genericNode)) {
        outputStream << "Node(" << basicBlockToDebugString(normalNode->block) << ")";
        return outputStream.str();
    }

    if (dynamic_cast<const HLAC::CallNode *>(genericNode) != nullptr) {
        outputStream << "CallNode";
        return outputStream.str();
    }

    if (dynamic_cast<const HLAC::VirtualNode *>(genericNode) != nullptr) {
        outputStream << "VirtualNode";
        return outputStream.str();
    }

    if (dynamic_cast<const HLAC::LoopNode *>(genericNode) != nullptr) {
        outputStream << "LoopNode";
        return outputStream.str();
    }

    outputStream << "GenericNode";
    return outputStream.str();
}

std::string ILPDebug::edgeToDebugString(const HLAC::Edge *edge) {
    if (edge == nullptr) {
        return "<null-edge>";
    }

    std::ostringstream outputStream;
    outputStream << genericNodeToDebugString(edge->soure)
                 << " -> "
                 << genericNodeToDebugString(edge->destination)
                 << " [col=" << edge->ilpIndex << "]";
    return outputStream.str();
}

std::string ILPDebug::integerVectorToString(const std::vector<int> &values) {
    std::ostringstream outputStream;
    outputStream << "[";

    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            outputStream << ", ";
        }
        outputStream << values[index];
    }

    outputStream << "]";
    return outputStream.str();
}

const HLAC::Edge *ILPDebug::findEdgeByIlpIndex(
    const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
    int ilpIndex) {
    for (const auto &edgePointer : edges) {
        if (edgePointer != nullptr && edgePointer->ilpIndex == ilpIndex) {
            return edgePointer.get();
        }
    }

    return nullptr;
}

std::string ILPDebug::debugRowToString(
    const CoinPackedVector &row,
    double lowerBound,
    double upperBound,
    const std::vector<std::unique_ptr<HLAC::Edge>> &edges) {
    std::ostringstream outputStream;

    for (int elementIndex = 0; elementIndex < row.getNumElements(); ++elementIndex) {
        if (elementIndex != 0) {
            outputStream << " ";
        }

        const int columnIndex = row.getIndices()[elementIndex];
        const double coefficient = row.getElements()[elementIndex];
        const HLAC::Edge *edge = findEdgeByIlpIndex(edges, columnIndex);

        outputStream << "("
                     << coefficient
                     << " * x" << columnIndex
                     << " = " << edgeToDebugString(edge)
                     << ")";
    }

    outputStream << " in [" << lowerBound << ", " << upperBound << "]";
    return outputStream.str();
}

std::string ILPDebug::formatBound(double boundValue) {
    if (boundValue == std::numeric_limits<double>::max()) {
        return "inf";
    }

    if (boundValue == -std::numeric_limits<double>::max()) {
        return "-inf";
    }

    return std::to_string(boundValue);
}

std::string ILPDebug::formatCoefficient(double coefficientValue) {
    std::ostringstream outputStream;
    outputStream << std::scientific << std::setprecision(12) << coefficientValue;
    return outputStream.str();
}

void ILPDebug::dumpILPModel(
    const ILPModel &model,
    const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
    const std::string &name) {
    Logger::getInstance().log("========== ILP DUMP BEGIN: " + name + " ==========", LOGLEVEL::INFO);

    const int numberOfColumns = model.matrix.getNumCols();
    const int numberOfRows = model.matrix.getNumRows();

    Logger::getInstance().log("Variables:", LOGLEVEL::INFO);
    for (int columnIndex = 0; columnIndex < numberOfColumns; ++columnIndex) {
        std::string edgeInfo = "<no-edge>";

        for (const auto &edgePointer : edges) {
            const auto *edge = edgePointer.get();
            if (edge != nullptr && edge->ilpIndex == columnIndex) {
                edgeInfo = edgeToDebugString(edge);
                break;
            }
        }

        Logger::getInstance().log(
            "  x" + std::to_string(columnIndex) +
            " lb=" + formatBound(model.col_lb[columnIndex]) +
            " ub=" + formatBound(model.col_ub[columnIndex]) +
            " obj=" + formatCoefficient(model.obj[columnIndex]) +
            " :: " + edgeInfo,
            LOGLEVEL::INFO);
    }

    Logger::getInstance().log("Constraints:", LOGLEVEL::INFO);

    const CoinPackedMatrix *matrixToRead = &model.matrix;
    CoinPackedMatrix rowOrderedCopy;

    // We want to iterate rows. If the matrix is column-ordered, create a row-ordered copy first.
    if (model.matrix.isColOrdered()) {
        rowOrderedCopy.reverseOrderedCopyOf(model.matrix);
        matrixToRead = &rowOrderedCopy;
    }

    const int *vectorStarts = matrixToRead->getVectorStarts();
    const int *vectorLengths = matrixToRead->getVectorLengths();
    const int *indices = matrixToRead->getIndices();
    const double *elements = matrixToRead->getElements();

    if (vectorStarts == nullptr || vectorLengths == nullptr || indices == nullptr || elements == nullptr) {
        Logger::getInstance().log("  <failed to access matrix storage>", LOGLEVEL::ERROR);
        Logger::getInstance().log("========== ILP DUMP END: " + name + " ==========", LOGLEVEL::INFO);
        return;
    }

    for (int rowIndex = 0; rowIndex < numberOfRows; ++rowIndex) {
        std::string rowString = "  row[" + std::to_string(rowIndex) + "]: ";

        const int startOffset = vectorStarts[rowIndex];
        const int rowLength = vectorLengths[rowIndex];

        for (int elementOffset = 0; elementOffset < rowLength; ++elementOffset) {
            const int storageIndex = startOffset + elementOffset;
            const int columnIndex = indices[storageIndex];
            const double coefficient = elements[storageIndex];

            rowString += "(" + std::to_string(coefficient) + " * x" + std::to_string(columnIndex) + ") ";
        }

        rowString += "in [" + std::to_string(model.row_lb[rowIndex]) +
                     ", " + std::to_string(model.row_ub[rowIndex]) + "]";

        Logger::getInstance().log(rowString, LOGLEVEL::INFO);
    }

    Logger::getInstance().log("========== ILP DUMP END: " + name + " ==========", LOGLEVEL::INFO);
}