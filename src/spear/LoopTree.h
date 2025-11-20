#ifndef BA_LOOPTREE_H
#define BA_LOOPTREE_H

#include <vector>
#include <cmath>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <phasar/PhasarLLVM/DataFlow/IfdsIde/Problems/IDELinearConstantAnalysis.h>

#include "LLVMHandler.h"


class LLVMHandler;

/**
 * LoopTree - A recursive datastructure to handle encapsulated loops
 */
class LoopTree {
public:
    /**
     * Member to store the loop from which the treenode extends to the subgraphs containing the subloops
     */
    llvm::Loop * mainloop;

    /**
     * Vector storing all subgraphs extending from this treenode
     */
    std::vector<LoopTree *> subTrees;

    /**
     * The basic blocks of the loop excluding the blocks of the subloops
     */
    std::vector<llvm::BasicBlock *> blocks;

    /**
     * A LLVMHandler object, so we can reason about the energy consumption of the loops
     */
    LLVMHandler* handler;

    /**
     * The over approximated iterations of the loop contained in this node
     */
    long iterations;

    std::vector<const llvm::Value *> boundvars;

    //const llvm::Value * boundvar;


    /**
     * Constructor building the recursive datastructure
     * @param main The loop from which the tree should be builded
     * @param subloops The loops contained in the mainloop
     * @param handler A LLVMHandler to handle calculations on the LLVM IR
     */
    LoopTree(
        llvm::Loop *main,
        const std::vector<llvm::Loop *>& subloops,
        LLVMHandler *handler,
        llvm::ScalarEvolution *scalarEvolution,
        std::map<
        std::string,
        std::map<std::string, std::pair<const llvm::Value*, psr::IDELinearConstantAnalysisDomain::l_t>>
        > *variablemapping
    );

    /**
     * Prints this node in preorder
     */
    void printPreOrder();

    long getLoopUpperBound(llvm::Loop *loop, llvm::ScalarEvolution *scalarEvolution);

    /**
     * Gets the Latches of the LoopTree
     * @return Vector of BasicBlock references
     */
    std::vector<llvm::BasicBlock *> getLatches();


    /**
     * LoopTree destructor
     */
    ~LoopTree();

private:

    /**
     * Method for calculating the difference of all blocks present in the loop and the subloops
     * @return Returns the calculated blocks as vector of pointers
     */
    std::vector<llvm::BasicBlock *> calcBlocks();

    /**
     * Simple method to check if the current LoopTree is a leaf and has now subloops
     * @return Returns true if the current LoopTree is a leaf, false otherwise
     */
    bool isLeaf() const;


    /**
     * Takes a scalar evolution expression and tries to deduce the source name of the referenced variable
     * @param Expr Expression found by scalar evolution
     * @param SE Scalar evolution analysis
     * @param IndVar Induction variable of the loop
     * @return Returns a vector of llvm value pointers that refer to the source variable of the loop bound
     */
    std::vector<const llvm::Value *> getSourceVariablesFromSCEV(const llvm::SCEV *Expr, llvm::ScalarEvolution &SE, llvm::PHINode *IndVar) const;

    /**
     * Parses the main loop of the looptree and uses the scalar evolution analysis to deduce the source name of the
     * loop bound variable
     * @param scalarEvolution
     */
    void findBoundVars(llvm::ScalarEvolution *scalarEvolution);

    /**
     * Phasar variable mapping
     * Maps variable source name to constant value
     */
    std::map<
        std::string,
        std::map<std::string, std::pair<const llvm::Value*, psr::IDELinearConstantAnalysisDomain::l_t>>
    > *_variablemapping;

    /**
     * Calculates the actual amount of iterations a loop runs with the given loop parameters
     * @param start Start value of the loop
     * @param end End value of the loop
     * @param step Steps performed with each iteration
     * @param direction Loop direction either increasing or decreasing
     * @return Returns the amount of repetitions the loop runs
     */
    long calculateIterations(long start, long end, long step, llvm::Loop::LoopBounds::Direction direction);

    /**
     * Calculates the loop iterations from a given loop bound
     * @param loopBound loop bound used to calculate loop parameters
     * @param endValue Predefined ending value. If no ending value is given, we will deduce the bound of the loop using
     * the given loopbound
     * @return Returns a loop bound
     */
    long iterationsFromLoopBound(llvm::Optional<llvm::Loop::LoopBounds> *loopBound, long endValue = -1 );
};


#endif //BA_LOOPTREE_H