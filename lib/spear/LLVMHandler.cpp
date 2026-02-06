/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "LLVMHandler.h"
#include "InstructionCategory.h"
#include "ProgramGraph.h"

#include <vector>
#include <string>

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
    // Init the sum of this block
    double blocksum = 0.0;
    std::vector<InstructionElement> workingInstList = node->instructions;
    auto nodename = node->block->getName();

    // Iterate over the instructions in this block
    for ( int i=0; i < node->instructions.size(); i++ ) {
        auto &instruction = node->instructions[i].inst;
        auto name = instruction->getOpcodeName();

        double energy = 0.0;

        if (auto *ICmp = llvm::dyn_cast<llvm::ICmpInst>(instruction)) {
            llvm::ICmpInst::Predicate Pred = ICmp->getPredicate();

            llvm::StringRef PredStr = llvm::ICmpInst::getPredicateName(Pred);

            std::string icmpname = "icmp " + PredStr.str();
            if (this->energyValues.contains(icmpname)) {
                energy = this->energyValues[icmpname];
            }
        } else {
            if (this->energyValues.contains(name)) {
                energy = this->energyValues[name];
            }
        }

        // Get the energy from the JSON energy values by referencing the category
        double instructionValue = 0.00;
        if (llvm::isa<llvm::CallBase>(instruction) && useCallAnalysis) {
            double calledValue = InstructionCategory::getCalledFunctionEnergy(*instruction, this->funcmap);
            instructionValue = energy;

            instructionValue += calledValue;
        } else {
            instructionValue = energy;
        }

        // We catch the energy value of phi nodes here as they are a feature of llvm
        // Their energy usage can not be translated directly to the energy usage of the source code
        // Therefore we make the energy usage zero
        if (llvm::isa<llvm::PHINode>(instruction)) {
            instructionValue = 0.0;
        }

        node->instructions[i].energy = instructionValue;

        // Add the value to the sum
        blocksum += instructionValue;
    }

    return blocksum;
}
