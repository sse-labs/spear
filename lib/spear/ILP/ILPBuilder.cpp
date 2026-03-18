/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */


#include "ILP/ILPBuilder.h"

ILPBuilder::ILPBuilder() {  }

ILPModel ILPBuilder::buildMonolithicILP(HLAC::FunctionNode *func) {
    auto &nodes = func->Nodes;
    auto &edges = func->Edges;

    // Matrix where we store the constrains
    CoinPackedMatrix matrix(false, 0, 0);

    // Bounds of the variables
    // CBC models them as lower and upper bound to express lb < Ax < ub
    std::vector<double> row_lb;
    std::vector<double> row_ub;

    int numVars = static_cast<int>(func->Edges.size());

    std::vector<double> col_lb(numVars, 0.0);
    std::vector<double> col_ub(numVars, COIN_DBL_MAX);
    std::vector<double> obj(numVars, 0.0);

    if (func->name == "main") {
        std::cout << "=========================" << std::endl;


        // We map edges back to nodes
        // Each node stores the id of the incoming and outgoing edges
        std::map<std::string, std::vector<int>> incomingEdgesPerNode;
        std::map<std::string, std::vector<int>> outgoingEdgesPerNode;

        for (int i = 0; i < edges.size(); i++) {
            auto e = edges[i].get();

            incomingEdgesPerNode[e->destination->name].push_back(i);
            outgoingEdgesPerNode[e->soure->name].push_back(i);
        }


        for (auto &node : nodes) {
            std::cout << "Node " << node.get()->name << "\n";
            std::cout << "Outgoing edges: " << std::endl;
            for (auto &target : outgoingEdgesPerNode[node.get()->name]) {
                std::cout << target << ", ";
            }
            std::cout << "\n";
            std::cout << "Incoming edges: " << std::endl;
            for (auto &e : incomingEdgesPerNode[node.get()->name]) {
                std::cout << e << ", ";
            }
            std::cout << "\n";
        }

        for (int n = 0; n < nodes.size(); ++n) {
            auto *node = nodes[n].get();

            CoinPackedVector row;

            // Check if we have a virtual node
            if (auto *virtualNode = dynamic_cast<HLAC::VirtualNode*>(node)) {
                if (virtualNode->isEntry) {
                    for (int e : outgoingEdgesPerNode[node->name]) {
                        row.insert(e, 1.0);
                    }
                    matrix.appendRow(row);
                    row_lb.push_back(1.0);
                    row_ub.push_back(1.0);
                    continue;
                }

                if (virtualNode->isExit) {
                    for (int e : incomingEdgesPerNode[node->name]) {
                        row.insert(e, 1.0);
                    }
                    matrix.appendRow(row);
                    row_lb.push_back(1.0);
                    row_ub.push_back(1.0);
                    continue;
                }
            }

            for (int e : incomingEdgesPerNode[node->name]) {
                row.insert(e, 1.0);
            }

            for (int e : outgoingEdgesPerNode[node->name]) {
                row.insert(e, -1.0);
            }

            matrix.appendRow(row);
            row_lb.push_back(0.0);
            row_ub.push_back(0.0);
        }

        // Push costs
        for (int e=0; e<edges.size(); e++) {
            // If we traverse an edge, we execute the destination node and therefore we need to pay the energy cost of the destination node
            obj[e] = edges[e].get()->destination->getEnergy();
        }

        std::cout << "=========================" << std::endl;
    }

    for (int e = 0; e < edges.size(); ++e) {
        std::cout << "obj[" << e << "] = " << obj[e]
                  << " edge: " << edges[e]->soure->name
                  << " -> " << edges[e]->destination->name << "\n";
    }

    return ILPModel{
        .matrix = matrix,
        .row_lb = row_lb,
        .row_ub = row_ub,
        .col_lb = col_lb,
        .col_ub = col_ub,
        .obj = obj
    };
}

ILPModel ILPBuilder::buildClusteredILP(HLAC::FunctionNode *func) {
    CoinPackedMatrix matrix(false, 0, 0);

    // Bounds of the variables
    // CBC models them as lower and upper bound to express lb < Ax < ub
    std::vector<double> row_lb;
    std::vector<double> row_ub;

    int numVars = static_cast<int>(func->Edges.size());

    std::vector<double> col_lb(numVars, 0.0);
    std::vector<double> col_ub(numVars, COIN_DBL_MAX);
    std::vector<double> obj(numVars, 0.0);

    return ILPModel{
        .matrix = matrix,
        .row_lb = row_lb,
        .row_ub = row_ub,
        .col_lb = col_lb,
        .col_ub = col_ub,
        .obj = obj
    };
}