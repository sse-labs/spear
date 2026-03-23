/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "ILP/ILPBuilder.h"
#include "ILP/ILPSolver.h"
#include  "ILP/ILPUtil.h"
#include "HLAC/hlac.h"

#include <unordered_set>


ILPBuilder::ILPBuilder() {
    // Nothing to do here...
}

int ILPBuilder::assignEdgeIndicesFunction(HLAC::FunctionNode *func, int nextIndex) {
    // Iterate over the edges in this function and assign them an index
    for (auto &edgeUP : func->Edges) {
        edgeUP->ilpIndex = nextIndex++;
    }

    for (auto &nodeUP : func->Nodes) {
        // For loopnodes we need to consider the contained edges. Therefore we need to index them as well
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode*>(nodeUP.get())) {
            nextIndex = assignEdgeIndicesLoop(loopNode, nextIndex);
        }
    }

    // For the recursive behavior we need to return the last used index
    return nextIndex;
}

int ILPBuilder::assignEdgeIndicesFunction(HLAC::LoopNode *loop, int nextIndex) {
    // Iterate over the edges in this function and assign them an index
    for (auto &edgeUP : loop->Edges) {
        edgeUP->ilpIndex = nextIndex++;
    }

    for (auto &nodeUP : loop->Nodes) {
        // For loopnodes we need to consider the contained edges. Therefore we need to index them as well
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode*>(nodeUP.get())) {
            nextIndex = assignEdgeIndicesLoop(loopNode, nextIndex);
        }
    }

    // For the recursive behavior we need to return the last used index
    return nextIndex;
}

int ILPBuilder::assignEdgeIndicesLoop(HLAC::LoopNode *loopNode, int nextIndex) {
    // Index the edges contained in this loop
    for (auto &edgeUP : loopNode->Edges) {
        edgeUP->ilpIndex = nextIndex++;
    }

    for (auto &nodeUP : loopNode->Nodes) {
        if (auto *innerLoop = dynamic_cast<HLAC::LoopNode*>(nodeUP.get())) {
            // Index the subloops edges
            nextIndex = assignEdgeIndicesLoop(innerLoop, nextIndex);
        }
    }

    // Return the last index for the recursive behavior
    return nextIndex;
}

void ILPBuilder::buildIncidenceMaps(
    const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
    std::unordered_map<HLAC::GenericNode*, std::vector<int>> &incoming,
    std::unordered_map<HLAC::GenericNode*, std::vector<int>> &outgoing) {
    // Check that all incoming and outgoing vectors are empty
    incoming.clear();
    outgoing.clear();

    // Check each edge contained in this scope
    for (const auto &edgeUP : edges) {
        // Get the current edge
        auto *edge = edgeUP.get();

        if (!edge) {
            continue;
        }

        // Check that the index of the edge is valid
        if (edge->ilpIndex < 0) {
            std::cerr << "Error: edge without valid ilpIndex encountered.\n";
            continue;
        }

        // Add the nodes to the respective vectors
        incoming[edge->destination].push_back(edge->ilpIndex);
        outgoing[edge->soure].push_back(edge->ilpIndex);
    }
}

void ILPBuilder::appendRow(ILPModel &model, const CoinPackedVector &row, double lb, double ub) {
    model.matrix.appendRow(row);
    model.row_lb.push_back(lb);
    model.row_ub.push_back(ub);
}

