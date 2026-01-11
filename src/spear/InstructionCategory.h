/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_INSTRUCTIONCATEGORY_H_
#define SRC_SPEAR_INSTRUCTIONCATEGORY_H_


#include <llvm/IR/Instruction.h>
#include "EnergyFunction.h"

#include <vector>
#include <string>

/**
 * Class handling an enum to categorize instructions
 */
class InstructionCategory {
 public:
    /**
     * Enum containing the possible categories
     */
    enum Category { MEMORY, PROGRAMFLOW, DIVISION, CALL, OTHER };

    /**
     * Calculates the category enum type of the provided Instruction
     * @param Instruction LLVM Instruction to categorize
     * @return Returns the Enum Category the instruction belongs to
     */
    static Category getCategory(llvm::Instruction &Instruction);

    /**
     * Returns the string the representation of the enum category enum
     * @param category The enum category which should be converted to a string
     * @return Returns a string representing the category
     */
    static std::string toString(Category category);

    /**
     * Checks if the given instruction is a call instruction
     * @param Instruction
     * @return
     */
    static bool isCallInstruction(llvm::Instruction &Instruction);

    /**
     * Checks if the given instruction interacts with the memory
     * @param Instruction
     * @return
     */
    static bool isMemoryInstruction(llvm::Instruction &Instruction);

    /**
     * Checks if the given instruction is a programflow-instruction
     * @param Instruction
     * @return
     */
    static bool isProgramFlowInstruction(llvm::Instruction &Instruction);

    /**
     * Checks if the given instruction calculates a division
     * @param Instruction
     * @return
     */
    static bool isDivisionInstruction(llvm::Instruction &Instruction);

    /**
     * Calculated the energy of the function called by the given instruction
     * @param Instruction
     * @param pool
     * @return
     */
    static double getCalledFunctionEnergy(llvm::Instruction &Instruction, const std::vector<EnergyFunction *>& pool);
};

#endif  // SRC_SPEAR_INSTRUCTIONCATEGORY_H_
