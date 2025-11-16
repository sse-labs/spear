//
// Created by max on 11/14/25.
//

#ifndef SPEAR_PHASARHANDLER_H
#define SPEAR_PHASARHANDLER_H
#include <string>
#include <phasar.h>


class PhasarHandler {
public:
    /**
     * Module of the parsed input program
     */
    llvm::Module *module;

    /**
     * Main constructor
     * @param mod
     */
    PhasarHandler(llvm::Module *mod);

    /**
     * Executes the predefined Phasar Analysis
     */
    void runAnalysis();

    /**
     * Queries a given function and tries to any contained bound variables
     * @param func Function to be analysed
    **/
    std::map<
        std::string,
        std::pair<const llvm::Value *, psr::IDELinearConstantAnalysisDomain::l_t>
    > queryBoundVars(llvm::Function * func);

    /**
     * Debug function to dump the analysis results
     */
    void dumpState();
private:
    /**
     * Results extracted from phasar
     */
    std::unique_ptr<
        psr::OwningSolverResults<
            const llvm::Instruction *,
            const llvm::Value *,
            psr::LatticeDomain<long>
        >
    > _analysisResult;

    /**
     * Phasar HelperAnalyses object
     */
    std::unique_ptr<psr::HelperAnalyses> _HA;

    /**
     * Function entrypoints
    **/
    std::vector<std::string> _entrypoints;
};

#endif //SPEAR_PHASARHANDLER_H