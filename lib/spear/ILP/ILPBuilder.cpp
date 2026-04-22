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

namespace {

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

std::string edgeToDebugString(const HLAC::Edge *edge) {
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

std::string integerVectorToString(const std::vector<int> &values) {
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

}  // namespace

std::string debugGenericNodeToString(const HLAC::GenericNode *genericNode) {
    if (genericNode == nullptr) {
        return "<null>";
    }

    if (const auto *normalNode = dynamic_cast<const HLAC::Node *>(genericNode)) {
        return basicBlockToDebugString(normalNode->block);
    }

    if (const auto *callNode = dynamic_cast<const HLAC::CallNode *>(genericNode)) {
        if (callNode->parentFunctionNode->function != nullptr && callNode->calledFunction != nullptr) {
            return "Call(" + callNode->calledFunction->getName().str() + ")";
        }
        return "CallNode";
    }

    if (const auto *virtualNode = dynamic_cast<const HLAC::VirtualNode *>(genericNode)) {
        if (virtualNode->isEntry) {
            return "VEntry";
        }
        if (virtualNode->isExit) {
            return "VExit";
        }
        return "VirtualNode";
    }

    if (dynamic_cast<const HLAC::LoopNode *>(genericNode) != nullptr) {
        return "LoopNode";
    }

    return "GenericNode";
}

const HLAC::Edge *findEdgeByIlpIndex(
    const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
    int ilpIndex) {
    for (const auto &edgeUP : edges) {
        if (edgeUP != nullptr && edgeUP->ilpIndex == ilpIndex) {
            return edgeUP.get();
        }
    }

    return nullptr;
}

std::string debugRowToString(
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

std::string debugEdgeToString(const HLAC::Edge *edge) {
    if (!edge) {
        return "<null-edge>";
    }

    std::ostringstream os;

    auto src = edge->soure;
    auto dst = edge->destination;

    os << (src ? src->getDotName() : "null")
       << " -> "
       << (dst ? dst->getDotName() : "null")
       << " [col=" << edge->ilpIndex << "]";

    return os.str();
}

// Helper function for readable bounds
std::string formatBound(double boundValue) {
    // Check for "infinite" upper bound
    if (boundValue == std::numeric_limits<double>::max()) {
        return "inf";  // or "∞" if you prefer
    }

    return std::to_string(boundValue);
}


std::string formatCoefficient(double coefficientValue) {
    std::ostringstream outputStream;
    outputStream << std::scientific << std::setprecision(12) << coefficientValue;
    return outputStream.str();
}

void ILPBuilder::debugDumpILPModel(
    const ILPModel &model,
    const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
    const std::string &name) {

    Logger::getInstance().log("========== ILP DUMP BEGIN: " + name + " ==========", LOGLEVEL::INFO);

    const int numberOfColumns = model.matrix.getNumCols();
    const int numberOfRows = model.matrix.getNumRows();

    Logger::getInstance().log("Variables:", LOGLEVEL::INFO);
    for (int columnIndex = 0; columnIndex < numberOfColumns; ++columnIndex) {
        std::string edgeInfo = "<no-edge>";

        for (const auto &edgeUP : edges) {
            const auto *edge = edgeUP.get();
            if (edge != nullptr && edge->ilpIndex == columnIndex) {
                edgeInfo = debugEdgeToString(edge);
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
    std::unordered_map<HLAC::GenericNode *, std::vector<int>> incomingEdgesPerNode;
    std::unordered_map<HLAC::GenericNode *, std::vector<int>> outgoingEdgesPerNode;

    // Build the indices for all contained edges
    ILPUtil::buildIncidenceMaps(
        edges,
        incomingEdgesPerNode,
        outgoingEdgesPerNode,
        static_cast<int>(model.col_lb.size()));

    auto basicBlockToDebugString = [](const llvm::BasicBlock *basicBlock) -> std::string {
        if (basicBlock == nullptr) {
            return "<null>";
        }

        std::string outputString;
        llvm::raw_string_ostream outputStream(outputString);
        basicBlock->printAsOperand(outputStream, false);
        return outputStream.str();
    };

    auto genericNodeToDebugString = [&](const HLAC::GenericNode *genericNode) -> std::string {
        if (genericNode == nullptr) {
            return "<null>";
        }

        if (const auto *normalNode = dynamic_cast<const HLAC::Node *>(genericNode)) {
            return basicBlockToDebugString(normalNode->block);
        }

        if (const auto *callNode = dynamic_cast<const HLAC::CallNode *>(genericNode)) {
            if (callNode->calledFunction != nullptr) {
                return "Call(" + callNode->calledFunction->getName().str() + ")";
            }
            return "CallNode";
        }

        if (const auto *virtualNode = dynamic_cast<const HLAC::VirtualNode *>(genericNode)) {
            if (virtualNode->isEntry) {
                return "VEntry";
            }
            if (virtualNode->isExit) {
                return "VExit";
            }
            return "VirtualNode";
        }

        if (dynamic_cast<const HLAC::LoopNode *>(genericNode) != nullptr) {
            return "LoopNode";
        }

        return "GenericNode";
    };

    auto findEdgeByIlpIndex = [&](int ilpIndex) -> const HLAC::Edge * {
        for (const auto &edgeUP : edges) {
            if (edgeUP != nullptr && edgeUP->ilpIndex == ilpIndex) {
                return edgeUP.get();
            }
        }

        return nullptr;
    };

    auto edgeToDebugString = [&](const HLAC::Edge *edge) -> std::string {
        if (edge == nullptr) {
            return "<null-edge>";
        }

        return genericNodeToDebugString(edge->soure) +
               " -> " +
               genericNodeToDebugString(edge->destination) +
               " [col=" + std::to_string(edge->ilpIndex) + "]";
    };

    auto integerVectorToString = [](const std::vector<int> &values) -> std::string {
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
    };

    auto rowToDebugString = [&](const CoinPackedVector &row, double lowerBound, double upperBound) -> std::string {
        std::ostringstream outputStream;

        for (int elementIndex = 0; elementIndex < row.getNumElements(); ++elementIndex) {
            if (elementIndex != 0) {
                outputStream << " ";
            }

            const int columnIndex = row.getIndices()[elementIndex];
            const double coefficient = row.getElements()[elementIndex];
            const HLAC::Edge *edge = findEdgeByIlpIndex(columnIndex);

            outputStream << "("
                         << coefficient
                         << " * x" << columnIndex
                         << " = " << edgeToDebugString(edge)
                         << ")";
        }

        outputStream << " in [" << lowerBound << ", " << upperBound << "]";
        return outputStream.str();
    };

    Logger::getInstance().log("========== GRAPH CONSTRAINT DEBUG BEGIN ==========", LOGLEVEL::INFO);

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

        const std::vector<int> incomingEdges =
            incomingEdgesPerNode.contains(node) ? incomingEdgesPerNode[node] : std::vector<int>{};
        const std::vector<int> outgoingEdges =
            outgoingEdgesPerNode.contains(node) ? outgoingEdgesPerNode[node] : std::vector<int>{};

        Logger::getInstance().log(
            "Node: " + genericNodeToDebugString(node),
            LOGLEVEL::INFO);
        Logger::getInstance().log(
            "  Incoming cols: " + integerVectorToString(incomingEdges),
            LOGLEVEL::INFO);
        for (int col : incomingEdges) {
            Logger::getInstance().log(
                "    IN  " + edgeToDebugString(findEdgeByIlpIndex(col)),
                LOGLEVEL::INFO);
        }

        Logger::getInstance().log(
            "  Outgoing cols: " + integerVectorToString(outgoingEdges),
            LOGLEVEL::INFO);
        for (int col : outgoingEdges) {
            Logger::getInstance().log(
                "    OUT " + edgeToDebugString(findEdgeByIlpIndex(col)),
                LOGLEVEL::INFO);
        }

        /**
         * Additionally, we need to handle our virtual nodes that represent entry and exit points of functions and loops.
         * For these nodes, we need to add the constraint that they will be executed at least once.
         *
         */
        if (auto *virtualNode = dynamic_cast<HLAC::VirtualNode *>(node)) {
            if (virtualNode->isEntry) {
                for (int col : outgoingEdges) {
                    ILPUtil::insertUnique(row, usedCols, col, 1.0);
                }

                if (invocationCols == nullptr) {
                    Logger::getInstance().log(
                        "  Appending top-level entry row: " + rowToDebugString(row, 1.0, 1.0),
                        LOGLEVEL::INFO);
                    // Top-level function entry: exactly one entry
                    ILPUtil::appendRow(model, row, 1.0, 1.0);
                } else {
                    // Loop-internal entry: equals outer loop invocation count
                    for (int col : *invocationCols) {
                        ILPUtil::insertUnique(row, usedCols, col, -1.0);
                    }

                    Logger::getInstance().log(
                        "  Appending loop entry row: " + rowToDebugString(row, 0.0, 0.0),
                        LOGLEVEL::INFO);
                    ILPUtil::appendRow(model, row, 0.0, 0.0);
                }

                continue;
            }

            if (virtualNode->isExit) {
                for (int col : incomingEdges) {
                    ILPUtil::insertUnique(row, usedCols, col, 1.0);
                }

                if (invocationCols == nullptr) {
                    Logger::getInstance().log(
                        "  Appending top-level exit row: " + rowToDebugString(row, 1.0, 1.0),
                        LOGLEVEL::INFO);
                    // Top-level function exit: exactly one completed path
                    ILPUtil::appendRow(model, row, 1.0, 1.0);
                } else {
                    // Loop-internal exit: equals outer loop invocation count
                    for (int col : *invocationCols) {
                        ILPUtil::insertUnique(row, usedCols, col, -1.0);
                    }

                    Logger::getInstance().log(
                        "  Appending loop exit row: " + rowToDebugString(row, 0.0, 0.0),
                        LOGLEVEL::INFO);
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
        for (int col : incomingEdges) {
            ILPUtil::insertUnique(row, usedCols, col, 1.0);
        }

        for (int col : outgoingEdges) {
            ILPUtil::insertUnique(row, usedCols, col, -1.0);
        }

        Logger::getInstance().log(
            "  Appending flow row: " + rowToDebugString(row, 0.0, 0.0),
            LOGLEVEL::INFO);

        // Each edge constraint has to equal 0, as the amount of incoming flow has to equal the amount of outgoing flow
        ILPUtil::appendRow(model, row, 0.0, 0.0);

        if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(node)) {
            const std::vector<int> outerIncoming = incomingEdgesPerNode[node];

            Logger::getInstance().log(
                "  Descending into loop node with incoming cols " + integerVectorToString(outerIncoming),
                LOGLEVEL::INFO);

            // Add all internal flow constraints of the loop.
            appendGraphConstraints(model, loopNode->Nodes, loopNode->Edges, &outerIncoming);

            // Add bound constraint for the loop.
            appendLoopBoundConstraint(model, loopNode, outerIncoming);
        }
    }

    Logger::getInstance().log("========== GRAPH CONSTRAINT DEBUG END ==========", LOGLEVEL::INFO);
}

void ILPBuilder::appendLoopBoundConstraint(
    ILPModel &model,
    HLAC::LoopNode *loopNode,
    const std::vector<int> &invocationCols) {

    const std::string functionName =
        (loopNode != nullptr && loopNode->parentFunction != nullptr && loopNode->parentFunction->function != nullptr)
            ? loopNode->parentFunction->function->getName().str()
            : "<unknown-function>";

    const std::string headerName =
        (loopNode != nullptr && loopNode->loop != nullptr)
            ? basicBlockToDebugString(loopNode->loop->getHeader())
            : "<unknown-header>";

    if (loopNode == nullptr) {
        Logger::getInstance().log(
            "Loop bound debug: loopNode is null.",
            LOGLEVEL::ERROR
        );
        return;
    }

    const auto lowerBound = loopNode->bounds.getLowerBound();
    const auto upperBound = loopNode->bounds.getUpperBound();

    if (loopNode->backEdges.empty()) {
        Logger::getInstance().log(
            "Loop bound debug: no backedges found for loop " + loopNode->getDotName(),
            LOGLEVEL::ERROR
        );
        return;
    }

    for (std::size_t backEdgeIndex = 0; backEdgeIndex < loopNode->backEdges.size(); ++backEdgeIndex) {
        HLAC::Edge *backEdge = loopNode->backEdges[backEdgeIndex];
    }

    const double lowerBoundAsDouble = static_cast<double>(lowerBound);
    const double upperBoundAsDouble = static_cast<double>(upperBound);

    CoinPackedVector upperBoundRow;
    std::unordered_set<int> upperBoundUsedColumns;

    for (HLAC::Edge *backEdge : loopNode->backEdges) {
        if (backEdge == nullptr) {
            continue;
        }

        ILPUtil::insertUnique(upperBoundRow, upperBoundUsedColumns, backEdge->ilpIndex, 1.0);
    }

    for (int invocationColumn : invocationCols) {
        ILPUtil::insertUnique(upperBoundRow, upperBoundUsedColumns, invocationColumn, -upperBoundAsDouble);
    }

    ILPUtil::appendRow(model, upperBoundRow, -COIN_DBL_MAX, 0.0);

    CoinPackedVector lowerBoundRow;
    std::unordered_set<int> lowerBoundUsedColumns;

    for (HLAC::Edge *backEdge : loopNode->backEdges) {
        if (backEdge == nullptr) {
            continue;
        }

        ILPUtil::insertUnique(lowerBoundRow, lowerBoundUsedColumns, backEdge->ilpIndex, 1.0);
    }

    for (int invocationColumn : invocationCols) {
        ILPUtil::insertUnique(lowerBoundRow, lowerBoundUsedColumns, invocationColumn, -lowerBoundAsDouble);
    }

    ILPUtil::appendRow(model, lowerBoundRow, 0.0, COIN_DBL_MAX);
}

void ILPBuilder::fillObjectiveFunction(ILPModel &model, HLAC::FunctionNode *func) {
    // For all edges in this functionnode set the objective vector values
    for (auto &edgeUP : func->Edges) {
        auto *edge = edgeUP.get();

        auto cacheIterator = func->directNodeEnergyCache.find(edge->destination);
        if (cacheIterator != func->directNodeEnergyCache.end()) {
            model.obj[edge->ilpIndex] = cacheIterator->second;
        } else {
            // Fall back to the live node energy for nodes that are intentionally
            // not cached here, such as call nodes filled later.
            model.obj[edge->ilpIndex] = edge->destination->getEnergy();
        }
    }

    // Then we need to check all contained loopnodes
    for (auto &nodeUP : func->Nodes) {
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(nodeUP.get())) {
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


std::optional<ILPResult> ILPBuilder::solveModel(const ILPModel& ilpModel) {
    // Create a new solver on the model
    ILPSolver modelSolver(ilpModel);

    // Solve the model
    auto optimalSolution = modelSolver.getSolvedModelValue();
    auto optimalPath = modelSolver.getSolvedSolution();

    // Check if a valid solution exists
    if (optimalPath.has_value() && optimalSolution.has_value()) {
        return std::make_optional<ILPResult>(optimalSolution.value(), optimalPath.value());
    }

    // --- Failure handling and logging ---

    auto solverStatus = modelSolver.getStatus();
    auto statusString = modelSolver.getStatusString();

    std::cout << "[ILP ERROR] Solver failed.\n";
    std::cout << "  Status: " << statusString << "\n";

    // Optional: more diagnostics
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

    /**
     * The Invocation col is the artificial variable we are introducing
     */
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

    /*Logger::getInstance().log(
        "Loop " + loop->getDotName() +
        " bounds = [" + std::to_string(loop->bounds.getLowerBound()) +
        ", " + std::to_string(loop->bounds.getUpperBound()) + "]",
        LOGLEVEL::INFO
    );*/

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
