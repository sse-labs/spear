/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "HLAC/hlac.h"
#include <utility>
#include <fstream>

void HLAC::hlac::makeFunction(llvm::Function* function, llvm::FunctionAnalysisManager *fam) {
    auto fnptr = FunctionNode::makeNode(function, fam);
    functions.emplace_back(std::move(fnptr));
}

void HLAC::hlac::printDotRepresentation() {
    for (auto &fn : functions) {
        if (fn->name == "main") {
            std::string filename = fn->name.str() + ".dot";
            std::ofstream out(filename);

            fn->printDotRepresentation(out);
        }
    }
}