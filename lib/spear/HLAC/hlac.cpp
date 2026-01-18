/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "HLAC/hlac.h"
#include <utility>

void HLAC::hlac::makeFunction(llvm::Function* function, llvm::FunctionAnalysisManager *fam) {
    auto fnptr = FunctionNode::makeNode(function, fam);
    functions.emplace_back(std::move(fnptr));
}
