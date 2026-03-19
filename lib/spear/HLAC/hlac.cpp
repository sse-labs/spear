/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "HLAC/hlac.h"
#include <utility>
#include <fstream>
#include <string>
#include <map>

#include "CbcModel.hpp"
#include "HLAC/util.h"
#include "OsiClpSolverInterface.hpp"

#include "ILP/ILPBuilder.h"

namespace HLAC {

void hlac::makeFunction(llvm::Function* function, llvm::FunctionAnalysisManager *fam) {
    auto fnptr = FunctionNode::makeNode(function, fam, registry, this);
    functions.emplace_back(std::move(fnptr));
}

void hlac::printDotRepresentation() {
    std::filesystem::create_directories("./dot");

    for (auto &fn : functions) {
        std::string filename = "./dot/" + fn->name + ".dot";
        std::ofstream out(filename);

        fn->printDotRepresentation(out);
    }
}

std::map<std::string, double> hlac::getEnergy() {
    // assume we are executing in postorder!!!
    for (auto &functionNode : functions) {
        functionNode->getEnergy();
    }

    return FunctionEnergyCache;
}

double hlac::getEnergyPerFunction(std::string functionName) {
    if (FunctionEnergyCache.contains(functionName)) {
        return FunctionEnergyCache[functionName];
    }

    llvm::errs() << "Trying to get energy of " << functionName << " which has not been analyzed yet!\n";
    return -300000;
}

std::map<std::string, CoinPackedMatrix> hlac::buildILPS() {
    // Assume we iterate in postOrder
    for (auto &functionNode : functions) {
        // ignore phasar hooks
        if (!Util::starts_with(functionNode->function->getName().str(), "__psr") && !Util::starts_with(functionNode->function->getName().str(), "__clang")) {
            auto ilpModel = ILPBuilder::buildMonolithicILP(functionNode.get());

            auto solvedModel = ILPBuilder::solveModel(ilpModel);

            if (solvedModel.has_value()) {
                auto [objectiveValue, variableValues] = solvedModel.value();
                std::cout << "Objective value for function " << functionNode->name << ": " << objectiveValue << "\n";

                std::cout << "Taken path: " << std::endl;

                for (int i = 0; i < variableValues.size(); ++i) {
                    std::cout << "x[" << i << "] = " << variableValues[i] << "\n";
                }

            } else {
                std::cout << "Failed to solve ILP for function " << functionNode->name << "\n";
            }
        }
    }

    return {};
}

}  // namespace HLAC
