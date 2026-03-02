/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_ANALYSES_LOOPBOUND_LOOPBOUNDWRAPPER_H_
#define SRC_SPEAR_ANALYSES_LOOPBOUND_LOOPBOUNDWRAPPER_H_

#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Dominators.h>
#include <phasar/DataFlow/IfdsIde/SolverResults.h>
#include <phasar/PhasarLLVM/HelperAnalyses.h>

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

#include "LoopBound.h"

namespace LoopBound {

/**
 * LoopCache struct
 * Caches the dominator tree and loop info for a function to avoid recalculating it multiple times during our analysis
 */
struct LoopCache {
    // Dominator tree of the function
    llvm::DominatorTree DT;
    // Loop info of the function
    llvm::LoopInfo LI;

    explicit LoopCache(llvm::Function &F) : DT(F), LI(DT) {}
};

/**
 * Phasar result type
 * Used to shorthand any interaction with the analysis result
 */
using ResultsTy = psr::OwningSolverResults<const llvm::Instruction *,
const llvm::Value *, LoopBound::DeltaInterval>;


/**
 * LoopBoundWrapper class
 *
 * Given program information this wrapper calculates loop step and bound values for each loop reachable
 * from the main function of the program under analysis
 */
class LoopBoundWrapper {
 public:
    /**
     * Constructor to run the loopbound analysis
     * @param helperAnalyses Phasar help analyses to access phasars analysis information
     * @param FAM FunctionAnalysisManager to access llvm analysis information
     */
    LoopBoundWrapper(std::shared_ptr<psr::HelperAnalyses> helperAnalyses, llvm::FunctionAnalysisManager *FAM);

    /**
     * Store the given loop and its subloops in the given vector
     * @param L Loop to save and analyze
     * @param Out Vector to store L and its subloop in
     */
    void collectLoops(llvm::Loop *L, std::vector<llvm::Loop *> &Out);

    /**
     * Return the internal list of LoopClassifier objects
     * @return
     */
    std::vector<LoopClassifier> getClassifiers();

    /**g
     * Calculate Checkexpression recursively from the given value.
     * @param V Value to analyse
     * @return The constructed Checkexpression if possible
     */
    static std::optional<CheckExpr> peelBasePlusConst(const llvm::Value *V);

    /**
     * Searches the loop defined by the given LoopDescription for a constant check value that the loop counter
     * is checked against
     * @param description LoopDescription that defines the loop under analsis
     * @return Returns a check value if it can be found
     */
    static std::optional<CheckExpr> findLoopCheckExpr(
        const LoopBound::LoopParameterDescription &description, llvm::FunctionAnalysisManager *FAM,
        llvm::LoopInfo &LIInfo);

    /**
     * Return the internal list of LoopClassifier objects
     * @return
     */
    std::unique_ptr<ResultsTy> getResults() const;

    /**
     * Internal shared pointer to the used analysis problem instance
     */
    std::shared_ptr<LoopBound::LoopBoundIDEAnalysis> problem;

    /**
     * Getter to return the internal list of LoopParameterDescriptions as a map from function name
     * to the corresponding descriptions
     * @return Map from function name to vector of LoopParameterDescriptions corresponding to the loops in the function
     */
    std::unordered_map<std::string, std::vector<LoopBound::LoopClassifier>> getLoopParameterDescriptionMap();

 private:
    /**
     * Internal map from function to the corresponding LoopCache, which contains the dominator tree and loop
     * info of the function
     */
    llvm::DenseMap<const llvm::Function*, std::unique_ptr<LoopCache>> LoopCaches;

    /**
     * Internal vector of loops found by llvm in the current program, which are used as starting points for our
     * analysis and to construct our loop classifiers
     */
    std::vector<llvm::Loop*> Loops;

    /**
     * Internal storage of the analysis results, which can be queried for loop increment values at given instructions
     * and facts.
     */
    std::unique_ptr<ResultsTy> cachedResults;

    /**
     * Internal storage of our constructed loop classifiers, which contain the information about the loops and their
     * parameters found by our analysis
     */
    std::vector<LoopClassifier> loopClassifiers;

    /**
     * Internal storage of the analysis manager so we can access llvms analysis information later on without
     * passing it down to our functions
     */
    llvm::FunctionAnalysisManager *FAM;

    /**
     * Searches the loop defined by the given LoopDescription for the store instruction that saves any increment to the
     * counter.
     * @param description LoopDescription of the loop under analysis
     * @return A store instruction if one exists, nullptr otherwise
     */
    const llvm::StoreInst *findStoreIncOfLoop(const LoopBound::LoopParameterDescription &description);

    /**
     * Checks if for a given instruction and a root fact whether there exists a calculated increment value or not
     * @param I Instruction to check
     * @param Fact Fact to check
     * @return true if a value was calculated, false otherwise
     */
    bool hasCachedValueAt(const llvm::Instruction *I, const llvm::Value *Fact) const;

    /**
     * Query our loopbound analysis results for information about the given fact at the given instruction
     * @param inst Instruction we are analysing for the fact
     * @param fact Fact that should be analysed at the given instruction
     * @return Returns a DeltaInterval instance representing the loop increment if it can be found
     */
    std::optional<LoopBound::DeltaInterval> queryIntervalAtInstuction(const llvm::Instruction *inst,
    const llvm::Value *fact);

    /**
     * Print the internally safed loop classifiers
     */
    void printClassifiers();
};

}  // namespace LoopBound

#endif  // SRC_SPEAR_ANALYSES_LOOPBOUND_LOOPBOUNDWRAPPER_H_
