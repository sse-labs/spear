/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "LLVMHandler.h"
#include "InstructionCategory.h"
#include "ProgramGraph.h"

#include <vector>
#include <string>

#include "ProfileHandler.h"

LLVMHandler::LLVMHandler(
    json energy,
    bool useCallAnalysis,
    EnergyFunction *funcPool,
    int funcPoolSize) {

    this->energyValues = energy;
    this->inefficient = 0;
    this->efficient = 0;
    this->useCallAnalysis = useCallAnalysis;

    for (int i=0; i < funcPoolSize; i++) {
        this->funcmap.push_back(&funcPool[i]);
    }
}

double LLVMHandler::getNodeSum(Node *node) {
    double energy = 0.0;
    auto &pHandler = ProfileHandler::get_instance();

    for (const llvm::Instruction &I : *node->block) {
        std::string instname = I.getOpcodeName();

        if (auto icmpinst = llvm::dyn_cast<llvm::ICmpInst>(&I)) {
            instname = std::string("icmp ") + llvm::ICmpInst::getPredicateName(icmpinst->getPredicate()).str();
        }

        auto candiate = pHandler.getEnergyForInstruction(instname);
        if (candiate.has_value()) {
            energy += candiate.value();
        } else {
            // If we do not have an energy value for the instruction, we log this and continue with the next instruction
            // llvm::errs() << "No energy value found for instruction: " << I.getOpcodeName() << "\n";
            auto unknownCost = pHandler.getUnknownCost();
            if (unknownCost.has_value()) {
                energy += unknownCost.value();
            } else {
                llvm::errs() << "No unknown value specified by the profile! Recreate the profile!" << "\n";
            }
        }
    }

    return energy;
}
