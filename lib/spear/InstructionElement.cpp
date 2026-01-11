/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "ProgramGraph.h"

InstructionElement::InstructionElement(llvm::Instruction* instruction) {
    this->energy = 0;
    this->inst = instruction;
}
