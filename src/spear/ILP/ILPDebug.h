
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_ILPDEBUG_H
#define SPEAR_ILPDEBUG_H

/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef ILP_ILPDEBUG_H
#define ILP_ILPDEBUG_H

#include <memory>
#include <string>
#include <vector>

#include <coin/CoinPackedMatrix.hpp>
#include <coin/CoinPackedVector.hpp>

#include "HLAC/hlac.h"

/**
 * ILPDebug class
 * Offers several methods for debugging ILP models.
 *
 */
class ILPDebug {
 public:
    /**
     * Convert a given basic block to a string
     * @param basicBlock Basic block to convert
     * @return String representing the basic block
     */
    static std::string basicBlockToDebugString(const llvm::BasicBlock *basicBlock);

    /**
     * Convert given generic node to a string representation
     * @param genericNode Node to convert
     * @return String representation of the generic node
     */
    static std::string genericNodeToDebugString(const HLAC::GenericNode *genericNode);

    /**
     * Convert a given edge to a debug string
     * @param edge Edge to convert
     * @return Debug string representation of the edge
     */
    static std::string edgeToDebugString(const HLAC::Edge *edge);

    /**
     * Convert a given vector of insts to a comma seperated string
     * @param values Vector to convert
     * @return String representation of the given vector
     */
    static std::string integerVectorToString(const std::vector<int> &values);

    /**
     * Convert a row of an ILP matrix to a string
     * @param row Row to convert
     * @param lowerBound Lower bound value
     * @param upperBound Upper bound value
     * @param edges Corresponding edges
     * @return String representing the row
     */
    static std::string debugRowToString(const CoinPackedVector &row, double lowerBound, double upperBound,
        const std::vector<std::unique_ptr<HLAC::Edge>> &edges);

    /**
     * Convert a given double value to a string. Returns inf/-inf if the numeric limits are reached
     * @param boundValue Value to convert
     * @return String representation of the given double value
     */
    static std::string formatBound(double boundValue);

    /**
     * Convert a given double value to a scientific number string
     * @param coefficientValue Value to convert
     * @return Scientific number string of the given value
     */
    static std::string formatCoefficient(double coefficientValue);

    /**
     * Find an edge in the given list of edges via the given ILPIndex
     * @param edges Edges to search in
     * @param ilpIndex Index to search for
     * @return Edge if found nullptr otherwise
     */
    static const HLAC::Edge *findEdgeByIlpIndex(
        const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
        int ilpIndex);

    /**
     * Print debug information about the given ILPModel
     * @param model Model to dump
     * @param edges Edges to consider while dumping
     * @param name Name to print
     */
    static void dumpILPModel(
        const ILPModel &model,
        const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
        const std::string &name);
};

#endif

#endif //SPEAR_ILPDEBUG_H
