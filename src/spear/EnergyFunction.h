#ifndef SPEAR_ENERGYFUNCTION_H
#define SPEAR_ENERGYFUNCTION_H


#include <llvm/IR/Function.h>
class ProgramGraph;

/**
 * Class abstracting functions with attached energy value 
 * 
 */
class EnergyFunction {
public:
    //Reference to the corresponding function
    llvm::Function * func;
    //The energy used by the function
    double energy;

    std::string name;

    ProgramGraph* programGraph;

    //Simple constructor
    explicit EnergyFunction();
};


#endif //SPEAR_ENERGYFUNCTION_H
