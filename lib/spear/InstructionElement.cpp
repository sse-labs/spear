
#include "ProgramGraph.h"

InstructionElement::InstructionElement(llvm::Instruction* instruction){
    this->energy = 0;
    this->inst = instruction;
}