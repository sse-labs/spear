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

void hlac::printDotRepresentationWithSolution(std::vector<double> result) {
    std::filesystem::create_directories("./dot");

    for (auto &fn : functions) {
        std::string filename = "./dot/" + fn->name + "_solution.dot";
        std::ofstream out(filename);

        fn->printDotRepresentationWithSolution(out, result);
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

std::map<std::string, ILPModel> hlac::buildMonolithicILPS() {
    std::map<std::string, ILPModel> resultMapping;

    // Assume we iterate in postOrder
    for (auto &functionNode : functions) {
        // ignore phasar hooks
        if (!Util::starts_with(functionNode->function->getName().str(), "__psr") && !Util::starts_with(functionNode->function->getName().str(), "__clang")) {
            auto ilpModel = ILPBuilder::buildMonolithicILP(functionNode.get());

            resultMapping[functionNode->name] = ilpModel;

        } else {
            // std::cout << "Ignored ILP building for function " << functionNode->name << "\n";
        }
    }

    return resultMapping;
}

std::map<std::string, std::map<LoopNode *, ILPModel>> hlac::buildClusteredILPS() {
    std::map<std::string, std::map<LoopNode *, ILPModel>> resultMapping;

    // Assume we iterate in postOrder
    for (auto &functionNode : functions) {
        // ignore phasar hooks
        if (!Util::starts_with(functionNode->function->getName().str(), "__psr") && !Util::starts_with(functionNode->function->getName().str(), "__clang")) {
            auto ilpModelMapping = ILPBuilder::buildClusteredILP(functionNode.get());

            resultMapping[functionNode->name] = ilpModelMapping;

        } else {
            // std::cout << "Ignored ILP building for function " << functionNode->name << "\n";
        }
    }

    return resultMapping;
}


std::map<std::string, std::pair<double, std::vector<double>>> hlac::solveMonolithicIlps(std::map<std::string, ILPModel> modelMapping) {
    std::map<std::string, std::pair<double, std::vector<double>>> result;

    for (auto &[name, model] : modelMapping) {
        auto solvedModel = ILPBuilder::solveModel(model);

        if (solvedModel.has_value()) {
            auto solvedPair = solvedModel.value();
            // std::cout << "Objective value for function " << name << ": " << objectiveValue << "\n";

            result[name] = solvedPair;

            /*std::cout << "Taken path: " << std::endl;

            for (int i = 0; i < variableValues.size(); ++i) {
                std::cout << "x[" << i << "] = " << variableValues[i] << "\n";
            }*/

            // this->printDotRepresentationWithSolution(variableValues);
        }
    }


    return result;
}

std::map<std::string, std::pair<double, std::vector<double>>>
hlac::solveClusteredIlps(std::map<std::string, std::map<HLAC::LoopNode *, ILPModel>> modelMapping) {
    std::map<std::string, std::pair<double, std::vector<double>>> result;

    for (auto &[name, loopModelMapping] : modelMapping) {
        // We need to solve the ILP for each loop and then combine the results to get the overall energy and path for the function
        std::vector<double> combinedVariableValues;

        std::cout << "Clustered results for " << name << ":\n";

        for (auto &[loopNode, model] : loopModelMapping) {
            auto solvedModel = ILPBuilder::solveModel(model);

            if (solvedModel.has_value()) {
                auto solvedPair = solvedModel.value();
                double objectiveValue = solvedPair.first;
                std::vector<double> variableValues = solvedPair.second;

                std::cout << "Loop " << loopNode->loop->getName().str() << " -> " << objectiveValue << std::endl;
            }
        }

        result[name] = {0.0, combinedVariableValues};
    }

    return result;
}

}  // namespace HLAC
