/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_HLAC_UTIL_H_
#define SRC_SPEAR_HLAC_UTIL_H_

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>




#include "HLAC/hlac.h"


namespace HLAC {

struct LoopNodeEdgeSummary {
    std::string functionName;
    std::string loopNodeName;
    std::size_t incomingEdgeCount = 0;
    std::size_t outgoingEdgeCount = 0;
};

/**
 * Util class to implement utility functions for HLACS
 */
class Util {
 public:
    /**
     * Takes a function declaration as input and strips all parameters.
     * Returns the function name with a shorthand for the parameters "(...)"
     * @param declaration Declaration to remove parameters from
     * @return Returns declaration with shorthand instead of parameters
     */
    static std::string stripParameters(const std::string& declaration);

    /**
     * Escape the given input StringRef for usage in dot
     * @param input Input to escape
     * @return Escaped string
     */
    static std::string dotRecordEscape(llvm::StringRef input);

    /**
     * Constructs the string representation of the given llvm instruction
     * @param inst Instruction to convert
     * @return String representation of the instruction
     */
    static std::string instToString(const llvm::Instruction &inst);

    /**
     * Converts a given C++ operator declaration to a prettified name
     * @param input Input function declaration
     * @return Prettified input string
     */
    static std::string prettifyOperators(std::string input);

    /**
     * Escapes a given string to enable usage in dot labels
     * @param input Input string
     * @return Escaped string
     */
    static std::string escapeDotLabel(std::string input);

    /**
     * Replace long function declarations with shorthand
     * @param input Input declaration
     * @return Shorthand representing the declaration
     */
    static std::string shortenStdStreamOps(std::string input);

    /**
     * Remove any return type specifier from the given input string
     * @param input Input string
     * @return Input without return type
     */
    static std::string dropReturnType(std::string input);

    /**
     * Demangle the given mangled input and return it as escaped and prettified dot label
     * @param mangled Mangled function name
     * @return Demangled and prettified name for usage in labels
     */
    static std::string dotSafeDemangledName(const std::string& mangled);

    /**
     * Convert an input feasibility value to a string representation
     * @param feas Feasibility to convert
     * @return String representing the feasibility value
     */
    static std::string feasibilityToString(bool feas);

    /**
     * Helper function to check if a string starts with a certain prefix
     * @param s String to analyze
     * @param prefix Prefix
     * @return True if the string starts with the prefix, false otherwise
     */
    static bool starts_with(const std::string& s, const std::string& prefix);

    /**
     * Return the callgraph of the given module as post order vector
     * @param M Module to analyze
     * @param FAM FunctionAnalysisManager to extract callgraph information from
     * @return Vector containing the callgraph in postorder fashion
     */
    static std::vector<llvm::Function *> getLazyCallGraphPostOrder(llvm::Module &M, llvm::FunctionAnalysisManager &FAM);

    /**
     * Create adjacent representation from given nodes and edges
     * @param nodes Nodes to consider
     * @param edges Edges to consider
     * @return Mapping from nodes to adjacent edges
     */
    static HLACAdjacentRepresentation createAdjacentList(
        const std::vector<GenericNode *> &nodes,
        const std::vector<std::unique_ptr<Edge>> &edges);

    /**
     * Create adjacent representation from given nodes and edges
     * @param nodes Nodes to consider
     * @param edges Edges to consider
     * @return Mapping from nodes to adjacent edges
     */
    static HLACAdjacentRepresentation createAdjacentList(
        const std::vector<std::unique_ptr<GenericNode>> &nodes,
        const std::vector<std::unique_ptr<Edge>> &edges);

    /**
     * Create adjacent representation from given nodes and edges
     * @param nodes Nodes to consider
     * @param edges Edges to consider
     * @return Mapping from nodes to adjacent edges
     */
    static HLACAdjacentRepresentation createIncomingList(
        const std::vector<std::unique_ptr<HLAC::GenericNode>> &nodes,
        const std::vector<std::unique_ptr<HLAC::Edge>> &edges);

