/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_ILP_ILPUTIL_H_
#define SRC_SPEAR_ILP_ILPUTIL_H_

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include <CoinFinite.hpp>
#include <CoinPackedMatrix.hpp>
#include "HLAC/hlac.h"

/**
 * ILPUtil class
 * Exposes several helper methods used to interact with ILP solving
*/
class ILPUtil {
 public:
    /**
     * Print the ILP-Model of the loop with reference to the name of the parent function
     * @param funcname Name of the function the loop originates from
     * @param loopname Name of the loop for which we are printing the model
     * @param model Model to print
     */
    static void printILPModelHumanReadable(std::string funcname, std::string loopname, const ILPModel &model);

    /**
     * Print the ILP-Model with reference to the name of the parent function
     * @param funcname Name of the function
     * @param model Model to print
     */
    static void printILPModelHumanReadable(std::string funcname, const ILPModel &model);

    /**
     * Calculate the longest path (= the energetically most expensive path) in the given FunctionNode. Uses the
     * loopMapping to fill in the energy cost of loops
     * @param func FunctionNode to analyse
     * @param loopMapping Mapping of LoopNodes to their ILPResult exposing the WCEC of the loop
     * @return Returns an instance of type ILPLongestPathDAGSolution that exposes the worst case energy cost per node
     * and a mapping of node to selected parent node
     */
    static ILPLongestPathDAGSolution longestPathDAG(
        HLAC::FunctionNode *func,
        const ILPClusteredLoopResult &loopMapping);

    /**
     * Calculate mappings from nodes to their incoming and outgoing edge indices for the given edges.
     * These mappings are used to efficiently access the relevant edges when building the ILP model.
     * @param edges Edges to analyze
     * @param incoming unordered_map used to store the mapping from nodes to their incoming edge indices.
     * @param outgoing unordered_map used to store the mapping from nodes to their outgoing edge indices.
     */
    static void buildIncidenceMaps(
        const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
        std::unordered_map<HLAC::GenericNode *, std::vector<int>> &incoming,
        std::unordered_map<HLAC::GenericNode *, std::vector<int>> &outgoing);

    /**
     * Iterate over the edges in the function and assign them a unique ID, which we call the ILPIndex
     * @param func Function, for which edges should be assigned an ID
     * @param nextIndex Next free index, the enumeration of the edges should start from
     * @return Returns the last used index
     */
    static int assignEdgeIndicesFunction(HLAC::FunctionNode *func, int nextIndex);

    /**
     * Iterate over the edges in the loopNode and assign them a unique ID, which we call the ILPIndex
     * @param loopNode LoopNode, for which edges should be assigned an ID
     * @param nextIndex Next free index, the enumeration of the edges should start from
     * @return Returns the last used index
     */
    static int assignEdgeIndicesLoop(HLAC::LoopNode *loopNode, int nextIndex);

    /**
     * Insert a column to the given row, if it does not exist already
     * @param row Row to insert in
     * @param used Already used column variables
     * @param col Column where to insert
     * @param coeff Coefficient to insert
     */
    static void insertUnique(CoinPackedVector &row,
        std::unordered_set<int> &used,
        int col,
        double coeff);

    /**
     * Append the given rows to the model
     * @param model Model to insert the row into
     * @param row Row to insert
     * @param lb Lower bound of the row
     * @param ub Upper bound of the row
     */
    static void appendRow(ILPModel &model, const CoinPackedVector &row, double lb, double ub);

    /**
     * Find the biggest ILPIndex in the given LoopNode
     * @param loopNode LoopNode to analyse
     * @return Returns the biggest ILPIndex of the given LoopNode
     */
    static int getMaxEdgeIndex(HLAC::LoopNode *loopNode);

 private:
    /**
     * Return the given double value of a bound to a string with fixed precission.
     * Used for ILP printing
     * @param value Bound value to convert
     * @return String representing a bound with fixed precision
     */
    static std::string boundToString(double value);

    /**
     * Convert the given matrix and row to a string. Used for ILP printing
     * @param matrix Matrix to convert
     * @param row Row to convert
     * @return String representation of the row and matrix
     */
    static std::string formatLinearExpr(const CoinPackedMatrix &matrix, int row);
};

#endif  // SRC_SPEAR_ILP_ILPUTIL_H_
