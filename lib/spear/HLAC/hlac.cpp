/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "HLAC/hlac.h"
#include <utility>
#include <fstream>
#include <string>
#include <map>

#include "OsiClpSolverInterface.hpp"
#include "CbcModel.hpp"

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
        if (functionNode->name == "main") {
            auto ilpModel = ILPBuilder::buildMonolithicILP(functionNode.get());

            OsiClpSolverInterface solver;

            solver.loadProblem(ilpModel.matrix,
                ilpModel.col_lb.data(),
                ilpModel.col_ub.data(),
                ilpModel.obj.data(),
                ilpModel.row_lb.data(),
                ilpModel.row_ub.data());

            // Set to maximize
            solver.setObjSense(-1.0);

            // All integer
            for (int e = 0; e < functionNode->Edges.size(); ++e) {
                solver.setInteger(e);
            }

            CbcModel model(solver);

            model.branchAndBound();

            if (!model.isProvenOptimal()) {
                std::cerr << "No optimal solution found\n";
                if (model.isProvenInfeasible()) {
                    std::cerr << "Model is infeasible\n";
                }
                if (model.isContinuousUnbounded()) {
                    std::cerr << "Model is unbounded\n";
                }
            }else {
                std::cout << "Optimal objective: " << model.getObjValue() << "\n";

                const double *solution = model.bestSolution();
                for (int i = 0; i < static_cast<int>(ilpModel.obj.size()); ++i) {
                    std::cout << "x[" << i << "] = " << solution[i] << "\n";
                }
            }
        }
    }

    return {};
}

}  // namespace HLAC
