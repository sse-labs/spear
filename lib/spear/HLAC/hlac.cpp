/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "HLAC/hlac.h"
#include <utility>
#include <fstream>
#include <string>

namespace HLAC {

void hlac::makeFunction(llvm::Function* function, llvm::FunctionAnalysisManager *fam) {
    auto fnptr = FunctionNode::makeNode(function, fam);
    functions.emplace_back(std::move(fnptr));
}

void hlac::printDotRepresentation() {
    for (auto &fn : functions) {
        std::string filename = fn->name + ".dot";
        std::ofstream out(filename);

        fn->printDotRepresentation(out);
    }
}

}  // namespace HLAC
