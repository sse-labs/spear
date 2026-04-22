/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_ILP_ILPBUILDER_H_
#define SRC_SPEAR_ILP_ILPBUILDER_H_

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ILPTypes.h"


/**
 * ILPBuilder class that provides static methods to build ILP models from HLAC function and loop nodes.
 */
class ILPBuilder {
 public:
    /**
     * Empty dummy constructor
     */
    ILPBuilder() {}

     /**
      * Construct a monolithic ILP from the given functionNode pointer
      * @param func FunctionNode pointer to calculate the monolithic ILP for
      * @return Returns the constructed ILPModel for the given function
      */
    static ILPModel buildMonolithicILP(HLAC::FunctionNode *func);

    /**
     * Construct a monolithic ILP for the given LoopNode. This is mainly used to construct ILPs for loops during
     * clustered ILP construction
     * @param loop LoopNode to construct the ILP for
     * @return Returns the constructed ILPModel for the given loopNode
     */
    static ILPModel buildMonolithicILP(HLAC::LoopNode *loop);

    /**
     * Construct a clustered ILP from the given functionNode pointer
     * @param func FunctionNode pointer to calculate the clustered ILP for
     * @return Returns the constructed ClusteredILPModel
     */
    static ClusteredILPModel buildClusteredILP(HLAC::FunctionNode *func);

    /**
     * Solve a given ILPModel using the ILPSolver class
     * @param model Model to solve
     * @return Optional over ILPResult. Contains the ILPResult if the model could be solved successfully, std::nullopt
     * otherwise
     */
    static std::optional<ILPResult> solveModel(const ILPModel& ilpModel);

    static void debugDumpILPModel(const ILPModel &model, const std::vector<std::unique_ptr<HLAC::Edge>> &edges, const std::string &name);

 private:

    /**
     * Insert feasibility information contained in the given functionNode to the given model.
     * Encodes the feasibility via bounds on the respective edge variables. If an edge is infeasible we set the upper
     * bound of the respective variable to 0 i.e the edge can never be taken
     * @param model Mode to apply the feasibilty information to
     * @param func FunctionNode, where we pull the feasibility information from
     */
    static void applyEdgeFeasibilityBounds(ILPModel &model, HLAC::FunctionNode *func);

    /**
     * Insert feasibility information contained in the given functionNode to the given model.
     * Encodes the feasibility via bounds on the respective edge variables. If an edge is infeasible we set the upper
     * bound of the respective variable to 0 i.e the edge can never be taken
     * @param model Mode to apply the feasibilty information to
     * @param loopNode LoopNode, where we pull the feasibility information from
     */
    static void applyEdgeFeasibilityBounds(ILPModel &model, HLAC::LoopNode *loopNode);

    /**
     * Add information about the edge behaviour of the hlac represented by the given nodes and edges.
     * We encode edges as edge constrains in the graph. For each node we have to guarantee, that the amount of
     * executions from incoming edges is equal to the amount of executions from the outgoing edges.
     * For example:
     *  x_1 + x_2 = x_3 + x_5
     *
     * @param model ILPModel to inser the constrains to
     * @param nodes Nodes from the HLAC graph under analysis
     * @param edges Edges from the HLAC graph under analysis
     * @param invocationCols Optional pointer to ILP column indices representing how often
     * this subgraph is entered from the parent graph. If nullptr, the subgraph is treated as top-level.
     */
    static void appendGraphConstraints(
        ILPModel &model,
        const std::vector<std::unique_ptr<HLAC::GenericNode>> &nodes,
        const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
        const std::vector<int> *invocationCols);


    /**
     * Add a loop bound constraint for the given loop node to the ILP model.
     *
     * The loop bound restricts how often the loop-internal backedge flow may be taken
     * relative to the number of times the loop is entered from the surrounding graph.
     * The invocation count is derived from the columns in invocationCols, which represent
     * the edges entering the loop node from its parent scope.
     *
     * Conceptually, if the loop may execute at most B times per invocation, we add a
     * linear constraint of the form:
     *
     *      loop_iterations <= B * loop_invocations
     *
     * loop_iterations is the execution count of the loop backedge(s) or the total
     * amount of repeated loop body execution, and loop_invocations is the execution count of the incoming edges
     * listed in invocationCols.
     *
     * This ties the number of iterations to the number of times the loop is reached in
     * the outer graph and prevents the ILP from assigning arbitrarily large execution
     * counts to the loop body.
     *
     * @param model ILPModel to insert the constraint into
     * @param loopNode Loop node whose bound should be encoded
     * @param invocationCols ILP column indices representing how often the loop is entered
     * from the surrounding graph
     */
    static void appendLoopBoundConstraint(ILPModel &model,
        HLAC::LoopNode *loopNode,
        const std::vector<int> &invocationCols);

    /**
     * Append the synthetic constraint that guarantees that all toplevel loops are assumed to be entered exactly once.
     * This is required for our clustered ILP construction, where we assume that all loops except the top-level loop
     * are executed according to incoming edges, but the top-level loop is entered exactly once.
     * @param model ILPModel to insert the constrains into
     * @param col Column relevant for the synthetic constrain
     */
    static void appendEqualityConstraint(ILPModel &model, int col);

    /**
     * Fill in the objective function of the model. This adds the energy cost per variable to the vector that
     * represents the coefficients of the goal function of the ILP.
     *
     * @param model Model to fill the objective function for
     * @param func Function to extract the energy values from
     */
    static void fillObjectiveFunction(ILPModel &model, HLAC::FunctionNode *func);

    /**
     * Fill in the objective function of the model. This adds the energy cost per variable to the vector that
     * represents the coefficients of the goal function of the ILP.
     *
     * @param model Model to fill the objective function for
     * @param loopNode LoopNode to extract the energy values from
     */
    static void fillObjectiveFunction(ILPModel &model, HLAC::LoopNode *loopNode);
};

#endif  // SRC_SPEAR_ILP_ILPBUILDER_H_
