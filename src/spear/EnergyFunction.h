/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_ENERGYFUNCTION_H_
#define SRC_SPEAR_ENERGYFUNCTION_H_

#include <llvm/IR/Function.h>
#include <string>

// Forward declaration
class ProgramGraph;

/**
 * Class abstracting functions with attached energy value 
 * 
 */
class EnergyFunction {
 public:
    // Reference to the corresponding function
    llvm::Function * func;
    // The energy used by the function
    double energy;

    std::string name;

    ProgramGraph* programGraph;

    // Simple constructor
    explicit EnergyFunction();
};


#endif  // SRC_SPEAR_ENERGYFUNCTION_H_
