//
// Created by max on 1/16/26.
//

#include "HLAC/hlac.h"

#include <llvm/IR/Function.h>

void HLAC::hlac::makeFunction(llvm::Function* function) {
    auto fnptr = FunctionNode::makeNode(function);
    functions.emplace_back(std::move(fnptr));
}
