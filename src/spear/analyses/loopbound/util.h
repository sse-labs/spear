/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_ANALYSES_LOOPBOUND_UTIL_H_
#define SRC_SPEAR_ANALYSES_LOOPBOUND_UTIL_H_

#include <llvm/IR/Constants.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Instructions.h>

#include <atomic>
#include <optional>
#include <string>

#include "LoopBound.h"
#include "LoopBoundEdgeFunction.h"

namespace LoopBound::Util {

/**
 * Debug Classifier
 */
extern std::atomic<bool> LB_DebugEnabled;
inline constexpr const char *LB_TAG = "[LBDBG]";

/**
 * Convert the given fact to a llvm::Value
 * @param fact Input fact
 * @return llvm::Value
 */
const llvm::Value *asValue(LoopBound::LoopBoundDomain::d_t fact);

/**
 * Removes any stuff that comes before the variable in the llvm::Value
 *
 * Example:
 * %p2 = bitcast i8* %p to i32* becomes %p
 *
 * @param Ptr input Value
 * @return llvm::Value representing the variable without additional stuff
 */
const llvm::Value *stripAddr(const llvm::Value *Ptr);

/**
 * Dump fact of the current analysis
 *
 * @param analysis LoopBoundIDEAnalysis to dump from
 * @param fact Fact to dump
 */
void dumpFact(LoopBound::LoopBoundIDEAnalysis *analysis, LoopBound::LoopBoundDomain::d_t fact);

/**
 * Dump Instruction
 * @param inst Instruction to dump
 */
void dumpInst(LoopBound::LoopBoundDomain::n_t inst);

/**
 * Dump the given edge function
 * @param edgeFunction
 */
void dumpEF(const LoopBound::EF &edgeFunction);

/**
 * Given a pointer expression this function returns the original object the pointer is referring to
 *
 * @param Ptr Pointer to clean
 * @return Cleaned pointer
 */
const llvm::Value *getUnderlyingObject(const llvm::Value *Ptr);

/**
 * Tries to infer an integer from the given value.
 * @param val Value to infer from
 * @return
 */
const llvm::ConstantInt *tryEvalToConstInt(const llvm::Value *val);

/**
 * Finds the most fitting store instruction corresponding to the given object before the given load happens
 *
 * @param LI Load instruction the store happends before
 * @param Obj Object the store is referring to
 * @param DT DominatorTree
 * @return Store instruction if found, nullptr in any other case
 */
const llvm::StoreInst *findDominatingStoreToObject(const llvm::LoadInst *LI,
                                                   const llvm::Value *Obj,
                                                   llvm::DominatorTree &DT);

/**
 * Tries to infer the ICMP value from the given llvm value
 * @param val Value that should be peeled to the ICMP
 * @return Returns the ICMP if the value is a ICMP instruction, nullptr otherwise
 */
const llvm::ICmpInst *peelToICmp(const llvm::Value *val);

/**
 * Takes a given value and infers the memory location it is refering to
 * @param val Value to infer the memory location from
 * @return The memory location if it can be inferred, nullptr otherwise
 */
const llvm::Value *getMemRootFromValue(const llvm::Value *val);

/**
 * Infer the constant integer value from the given load instruction if it loads a constant
 * @param LI Load instruction to infer from
 * @param DT DominatorTree
 * @return Optional value representing the constant integer value
 */
std::optional<int64_t> tryDeduceConstFromLoad(
const llvm::LoadInst *LI, llvm::DominatorTree &DT, llvm::LoopInfo &LIInfo);

/**
 * Convert a given predicate to a string
 * @param pred Predicate to generate the string representation for
 * @return String representing the predicate (==, !=, <, >, <=, >=)
 */
std::string predicateToSymbol(llvm::CmpInst::Predicate pred);

/**
 * Flip an ICMP predicate direction (e.g., < -> >).
 * @param predicate Predicate to flip
 * @return Flipped predicate, or original if not flippable
 */
llvm::CmpInst::Predicate flipPredicate(llvm::CmpInst::Predicate predicate);

/**
 * Floor division with correct behavior for negative operands.
 * @param a Dividend
 * @param b Divisor
 * @return Floor(a / b)
 */
int64_t floorDiv(int64_t a, int64_t b);

/**
 * Ceil division with correct behavior for negative operands.
 * @param a Dividend
 * @param b Divisor
 * @return Ceil(a / b)
 */
int64_t ceilDiv(int64_t a, int64_t b);

/**
 * Exact integer division (returns 0 if either operand is 0).
 * @param a Dividend
 * @param b Divisor
 * @return a / b, or 0 if a or b is 0
 */
int64_t exactDiv(int64_t a, int64_t b);

/**
 * Get a direct load from a given root pointer expression.
 * @param V Value to inspect
 * @param Root Root pointer to match
 * @return Load instruction if it directly loads from Root, nullptr otherwise
 */
static const llvm::LoadInst *getDirectLoadFromRoot(const llvm::Value *V,
                                                   const llvm::Value *Root);

/**
 * Strip cast instructions from the given value.
 * @param V Value to strip
 * @return Value without leading casts
 */
const llvm::Value *stripCasts(const llvm::Value *V);

/**
 * Check if a predicate holds for two integer values.
 * @param pred Predicate to evaluate
 * @param val Left-hand value
 * @param check Right-hand value
 * @return True if predicate holds, otherwise false
 */
bool predicatesCoditionHolds(llvm::CmpInst::Predicate pred, int64_t val, int64_t check);

/**
 * Determine the loop type for the given loop description.
 * @param description Loop parameter description
 * @param FAM FunctionAnalysisManager
 * @return LoopType classification
 */
LoopBound::LoopType determineLoopType(LoopBound::LoopParameterDescription description,
llvm::FunctionAnalysisManager *FAM);

/**
 * Check whether a loop has a uniform, analyzable structure.
 * @param loop Loop to inspect
 * @param DT DominatorTree
 * @return True if uniform, otherwise false
 */
bool loopIsUniform(llvm::Loop *loop, llvm::DominatorTree &DT);

/**
 * Check whether the loop condition cannot be deduced.
 * @param description Loop parameter description
 * @param FAM FunctionAnalysisManager
 * @param DT DominatorTree
 * @param LIInfo LoopInfo
 * @return True if condition cannot be deduced, otherwise false
 */
bool loopConditionCannotBeDeduced(LoopBound::LoopParameterDescription description,
llvm::FunctionAnalysisManager *FAM,
                                  llvm::DominatorTree &DT,
                                  llvm::LoopInfo &LIInfo);

/**
 * Check whether the loop init cannot be deduced.
 * @param description Loop parameter description
 * @return True if init cannot be deduced, otherwise false
 */
bool loopInitCannotBeDeduced(LoopBound::LoopParameterDescription description);

/**
 * Check whether a loop is a counting loop based on its condition.
 * @param loop Loop to inspect
 * @param IC ICmp condition used by the loop
 * @return True if counting, otherwise false
 */
bool loopIsCounting(llvm::Loop *loop, llvm::ICmpInst *IC);

/**
 * Check whether the loop bound variable is modified inside the loop.
 * @param loop Loop to inspect
 * @return True if modified, otherwise false
 */
bool loopBoundIsModified(llvm::Loop *loop);

/**
 * Check whether the loop counter is monotonic.
 * @param loop Loop to inspect
 * @return True if monotonic, otherwise false
 */
bool loopisMonotonic(llvm::Loop *loop);

/**
 * Check whether a memory root is written within a loop.
 * @param Base Base memory location
 * @param L Loop to inspect
 * @return True if written, otherwise false
 */
static bool isMemoryRootWrittenInLoop(const llvm::Value *Base,
                                     llvm::Loop *L);

/**
 * Check whether a loop is dependent nested (bound depends on outer loop state).
 * @param desc Loop parameter description
 * @param LIInfo LoopInfo
 * @return True if dependent nested, otherwise false
 */
bool loopIsDependentNested(const LoopParameterDescription &desc,
                           llvm::LoopInfo &LIInfo);

/**
 * Convert a loop type to its string name.
 * @param type Loop type to stringify
 * @return String representation of the loop type
 */
std::string LoopTypeToString(LoopBound::LoopType type);

}  // namespace LoopBound::Util

#endif  // SRC_SPEAR_ANALYSES_LOOPBOUND_UTIL_H_
