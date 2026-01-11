/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_FUNCTIONTREE_H_
#define SRC_SPEAR_FUNCTIONTREE_H_


#include <llvm/IR/Function.h>

#include <string>
#include <vector>

/**
 * Class representing the call-structure of a function as tree
 */
class FunctionTree {
 public:
    /**
     * All called functions represented as FunctionsTrees
     */
    std::vector<FunctionTree *> subtrees;

    /**
     * The function the call-tree extends from
     */
    llvm::Function *func;

    /**
     * Name of the function
     */
    std::string name;

    /**
     * Constructor creating a FunctionTree with the given llvm::Function
     * @param function Reference to a llvm::Function
     */
    explicit FunctionTree(llvm::Function * function);

    /**
     * Static method to construct a FunctionTree from a given llvm::Function
     * @param function Reference to a llvm::Function
     * @return Returns the built FunctionTree with all subgraphs
     */
    static FunctionTree* construct(llvm::Function * function);

    /**
     * Prints the FunctionTree in a preorder fashion
     */
    void printPreorder();

    /**
     * Get the functions of the tree in preorder fashion as vector
     * @return Returns a vector containing references to the functions of the tree in pre-order
     */
    std::vector<llvm::Function *> getPreOrderVector();

 private:
    /**
     * Calculate the functions called by the root-node of the tree
     * @return Returns a vector containing references to the called functions
     */
    [[nodiscard]] std::vector<llvm::Function *> getCalledFunctions() const;
};


#endif  // SRC_SPEAR_FUNCTIONTREE_H_