void ILPBuilder::insertUnique(
    CoinPackedVector &row,
    std::unordered_set<int> &used,
    int col,
    double coeff,
    const std::string &context) {

    // Check that we do not add columns that are already contained in the vector
    if (!used.insert(col).second) {
        std::cerr << "Warning: duplicate column " << col
                  << " while building row for " << context << '\n';
        return;
    }

    // Inser the row and the respective coefficient into the vector
    row.insert(col, coeff);
}

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
    buildIncidenceMaps(edges, incomingEdgesPerNode, outgoingEdgesPerNode);

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
                    insertUnique(row, usedCols, col, 1.0, node->name);
                }

                if (invocationCols == nullptr) {
                    // Top-level function entry: exactly one entry
                    appendRow(model, row, 1.0, 1.0);
                } else {
                    // Loop-internal entry: equals outer loop invocation count
                    for (int col : *invocationCols) {
                        insertUnique(row, usedCols, col, -1.0, node->name);
                    }
                    appendRow(model, row, 0.0, 0.0);
                }

                continue;
            }

            if (virtualNode->isExit) {
                for (int col : incomingEdgesPerNode[node]) {
                    insertUnique(row, usedCols, col, 1.0, node->name);
                }

                if (invocationCols == nullptr) {
                    // Top-level function exit: exactly one completed path
                    appendRow(model, row, 1.0, 1.0);
                } else {
                    // Loop-internal exit: equals outer loop invocation count
                    for (int col : *invocationCols) {
                        insertUnique(row, usedCols, col, -1.0, node->name);
                    }
                    appendRow(model, row, 0.0, 0.0);
                }

                continue;
            }
        }


        for (int col : incomingEdgesPerNode[node]) {
            insertUnique(row, usedCols, col, 1.0, node->name);
        }

        for (int col : outgoingEdgesPerNode[node]) {
            insertUnique(row, usedCols, col, -1.0, node->name);
        }

        appendRow(model, row, 0.0, 0.0);


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

        insertUnique(row, usedCols, backCol, 1.0, "Loop upper bound for " + loopNode->getDotName());

        for (int col : invocationCols) {
            insertUnique(row, usedCols, col, -ub, "Loop upper bound for " + loopNode->getDotName());
        }

        appendRow(model, row, -COIN_DBL_MAX, 0.0);
    }

    // Lower bound:
    // x_back - lb * sum(invocations) >= 0
    {
        CoinPackedVector row;
        std::unordered_set<int> usedCols;

        insertUnique(row, usedCols, backCol, 1.0, "Loop lower bound for " + loopNode->getDotName());

        for (int col : invocationCols) {
            insertUnique(row, usedCols, col, -lb, "Loop lower bound for " + loopNode->getDotName());
        }

        appendRow(model, row, 0.0, COIN_DBL_MAX);
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


std::optional<std::pair<double, std::vector<double>>> ILPBuilder::solveModel(ILPModel ilpModel) {
    ILPSolver modelSolver(ilpModel);
    auto optimalSolution = modelSolver.getSolvedModelValue();
    auto optimalPath = modelSolver.getSolvedSolution();

    if (optimalPath.has_value() && optimalSolution.has_value()) {
        return std::make_pair(optimalSolution.value(), optimalPath.value());
    }

    return std::nullopt;
}

ILPModel ILPBuilder::appendLoopNodeContents(ILPModel model, HLAC::LoopNode *loopNode) {
    for (auto &node : loopNode->Nodes) {
        // We need to check, if the loopNode itsselfs contains another loopnode. In this case we have to call this
        // function recursively.
        if (auto *innerLoopNode = dynamic_cast<HLAC::LoopNode*>(node.get())) {
            model = appendLoopNodeContents(model, innerLoopNode);
        } else {
            // We need to add the constraints of the node to the model
            // We map edges back to nodes

            auto &nodes = loopNode->Nodes;
            auto &edges = loopNode->Edges;

            // Each node stores the id of the incoming and outgoing edges
            std::unordered_map<HLAC::GenericNode*, std::vector<int>> incomingEdgesPerNode;
            std::unordered_map<HLAC::GenericNode*, std::vector<int>> outgoingEdgesPerNode;

            for (int i = 0; i < edges.size(); i++) {
                auto e = edges[i].get();

                incomingEdgesPerNode[e->destination].push_back(i);
                outgoingEdgesPerNode[e->soure].push_back(i);
            }


            for (int n = 0; n < nodes.size(); ++n) {
                auto *localNode = nodes[n].get();

                CoinPackedVector row;

                // Check if we have a virtual node
                if (auto *virtualNode = dynamic_cast<HLAC::VirtualNode*>(localNode)) {
                    if (virtualNode->isEntry) {
                        for (int e : outgoingEdgesPerNode[localNode]) {
                            row.insert(e, 1.0);
                        }
                        model.matrix.appendRow(row);
                        model.row_lb.push_back(1.0);
                        model.row_ub.push_back(1.0);
                        continue;
                    }

                    if (virtualNode->isExit) {
                        for (int e : incomingEdgesPerNode[localNode]) {
                            row.insert(e, 1.0);
                        }
                        model.matrix.appendRow(row);
                        model.row_lb.push_back(1.0);
                        model.row_ub.push_back(1.0);
                        continue;
                    }
                }

                for (int e : incomingEdgesPerNode[localNode]) {
                    // std::cout << "incoming index " << e << " of node " << localNode->name << std::endl;
                    row.insert(e, 1.0);
                }

                for (int e : outgoingEdgesPerNode[localNode]) {
                    // std::cout << "outgoing index " << e << " of node " << localNode->name << std::endl;
                    row.insert(e, -1.0);
                }

                model.matrix.appendRow(row);
                model.row_lb.push_back(0.0);
                model.row_ub.push_back(0.0);
            }

            // Push costs
            for (int e=0; e<edges.size(); e++) {
                // If we traverse an edge, we execute the destination node and therefore we need to pay the energy cost of the destination node
                model.obj[e] = edges[e].get()->destination->getEnergy();
            }
        }
    }

    return model;
}

void ILPBuilder::appendLoopBoundConstraint(ILPModel &model, HLAC::LoopNode *loopNode) {
    if (loopNode->backEdge == nullptr) {
        std::cerr << "Warning: Loop " << loopNode->getDotName()
                  << " has no backedge, skipping loop bound constraint.\n";
        return;
    }

    const int backCol = loopNode->backEdge->ilpIndex;

    if (backCol < 0 || backCol >= static_cast<int>(model.obj.size())) {
        std::cerr << "Warning: invalid backedge ilpIndex for loop "
                  << loopNode->getDotName() << '\n';
        return;
    }

    CoinPackedVector row;
    std::unordered_set<int> usedCols;

    insertUnique(row, usedCols, backCol, 1.0,
                 "Standalone loop bound for " + loopNode->getDotName());

    // backedge counts re-iterations
    const double lb = std::max(0.0, static_cast<double>(loopNode->bounds.getLowerBound()));
    const double ub = std::max(0.0, static_cast<double>(loopNode->bounds.getUpperBound()));

    appendRow(model, row, lb, ub);
}

ILPModel ILPBuilder::buildMonolithicILP(HLAC::LoopNode *loop) {
    // std::cout << "Building ILP for function " << func->function->getName().str() << std::endl;

    // Assign global ILP column indices to every edge recursively.
    const int numVars = assignEdgeIndicesFunction(loop, 0);

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
    appendGraphConstraints(model, loop->Nodes, loop->Edges, nullptr);

    // As we are already in a loop, we need to append all loop bound constrains right here
    // We calculate loop constrains via the scale of how often the loopnode will be entered,
    // This works perfectly fine for monolithic ILP calculation, where all constrains exit.
    // However, for clustered ILP construction, we have to assume that the top level loop(and only the top level loop)
    // Will be entered exactly one time.
    appendLoopBoundConstraint(model, loop);


    // Fill objective recursively with energy cost
    fillObjectiveFunction(model, loop);



    return model;
}

ILPModel ILPBuilder::buildMonolithicILP(HLAC::FunctionNode *func) {
    // std::cout << "Building ILP for function " << func->function->getName().str() << std::endl;

    // Assign global ILP column indices to every edge recursively.
    const int numVars = assignEdgeIndicesFunction(func, 0);

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

    ILPUtil::printILPModelHumanReadable(func->name, model);


    return model;
}

std::map<HLAC::LoopNode *, ILPModel> ILPBuilder::buildClusteredILP(HLAC::FunctionNode *func) {
    /**
     * In contrary to the monolithic ILP, we do not build one big ILP for the whole function,
     * but instead we build separate ILPs for each loop in the function and use a generic graph algorithm for finding the
     * worst path in the function
     */
    std::map<HLAC::LoopNode *, ILPModel> resultMapping;

    for (auto &nodeUP : func->Nodes) {
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode*>(nodeUP.get())) {
            // Build the ILP for the loop node
            ILPModel loopModel = buildMonolithicILP(loopNode);

            ILPUtil::printILPModelHumanReadable(func->name, loopNode->loop->getName().str(), loopModel);

            // Store the model in the result mapping
            resultMapping[loopNode] = loopModel;
        }
    }

    return resultMapping;
}