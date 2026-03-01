/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_LOOPCLASSIFIER_H
#define SPEAR_LOOPCLASSIFIER_H
#include <llvm/Analysis/LoopInfo.h>

#include "DeltaInterval.h"

namespace LoopBound {

/**
 * Enumeration of different loop types that can be detected during analysis.
 */
enum LoopType {
    NORMAL_LOOP,           // A standard counting loop with clear bounds
    MALFORMED_LOOP,        // A loop with malformed or unclear structure
    SYMBOLIC_BOUND_LOOP,   // A loop with symbolic (non-constant) bounds
    NON_COUNTING_LOOP,     // A loop that doesn't follow counting pattern
    NESTED_LOOP,           // A loop nested inside another loop
    UNKNOWN_LOOP
};

/**
 * Internal description of the loop we are analyzing.
 * Stores information about the loop, the counter and the related scalar values
 * attached to them
 */
struct LoopParameterDescription {
    llvm::Function *function = nullptr;
    llvm::Loop *loop = nullptr;  // The loop the description is based upon
    llvm::ICmpInst *icmp = nullptr;  // The ICMP instruction of the loop
    const llvm::Value *counterRoot = nullptr;  // The instruction defining the counter of the loop
    std::optional<int64_t> init = std::nullopt;  // Initial value of the loop
    LoopType type = LoopType::UNKNOWN_LOOP;
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

}

#endif //SPEAR_LOOPCLASSIFIER_H