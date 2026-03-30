
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_HLAC_HLACHASHING_H_
#define SRC_SPEAR_HLAC_HLACHASHING_H_

#include <string>

#include "HLAC/hlac.h"

namespace HLAC {

class Hasher {
 public:
    /**
     * Calculate the hash for the given Node. The hash is calculated based on the content of the node and the hashes
     * of its children.
     * @param node Node to calculate the hash for
     * @return Returns the calculated hash for the given node
     */
    static std::string getHashForNode(GenericNode * node);

 private:
    // Node specific hash getter
    static std::string getHashForVirtualNode(VirtualNode * node);

    static std::string getHashForCallNode(CallNode * node);

    static std::string getHashForNormalNode(Node * node);

    static std::string getHashForLoopNode(LoopNode * node);

    static std::string getHashForFunctionNode(FunctionNode * node);

    // Hash calculation

    /**
     * Convert the given basic block to a hex string value
     * @param basicBlock Basic block to convert
     * @return String hex representation of the given basic block
     */
    static std::string getBasicBlockToHexString(const llvm::BasicBlock *basicBlock);

    /**
     * Calculate a hash for the given basic block
     * @param basicBlock Block to build the hash for
     * @return String hash of the given basic block
     */
    static std::string getBasicBlockHash(const llvm::BasicBlock *basicBlock);


    // Helper functions
    /**
     * Convert the given llvm type to a corresponding string representation
     * @param type Type to convert
     * @return String representation of the given type
     */
    static std::string typeToString(const llvm::Type *type);

    /**
     * Converts a given apint value to a string representation
     * @param value Value to convert
     * @return String representation of the apint value
     */
    static std::string apIntToString(const llvm::APInt &value);

    /**
     * Convert the given llvm value to a string representation
     * @param value
     * @return
     */
    static std::string valueToString(const llvm::Value *value);

    /**
     * Get the index of the given basic block in the parent functions block list
     * @param basicBlock Basic block to find
     * @return Index of the given basic block in the parents block list
     */
    static std::size_t getBasicBlockIndexInFunction(const llvm::BasicBlock *basicBlock);

    /**
     * Add additional information about the given node to the hash to mitigate hash collisions
     * @param node Node to analyse
     * @return String representation of the given node that is hopefully unique
     */
    static std::string fallBackHashAdditions(const HLAC::GenericNode *node);

    /**
     * Query the node type of the given node as string
     * @param node Node to analyze
     * @return String representation of the nodetype
     */
    static std::string nodeTypeToString(const HLAC::GenericNode *node);

    /**
     *  Hex string representation of the given value
     * @param value Size_t value to convert to hex
     * @return String value as hex representation of the given value
     */
    static std::string toHexString(std::size_t value);

    /**
     *  Hex string representation of the given value
     * @param value string value to convert to hex
     * @return String value as hex representation of the given value
     */
    static std::string toHexString(std::string value);
};
}  // namespace HLAC

#endif  // SRC_SPEAR_HLAC_HLACHASHING_H_
