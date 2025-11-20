//
// Created by max on 11/14/25.
//

#ifndef SPEAR_PHASARHANDLER_H
#define SPEAR_PHASARHANDLER_H
#include <string>
#include <memory>
#include <map>
#include <vector>
#include <phasar.h>

class PhasarHandler {
public:
    /**
     * Module of the parsed input program
     */
    llvm::Module *module;

    /**
     * Get the singleton instance
     * @param mod Optional, only used on first call
     */
    static PhasarHandler& getInstance(llvm::Module *mod = nullptr) {
        static PhasarHandler instance(mod);
        return instance;
    }

    /**
     * Deleted copy constructor and assignment operator
     * to prevent copying
     */
    PhasarHandler(const PhasarHandler&) = delete;
    PhasarHandler& operator=(const PhasarHandler&) = delete;

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
        std::map<std::string, std::pair<const llvm::Value*, psr::IDELinearConstantAnalysisDomain::l_t>>
    > queryBoundVars(llvm::Function * func);

    /**
     * Debug function to dump the analysis results
     */
    void dumpState();

private:
    /**
     * Private constructor
     * @param mod
     */
    explicit PhasarHandler(llvm::Module *mod);

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
