//
// Created by max on 1/16/26.
//

#include "HLAC/hlac.h"

#include <llvm/IR/Function.h>

void HLAC::hlac::makeFunction(llvm::Function* function, llvm::FunctionAnalysisManager *fam) {
    auto fnptr = FunctionNode::makeNode(function, fam);
    functions.emplace_back(std::move(fnptr));
}
