/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_ANALYSES_LOOPBOUND_LOOPBOUNDWRAPPER_H_
#define SRC_SPEAR_ANALYSES_LOOPBOUND_LOOPBOUNDWRAPPER_H_

#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/PassManager.h>
#include <phasar/DataFlow/IfdsIde/SolverResults.h>
#include <phasar/PhasarLLVM/HelperAnalyses.h>

#include <memory>
#include <vector>

#include "LoopBound.h"

/**
 * Phasar result type
 * Used to shorthand any interaction with the analysis result
 */
using ResultsTy = psr::OwningSolverResults<const llvm::Instruction *, const llvm::Value *, LoopBound::DeltaInterval>;  // Phasar solver results alias

/**
 * Check Expression Class
 * Models checks, loops run against.
 * Allows for calculation of check values if they are no direct constant.
 *
 * We model loop checks as affine expression
 * check = offset + i * scaling
*/
class CheckExpr {
 public:
    /**
     * Base Value the expression is based on.
     * Should be a memory root derived from a load instruction
    */
    const llvm::Value *Base = nullptr;

    /**
     * Corresponding load instruction of the vase value
     */
    const llvm::LoadInst *BaseLoad = nullptr;

    /**
     * Represents the offset of our calculation
     */
    int64_t Offset = 0;

    /**
     * Flag to distinguish loaded checks from constants
     */
    bool isConstant = false;

    /**
     * Division value to represent the scaling factor 1/scaling
     */
    std::optional<int64_t> DivBy;

    /**
     * Multiplication value to represent the scaling factor scaling
     */
    std::optional<int64_t> MulBy;

    /**
     * Constructor
     * @param base Memory root the check is based on
     * @param baseload Load of the memory root
     * @param offset Constant offset
     */
    CheckExpr(const llvm::Value *base, const llvm::LoadInst *baseload,  int64_t offset, bool constant) :
    Base(base), BaseLoad(baseload), Offset(offset), isConstant(constant) {}

    /**
     * Calculate the actual check value from the stored information
     * @param FAM FunctionAnalysisManger used to deduce constnats
     * @param LIInfo LoopInfo to infer loop related constants
     * @return Possible calculated check value
     */
    std::optional<int64_t> calculateCheck(llvm::FunctionAnalysisManager *FAM, llvm::LoopInfo &LIInfo);
};


/**
 * LoopClassifier Class
 * Description of an analyzed loop.
 */
class LoopClassifier {
 public:
    // Which loop the classifier belongs to
    llvm::Loop *loop;

    // Which function the loop belongs to
    llvm::Function *function;

    // Increment representation
    std::optional<LoopBound::DeltaInterval> increment;

    // Possible bound calculated from the parameters
    std::optional<LoopBound::DeltaInterval> bound;

    // Init value
    std::optional<int64_t> init;

    // Loop bound
    std::optional<int64_t> check;

    // Comparison operator
    llvm::CmpInst::Predicate predicate;

    // Looptype
    LoopBound::LoopType type;

    /**
     * Simple constructor
     * @param loop The loop we are analysing
     * @param increment The DeltaInterval from the loopbound analysis describing the possible
     * increments of the loop counter
     * @param init Initial value of the loop counter found by our analysis
     * @param pred ICMP instruction used by the loop
     * @param check Constant value we are checking the loop variable against
     */
    LoopClassifier(
        llvm::Function *func,
        llvm::Loop *loop,
        std::optional<LoopBound::DeltaInterval> increment,
        std::optional<int64_t> init,
        llvm::CmpInst::Predicate pred,
        std::optional<int64_t> check,
        LoopBound::LoopType type)
    : function(func), loop(loop), increment(increment), init(init), predicate(pred), check(check), type(type) {
        this->bound = calculateBound();
    }

    /**
     * Calculate the possible loop bound from the stored information
     * @return The possible loopbound
     */
    std::optional<LoopBound::DeltaInterval> calculateBound();

    /**
     * Checks if we found a check value. Otherwise, no bound calculation is possible
     * @return true if a check value exists, false otherwise
     */
    bool isBoundable() const {
        return check.has_value();
    }

 private:
    /**
     * Performs the actual bound calculation depending on the predicate
     * @param predicate Predicate relevant for the bound
     * @param init Init value of the loop
     * @param check Check the counter is running against
     * @param increment Concrete increment per loop iteration
     * @return Possible calculated bound
     */
    static std::optional<int64_t> solveAdditiveBound(llvm::CmpInst::Predicate predicate,
                                      int64_t init, int64_t check, int64_t increment);

    /**
    * Performs the actual bound calculation depending on the predicate
    * @param predicate Predicate relevant for the bound
    * @param init Init value of the loop
    * @param check Check the counter is running against
    * @param increment Concrete increment per loop iteration
    * @return Possible calculated bound
    */
    static std::optional<int64_t> solveMultiplicativeBound(llvm::CmpInst::Predicate predicate,
                                      int64_t init, int64_t check, int64_t increment);

    /**
    * Performs the actual bound calculation depending on the predicate
    * @param predicate Predicate relevant for the bound
    * @param init Init value of the loop
    * @param check Check the counter is running against
    * @param increment Concrete increment per loop iteration
    * @return Possible calculated bound
    */
    static std::optional<int64_t> solveDivisionBound(llvm::CmpInst::Predicate predicate,
                                      int64_t init, int64_t check, int64_t increment);
};

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
    LoopBoundWrapper(std::unique_ptr<psr::HelperAnalyses> helperAnalyses, llvm::FunctionAnalysisManager *FAM);

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

    /**
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

 private:
    // Internal storage of the analysis results calculated by phasar
    std::unique_ptr<ResultsTy> cachedResults;

    // Loops found by llvm in the current program
    std::vector<llvm::Loop *> loops;

    // Internal storage of our constructed loop classifiers
    std::vector<LoopClassifier> loopClassifiers;

    // Internal storage of the analysis manager so we can access llvms analysis information later on without
    // passing it down to our functions
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
    std::optional<LoopBound::DeltaInterval> queryIntervalAtInstuction(
    const llvm::Instruction *inst, const llvm::Value *fact);

    /**
     * Print the internally safed loop classifiers
     */
    void printClassifiers();
};

#endif  // SRC_SPEAR_ANALYSES_LOOPBOUND_LOOPBOUNDWRAPPER_H_
