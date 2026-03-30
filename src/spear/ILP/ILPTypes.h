
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_ILP_ILPTYPES_H_
#define SRC_SPEAR_ILP_ILPTYPES_H_

#include <unordered_map>
#include <vector>
#include <map>

#include <CoinPackedMatrix.hpp>


/**
 * Forward declarations of the HLAC namespace
 *
 */
namespace HLAC {
class FunctionNode;
class LoopNode;
class Edge;
class GenericNode;
}  // namespace HLAC

/**
 * Helper struct that represents a CBC ILP
 */
struct ILPModel {
    // Constraint matrix in sparse format (rows = constraints, columns = variables)
    CoinPackedMatrix matrix;

    // Lower bounds for each constraint row (left-hand side bounds)
    std::vector<double> row_lb;

    // Upper bounds for each constraint row (right-hand side bounds)
    std::vector<double> row_ub;

    // Lower bounds for each variable (column)
    std::vector<double> col_lb;

    // Upper bounds for each variable (column)
    std::vector<double> col_ub;

    // Objective function coefficients -> This is out mapping of cost per variable
    std::vector<double> obj;

    // Mapping from column index to corresponding HLAC edge
    std::vector<HLAC::Edge*> colToEdge;
};

// Type alias for a mapping from LoopNode pointers to their corresponding ILP models in the clustered ILP construction.
using ClusteredILPModel = std::unordered_map<HLAC::LoopNode *, ILPModel>;


/**
 * Struct representing the result of a ILP solving operation
 */
struct ILPResult {
    // Optimal result of the ILP solving. In our case the worst cast energy
    double optimalValue;
    // Variable assignment of the optimal solution. Here, how often an edge was taken in order to calculate the WCEC
    std::vector<double> variableValues;
};

// Type alias the mapping of LoopNode -> ILPModel
using ILPLoopModelMapping = std::unordered_map<HLAC::LoopNode *, ILPModel>;

// Type alias for the results exposed by the clustered ILP solving
using ILPClusteredLoopResult = std::unordered_map<HLAC::LoopNode *, ILPResult>;

/**
 * Struct representing the solution of the longest path calculation on the DAG representation of the function.
 * This is used to extract the worst-case path through the function after solving the clustered ILPs for the loops and
 * calculating the longest path through the DAG of the function.
 */
struct ILPLongestPathDAGSolution {
    // Mapping from node to its longest path distance from the exit node
    std::unordered_map<HLAC::GenericNode*, double> nodeDistances;

    // Mapping from node to its parent in the longest path tree
    std::unordered_map<HLAC::GenericNode*, HLAC::GenericNode*> nodeParents;
};

/**
 * Solution of the DAG longest path algorithm
 */
struct DAGLongestPathSolution {
    // Worst case energy of the longest path
    double WCEC;

    // Edges found on the longest path
    std::vector<HLAC::Edge *> longestPath;
};

// Adjacent representation of the HLAC graph, mapping from node to its adjacent edges.
using HLACAdjacentRepresentation = std::map<HLAC::GenericNode *, std::vector<HLAC::Edge *>>;

#endif  // SRC_SPEAR_ILP_ILPTYPES_H_