    /**
     * Traces the given predecessor list beginning in the entryNode until NULL is reached.
     * Stores all found edges along the way and returns them
     * @param entryNode Node to start from
     * @param predecessors Predecessor list
     * @param edges Existing edges
     * @return List of edges taken on the calculated path
     */
    static std::vector<HLAC::Edge *> findTakenEdges(
        GenericNode *entryNode,
        const std::unordered_map<HLAC::GenericNode *, HLAC::GenericNode *> &predecessors,
        std::vector<std::unique_ptr<HLAC::Edge>> &edges,
        const std::unordered_map<HLAC::LoopNode *, std::vector<std::pair<ILPResult, HLAC::VirtualNode *>>> &loopResults);

    /**
     * Search for the edge under the given ILPIndex
     * @param edgeList List of edges to search in
     * @param globalId Id to search for
     * @return Pointer to the found edge. Nullptr if no edge could be found
     */
    static Edge * findEdgeByGlobalId(std::vector<Edge *> &edgeList, int globalId);

    /**
     * Check the edges in the given LoopNode and append them to the given vector of edges
     * @param loop LoopNode to inspect
     * @param allEdges Vector the edges will be appended to
     */
    static void collectAllContainedEdges(HLAC::LoopNode *loop, std::vector<HLAC::Edge *> &allEdges);

    /**
     * Take the edges from the longest path search und analyse the used loop and append the edges to the given
     * resVector
     * @param loopResults LoopMapping to analyse
     * @param resultpair DAGLongestPath to analyse
     * @param resVector Vector to safe the edges to
     */
    static void appendLoopContainedEdges(
        std::unordered_map<HLAC::LoopNode *, ILPResult> loopResults,
        const DAGLongestPathSolution resultpair,
        std::vector<Edge *> &resVector);

    /**
     * Find maximum edge ILPIndex in LoopNode
     * @param loopNode LoopNode to analyse
     * @return Maximum found index
     */
    static int getMaxEdgeIndexInLoop(HLAC::LoopNode *loopNode);

    /**
     * Find maximum edge ILPIndex in FunctionNode
     * @param FN FunctionNode to analyse
     * @return Maximum found index
     */
    static int getMaxEdgeIndexInFunction(HLAC::FunctionNode *FN);

    /**
     * Analyse which edges have been taken inside the given LoopNode
     * @param loopNode LoopNode to analyse
     * @param takenSet Edges that should have been taken
     * @param result Vector of ids representing the taken edges
     */
    static void markTakenEdgesInLoop(
        HLAC::LoopNode *loopNode,
        const std::unordered_set<HLAC::Edge *> &takenSet,
        std::vector<double> &result);

    /**
 * Collect all LoopNodes and nested LoopNodes that have more than one incoming or outgoing edge.
 * The edge counts are computed relative to the graph level that directly contains the LoopNode.
 *
 * @param functionName Name of the parent function
 * @param nodes Nodes to inspect
 * @param edges Edges belonging to the inspected graph level
 * @param loopNodeEdgeSummaries Output vector receiving matching LoopNode summaries
 */
    static void collectLoopNodeEdgeSummaries(
        const std::string &functionName,
        const std::vector<std::unique_ptr<HLAC::GenericNode>> &nodes,
        const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
        std::vector<LoopNodeEdgeSummary> &loopNodeEdgeSummaries);

    /**
     * Print LoopNode edge summaries as CSV.
     *
     * @param os Output stream to print to
     * @param loopNodeEdgeSummaries Summaries to print
     */
    static void printLoopNodeEdgeSummaries(
        std::ostream &os,
        const std::vector<LoopNodeEdgeSummary> &loopNodeEdgeSummaries);
};

}  // namespace HLAC

#endif  // SRC_SPEAR_HLAC_UTIL_H_
