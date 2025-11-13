#ifndef BA_LOOPTREE_H
#define BA_LOOPTREE_H

#include <vector>
#include <cmath>
#include <llvm/Analysis/LoopInfo.h>
#include "LLVMHandler.h"


class LLVMHandler;

/**
 * LoopTree - A recursive datastructure to handle encapsulated loops
 */
class LoopTree {
public:
    llvm::LoopInfo *LI;

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

    /**
     * Constructor building the recursive datastructure
     * @param main The loop from which the tree should be builded
     * @param subloops The loops contained in the mainloop
     * @param handler A LLVMHandler to handle calculations on the LLVM IR
     */
    LoopTree(llvm::Loop *main, const std::vector<llvm::Loop *>& subloops, LLVMHandler *handler, llvm::ScalarEvolution *scalarEvolution, llvm::LoopInfo *li);

    /**
     * Prints this node in preorder
     */
    void printPreOrder();

    long getLoopUpperBound(llvm::Loop *loop, llvm::ScalarEvolution *scalarEvolution) const;

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

};


#endif //BA_LOOPTREE_H