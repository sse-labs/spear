
#include "LLVMHandler.h"
#include "InstructionCategory.h"
#include <cmath>
#include "ProgramGraph.h"


#include <utility>

LLVMHandler::LLVMHandler( json energy, int valueIfIndeterminable, bool useCallAnalysis, EnergyFunction *funcPool, int funcPoolSize) {
    this->energyValues = energy;
    this->valueIfIndeterminable = valueIfIndeterminable;
    this->inefficient = 0;
    this->efficient = 0;
    this->useCallAnalysis = useCallAnalysis;

    for(int i=0; i < funcPoolSize; i++){
        this->funcmap.push_back(&funcPool[i]);
    }
}

double LLVMHandler::getNodeSum(Node *node){
    //Init the sum of this block
    double blocksum = 0.0;
    std::vector<InstructionElement> workingInstList = node->instructions;
    auto nodename = node->block->getName();

    //Iterate over the instructions in this block
    for ( int i=0; i < node->instructions.size(); i++ ) {
        auto &instruction = node->instructions[i].inst;
        auto name = instruction->getOpcodeName();

        //Categorize the current instruction
        InstructionCategory::Category category = InstructionCategory::getCategory(*instruction);

        //Get the energy from the JSON energy values by referencing the category
        double instructionValue = 0.00;
        if(category == InstructionCategory::Category::CALL && useCallAnalysis){
            double testval = InstructionCategory::getCalledFunctionEnergy(*instruction, this->funcmap);
            double calledValue = InstructionCategory::getCalledFunctionEnergy(*instruction, this->funcmap);
            instructionValue = this->energyValues[InstructionCategory::toString(category)].get<double>();

            instructionValue += calledValue;
        }else{
            instructionValue = this->energyValues[InstructionCategory::toString(category)].get<double>();
        }

        // We catch the energy value of phi nodes here as they are a feature of llvm
        // Their energy usage can not be translated directly to the energy usage of the source code
        // Therefore we make the energy usage zero
        if(llvm::isa<llvm::PHINode>(instruction)){
            instructionValue = 0;
        }

        node->instructions[i].energy = instructionValue;

        //Add the value to the sum
        blocksum += instructionValue;
    }

    return blocksum;
}
