/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <llvm/IR/Operator.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Analysis/ConstantFolding.h>

#include <string>

#include "analyses/loopbound/util.h"
#include "analyses/loopbound/loopBoundWrapper.h"

namespace LoopBound::Util {
std::atomic<bool> LB_DebugEnabled{false};

const llvm::Value *asValue(LoopBound::LoopBoundDomain::d_t fact) {
  return static_cast<const llvm::Value *>(fact);  // Cast domain fact to LLVM Value
}

const llvm::Value *stripAddr(const llvm::Value *pointer) {
  pointer = pointer->stripPointerCasts();  // Remove pointer casts

  while (true) {
    if (auto *gepOperator = llvm::dyn_cast<llvm::GEPOperator>(pointer)) {
      pointer = gepOperator->getPointerOperand()->stripPointerCasts();  // Extract base pointer from GEP
      continue;
    }

    if (auto *operator_inst = llvm::dyn_cast<llvm::Operator>(pointer)) {
      switch (operator_inst->getOpcode()) {
        case llvm::Instruction::BitCast:
        case llvm::Instruction::AddrSpaceCast:
          pointer = operator_inst->getOperand(0)->stripPointerCasts();  // Strip bitcast/addrspace cast
          continue;
        default:
          break;
      }
    }

    break;  // No more operators to strip
  }

  return pointer;
}

void dumpFact(LoopBound::LoopBoundIDEAnalysis *analysis, LoopBound::LoopBoundDomain::d_t fact) {
  if (!LB_DebugEnabled.load())  // Skip if debug output disabled
    return;
  if (!fact) {
    llvm::errs() << "<null>";
    return;
  }
  if (analysis->isZeroValue(fact)) {  // Check if fact is zero/bottom value
    llvm::errs() << "<ZERO>";
    return;
  }
  const llvm::Value *value = asValue(fact);  // Convert fact to Value
  const llvm::Value *strippedValue = stripAddr(value);  // Remove address operations
  llvm::errs() << value;
  if (strippedValue != value) {  // Show stripped form if different
    llvm::errs() << " (strip=" << strippedValue << ")";
  }
}

void dumpInst(LoopBound::LoopBoundDomain::n_t instruction) {
  if (!LB_DebugEnabled.load())  // Skip if debug output disabled
    return;
  if (!instruction) {
    llvm::errs() << "<null-inst>";
    return;
  }
  llvm::errs() << *instruction;
}

void dumpEF(const LoopBound::EF &edgeFunction) {
  if (edgeFunction.template isa<LoopBound::DeltaIntervalIdentity>()) {  // Identity function
    llvm::errs() << "EF=ID";
    return;
  }

  if (edgeFunction.template isa<LoopBound::DeltaIntervalBottom>() ||  // Bottom/unreachable
      llvm::isa<psr::AllBottom<LoopBound::l_t>>(edgeFunction)) {
    llvm::errs() << "EF=BOT";
    return;
  }

  if (auto *additiveFunc = edgeFunction.template dyn_cast<LoopBound::DeltaIntervalAdditive>()) {  // Addition function
    llvm::errs() << "EF=ADD[" << additiveFunc->lowerBound << "," << additiveFunc->upperBound << "]";
    return;
  }

  if (auto *multiplicativeFunc = edgeFunction.template dyn_cast<LoopBound::DeltaIntervalMultiplicative>()) {  // Multiplication function
    llvm::errs() << "EF=MUL[" << multiplicativeFunc->lowerBound << "," << multiplicativeFunc->upperBound << "]";
    return;
  }

  if (auto *divisionFunc = edgeFunction.template dyn_cast<LoopBound::DeltaIntervalDivision>()) {  // Division function
    llvm::errs() << "EF=DIV[" << divisionFunc->lowerBound << "," << divisionFunc->upperBound << "]";
    return;
  }

  llvm::errs() << "EF=<other>";
}

const llvm::Value *getUnderlyingObject(const llvm::Value *pointer) {
  if (!pointer) {  // Null pointer check
    return nullptr;
  }

  const llvm::Value *currentValue = pointer;  // Current value being processed

  while (currentValue) {
    currentValue = currentValue->stripPointerCasts();  // Remove casts first

    // Peel GEPs (instruction + operator forms)
    if (auto *gepInst = llvm::dyn_cast<llvm::GetElementPtrInst>(currentValue)) {  // GEP instruction form
      currentValue = gepInst->getPointerOperand();
      continue;
    }
    if (auto *gepOperator = llvm::dyn_cast<llvm::GEPOperator>(currentValue)) {  // GEP operator form
      currentValue = gepOperator->getPointerOperand();
      continue;
    }

    break;  // No more GEPs to peel
  }

  return currentValue;
}

const llvm::ConstantInt *tryEvalToConstInt(const llvm::Value *value) {
  if (!value) {  // Null check
    return nullptr;
  }

  if (auto *constInt = llvm::dyn_cast<llvm::ConstantInt>(value)) {  // Already a constant int
    return constInt;
  }

  // Handle integer casts of constants
  if (auto *castInst = llvm::dyn_cast<llvm::CastInst>(value)) {
    const llvm::ConstantInt *innerValue = tryEvalToConstInt(castInst->getOperand(0));  // Recursively evaluate inner operand
    if (!innerValue) {  // Inner value not constant
      return nullptr;
    }
    if (!castInst->getType()->isIntegerTy()) {  // Result type not integer
      return nullptr;
    }

    llvm::APInt constValue = innerValue->getValue();  // Extract constant value
    unsigned bitWidth = castInst->getType()->getIntegerBitWidth();  // Get target bit width

    switch (castInst->getOpcode()) {
      case llvm::Instruction::ZExt:
        constValue = constValue.zext(bitWidth);  // Zero extend
        break;
      case llvm::Instruction::SExt:
        constValue = constValue.sext(bitWidth);  // Sign extend
        break;
      case llvm::Instruction::Trunc:
        constValue = constValue.trunc(bitWidth);  // Truncate
        break;
      default:
        return nullptr;  // Unsupported cast type
    }

    return llvm::ConstantInt::get(castInst->getType()->getContext(), constValue);
  }

  // Fold simple binops of constant ints (recursively)
  auto *binOp = llvm::dyn_cast<llvm::BinaryOperator>(value);
  if (!binOp) {  // Not a binary operation
    return nullptr;
  }

  const llvm::ConstantInt *operandLeft = tryEvalToConstInt(binOp->getOperand(0));  // Left operand
  const llvm::ConstantInt *operandRight = tryEvalToConstInt(binOp->getOperand(1));  // Right operand
  if (!operandLeft || !operandRight) {  // Either operand not constant
    return nullptr;
  }

  llvm::APInt leftValue = operandLeft->getValue();  // Left constant value
  llvm::APInt rightValue = operandRight->getValue();  // Right constant value
  llvm::APInt resultValue(leftValue.getBitWidth(), 0);  // Result accumulator

  switch (binOp->getOpcode()) {
    case llvm::Instruction::Add:
      resultValue = leftValue + rightValue;  // Addition
      break;
    case llvm::Instruction::Sub:
      resultValue = leftValue - rightValue;  // Subtraction
      break;
    case llvm::Instruction::Mul:
      resultValue = leftValue * rightValue;  // Multiplication
      break;
    case llvm::Instruction::And:
      resultValue = leftValue & rightValue;  // Bitwise AND
      break;
    case llvm::Instruction::Or:
      resultValue = leftValue | rightValue;  // Bitwise OR
      break;
    case llvm::Instruction::Xor:
      resultValue = leftValue ^ rightValue;  // Bitwise XOR
      break;
    case llvm::Instruction::Shl:
      resultValue = leftValue.shl(rightValue);  // Shift left
      break;
    case llvm::Instruction::LShr:
      resultValue = leftValue.lshr(rightValue);  // Logical shift right
      break;
    case llvm::Instruction::AShr:
      resultValue = leftValue.ashr(rightValue);  // Arithmetic shift right
      break;
    case llvm::Instruction::UDiv:
      if (rightValue == 0)  // Division by zero check
        return nullptr;
      resultValue = leftValue.udiv(rightValue);  // Unsigned division
      break;
    case llvm::Instruction::SDiv:
      if (rightValue == 0)  // Division by zero check
        return nullptr;
      resultValue = leftValue.sdiv(rightValue);  // Signed division
      break;
    default:
      return nullptr;  // Unsupported operation
  }

  return llvm::ConstantInt::get(binOp->getType()->getContext(), resultValue);
}

const llvm::StoreInst *findDominatingStoreToObject(const llvm::LoadInst *loadInst,
                                                   const llvm::Value *object,
                                                   llvm::DominatorTree &dominatorTree) {
  if (!loadInst || !object) {  // Null pointer check
    return nullptr;
  }

  const llvm::StoreInst *bestStore = nullptr;  // Best matching store found so far

  const llvm::Function *function = loadInst->getFunction();  // Get parent function
  if (!function) {  // Function not found
    return nullptr;
  }

  for (const llvm::BasicBlock &basicBlock : *function) {
    for (const llvm::Instruction &instruction : basicBlock) {
      auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(&instruction);  // Cast to store instruction
      if (!storeInst) {  // Not a store instruction
        continue;
      }

      const llvm::Value *storeObject = getUnderlyingObject(storeInst->getPointerOperand());  // Extract stored object
      if (!storeObject || storeObject != object) {  // Not the target object
        continue;
      }

      if (!dominatorTree.dominates(storeInst, loadInst)) {  // Store doesn't dominate load
        continue;
      }

      if (!bestStore) {  // First matching store found
        bestStore = storeInst;
        continue;
      }

      // Prefer a later dominating store if dominance-ordered
      if (dominatorTree.dominates(bestStore, storeInst)) {
        bestStore = storeInst;
        continue;
      }

      // Same block: pick whichever appears later before the load
      if (storeInst->getParent() == loadInst->getParent() && bestStore->getParent() == loadInst->getParent()) {  // Both stores in same block as load
        const llvm::BasicBlock *block = loadInst->getParent();  // Current block
        const llvm::StoreInst *lastSeenStore = nullptr;  // Track most recent store
        for (const llvm::Instruction &currentInst : *block) {
          if (&currentInst == bestStore)  // Found best store
            lastSeenStore = bestStore;
          if (&currentInst == storeInst)  // Found new store
            lastSeenStore = storeInst;
          if (&currentInst == loadInst)  // Reached load, stop searching
            break;
        }
        bestStore = lastSeenStore;
      } else if (storeInst->getParent() == loadInst->getParent() && bestStore->getParent() != loadInst->getParent()) {  // New store in same block as load, old store not
        bestStore = storeInst;  // Prefer store in same block
      }
    }
  }

  return bestStore;
}

const llvm::ICmpInst *peelToICmp(const llvm::Value *value) {
  if (!value) {  // Null pointer check
    return nullptr;
  }

  const llvm::Value *currentValue = value;  // Current value being processed

  // strip casts
  while (auto *castInst = llvm::dyn_cast<llvm::CastInst>(currentValue)) {  // Remove cast instructions
    currentValue = castInst->getOperand(0);
  }

  // handle xor with true as logical NOT
  if (auto *binOp = llvm::dyn_cast<llvm::BinaryOperator>(currentValue)) {  // Check for binary operation
    if (binOp->getOpcode() == llvm::Instruction::Xor) {  // XOR operation
      if (auto *constInt = llvm::dyn_cast<llvm::ConstantInt>(binOp->getOperand(1))) {  // Check right operand is constant
        if (constInt->isOne()) {  // XOR with 1 is logical NOT
          currentValue = binOp->getOperand(0);
          while (auto *castInst2 = llvm::dyn_cast<llvm::CastInst>(currentValue)) {  // Strip remaining casts
            currentValue = castInst2->getOperand(0);
          }
        }
      }
    }
  }

  return llvm::dyn_cast<llvm::ICmpInst>(currentValue);  // Return as ICmp if applicable
}

const llvm::Value *getMemRootFromValue(const llvm::Value *value) {
  if (!value) {  // Null pointer check
    return nullptr;
  }

  const llvm::Value *currentValue = value;  // Current value being processed

  while (auto *castInst = llvm::dyn_cast<llvm::CastInst>(currentValue)) {  // Remove cast instructions
    currentValue = castInst->getOperand(0);
  }

  if (auto *loadInst = llvm::dyn_cast<llvm::LoadInst>(currentValue)) {  // Check if load instruction
    const llvm::Value *loadedPointer = loadInst->getPointerOperand();  // Get pointer being loaded from
    return stripAddr(loadedPointer);  // Return root of loaded address
  }

  if (auto *gepInst = llvm::dyn_cast<llvm::GetElementPtrInst>(currentValue)) {  // Check if GEP instruction
    const llvm::Value *gepPointer = gepInst;  // Pointer from GEP
    return stripAddr(gepPointer);  // Return stripped GEP pointer
  }

  return nullptr;  // No identifiable memory root
}

std::optional<int64_t> tryDeduceConstFromLoad(
const llvm::LoadInst *loadInst, llvm::DominatorTree &dominatorTree, llvm::LoopInfo &loopInfo) {
  if (!loadInst)  // Null check
    return std::nullopt;

  const llvm::Value *object = getUnderlyingObject(loadInst->getPointerOperand());  // Extract loaded object
  if (!object)  // Object not identifiable
    return std::nullopt;

  // If Obj is written in any loop that can affect this load, do not treat it as const.
  // In particular: if there is a store to Obj in any loop that contains this load
  // (or any parent loop), it's loop-variant.
  llvm::Loop *currentLoop = loopInfo.getLoopFor(loadInst->getParent());  // Get containing loop
  if (currentLoop) {
    for (llvm::Loop *loopLevel = currentLoop; loopLevel != nullptr; loopLevel = loopLevel->getParentLoop()) {  // Check all enclosing loops
      for (llvm::BasicBlock *loopBlock : loopLevel->blocks()) {  // Check each block in loop
        for (llvm::Instruction &instruction : *loopBlock) {
          auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(&instruction);  // Check if store instruction
          if (!storeInst)  // Not a store
            continue;
          const llvm::Value *storeDestination = stripAddr(storeInst->getPointerOperand());  // Get store target
          if (storeDestination == object) {  // Store modifies our object
            return std::nullopt;  // loop-variant → not const
          }
        }
      }
    }
  }

  const llvm::StoreInst *definingStore = findDominatingStoreToObject(loadInst, object, dominatorTree);  // Find store that defines value
  if (!definingStore)  // No defining store found
    return std::nullopt;

  const llvm::ConstantInt *constValue = tryEvalToConstInt(definingStore->getValueOperand());  // Try to evaluate stored value
  if (!constValue)  // Stored value not constant
    return std::nullopt;

  return constValue->getSExtValue();  // Return sign-extended constant value
}

std::string predicateToSymbol(llvm::CmpInst::Predicate predicate) {
  if (predicate) {  // Validate predicate
    switch (predicate) {
      case llvm::CmpInst::ICMP_EQ:
        return "==";  // Equal
      case llvm::CmpInst::ICMP_NE:
        return "!=";  // Not equal
      case llvm::CmpInst::ICMP_UGT:
        return ">";  // Unsigned greater than
      case llvm::CmpInst::ICMP_ULT:
        return "<";  // Unsigned less than
      case llvm::CmpInst::ICMP_UGE:
        return ">=";  // Unsigned greater or equal
      case llvm::CmpInst::ICMP_ULE:
        return "<=";  // Unsigned less or equal
      case llvm::CmpInst::ICMP_SGT:
        return ">";  // Signed greater than
      case llvm::CmpInst::ICMP_SLT:
        return "<";  // Signed less than
      case llvm::CmpInst::ICMP_SGE:
        return ">=";  // Signed greater or equal
      case llvm::CmpInst::ICMP_SLE:
        return "<=";  // Signed less or equal
      default:
        return "UNKNOWN PREDICATE";  // Unknown predicate
    }
  }
  return "UNDEFINED";  // Invalid predicate
}

llvm::CmpInst::Predicate flipPredicate(llvm::CmpInst::Predicate predicate) {
  switch (predicate) {  // Flip comparison direction
    case llvm::CmpInst::Predicate::ICMP_SLT:  // Signed less than
      return llvm::CmpInst::Predicate::ICMP_SGT;  // to greater than
    case llvm::CmpInst::Predicate::ICMP_SLE:  // Signed less or equal
      return llvm::CmpInst::Predicate::ICMP_SGE;  // to greater or equal
    case llvm::CmpInst::Predicate::ICMP_SGT:  // Signed greater than
      return llvm::CmpInst::Predicate::ICMP_SLT;  // to less than
    case llvm::CmpInst::Predicate::ICMP_SGE:  // Signed greater or equal
      return llvm::CmpInst::Predicate::ICMP_SLE;  // to less or equal

    case llvm::CmpInst::Predicate::ICMP_ULT:  // Unsigned less than
      return llvm::CmpInst::Predicate::ICMP_UGT;  // to greater than
    case llvm::CmpInst::Predicate::ICMP_ULE:  // Unsigned less or equal
      return llvm::CmpInst::Predicate::ICMP_UGE;  // to greater or equal
    case llvm::CmpInst::Predicate::ICMP_UGT:  // Unsigned greater than
      return llvm::CmpInst::Predicate::ICMP_ULT;  // to less than
    case llvm::CmpInst::Predicate::ICMP_UGE:  // Unsigned greater or equal
      return llvm::CmpInst::Predicate::ICMP_ULE;  // to less or equal

    default:
      return predicate;  // Return unchanged if not flippable
  }
}

int64_t floorDiv(int64_t dividend, int64_t divisor) {
  int64_t quotient = dividend / divisor;  // Compute quotient
  int64_t remainder = dividend % divisor;  // Compute remainder
  if (remainder != 0 && ((remainder > 0) != (divisor > 0))) {  // Adjust for sign mismatch
    --quotient;
  }
  return quotient;
}

int64_t ceilDiv(int64_t dividend, int64_t divisor) {
  int64_t quotient = dividend / divisor;  // Compute quotient
  int64_t remainder = dividend % divisor;  // Compute remainder
  if (remainder != 0 && ((remainder > 0) == (divisor > 0))) {  // Adjust for same sign
    ++quotient;
  }
  return quotient;
}

int64_t exactDiv(int64_t dividend, int64_t divisor) {
  if (dividend == 0 || divisor == 0) {  // Check for zero operands
    return 0;
  }

  return dividend / divisor;  // Exact integer division
}

const llvm::Value *stripCasts(const llvm::Value *value) {
  while (auto *castInst = llvm::dyn_cast<llvm::CastInst>(value)) {  // Remove all leading casts
    value = castInst->getOperand(0);
  }
  return value;
}

bool predicatesCoditionHolds(llvm::CmpInst::Predicate predicate, int64_t leftValue, int64_t rightValue) {
  switch (predicate) {  // Evaluate predicate
    // signed
    case llvm::CmpInst::ICMP_SLT:  // Signed less than
      return leftValue <  rightValue;
    case llvm::CmpInst::ICMP_SLE:  // Signed less or equal
      return leftValue <= rightValue;
    case llvm::CmpInst::ICMP_SGT:  // Signed greater than
      return leftValue >  rightValue;
    case llvm::CmpInst::ICMP_SGE:  // Signed greater or equal
      return leftValue >= rightValue;

    // unsigned
    case llvm::CmpInst::ICMP_ULT:  // Unsigned less than
      return static_cast<uint64_t>(leftValue) <  static_cast<uint64_t>(rightValue);
    case llvm::CmpInst::ICMP_ULE:  // Unsigned less or equal
      return static_cast<uint64_t>(leftValue) <= static_cast<uint64_t>(rightValue);
    case llvm::CmpInst::ICMP_UGT:  // Unsigned greater than
      return static_cast<uint64_t>(leftValue) >  static_cast<uint64_t>(rightValue);
    case llvm::CmpInst::ICMP_UGE:  // Unsigned greater or equal
      return static_cast<uint64_t>(leftValue) >= static_cast<uint64_t>(rightValue);

    default:
      return false;  // Unknown predicate
  }
}

bool loopIsUniform(llvm::Loop *loop, llvm::DominatorTree &dominatorTree) {
  if (!loop)  // Null loop check
    return false;

  // Still important for your init/inc inference
  llvm::BasicBlock *preheader = loop->getLoopPreheader();  // Get block before loop
  llvm::BasicBlock *latchBlock = loop->getLoopLatch();  // Get loop back-edge block
  if (!preheader || !latchBlock)  // Loop structure invalid
    return false;

  // We need at least one "main" exiting condition that bounds the backedge.
  // Prefer: condition on latch terminator
  auto *latchBranch = llvm::dyn_cast<llvm::BranchInst>(latchBlock->getTerminator());  // Get branch in latch
  if (latchBranch && latchBranch->isConditional()) {  // Latch has conditional branch
    if (LoopBound::Util::peelToICmp(latchBranch->getCondition())) {  // Condition is ICmp
      return true;  // Loop is uniform with latch condition
    }
  }

  // Otherwise: find an exiting block whose terminator condition is an ICmp
  // and which dominates the latch (so it constrains the backedge count).
  llvm::SmallVector<llvm::BasicBlock*, 8> exitingBlocks;  // Vector of exiting blocks
  loop->getExitingBlocks(exitingBlocks);  // Get all loop exit points

  for (llvm::BasicBlock *exitingBlock : exitingBlocks) {  // Check each exit point
    auto *branchInst = llvm::dyn_cast<llvm::BranchInst>(exitingBlock->getTerminator());  // Get branch instruction
    if (!branchInst || !branchInst->isConditional())  // Not a conditional branch
      continue;

    const llvm::ICmpInst *icmpInst = LoopBound::Util::peelToICmp(branchInst->getCondition());  // Extract ICmp condition
    if (!icmpInst)  // Condition not ICmp
      continue;

    // If this test dominates the latch, it bounds how often we can reach the latch.
    if (dominatorTree.dominates(exitingBlock, latchBlock)) {  // Exit condition controls loop
      return true;  // Loop is uniform
    }
  }

  return false;  // No uniform exit condition found
}

static const llvm::LoadInst *getDirectLoadFromRoot(const llvm::Value *value,
                                                   const llvm::Value *root) {
  if (!value || !root)  // Null pointer check
    return nullptr;

  value = LoopBound::Util::stripCasts(value);  // Remove casts from value

  // Accept add/sub with 0 around the load (common noise)
  if (auto *binOp = llvm::dyn_cast<llvm::BinaryOperator>(value)) {  // Check for binary operation
    auto opcode = binOp->getOpcode();  // Get operation type
    if (opcode == llvm::Instruction::Add || opcode == llvm::Instruction::Sub) {  // Addition or subtraction
      const llvm::Value *operandLeft = LoopBound::Util::stripCasts(binOp->getOperand(0));  // Left operand
      const llvm::Value *operandRight = LoopBound::Util::stripCasts(binOp->getOperand(1));  // Right operand

      // x + 0 or x - 0
      if (auto *constInt = llvm::dyn_cast<llvm::ConstantInt>(operandRight)) {  // Check if right is constant
        if (constInt->isZero()) {  // Constant is zero
          value = operandLeft;  // Simplify away the zero operation
        }
      }
    }
  }

  auto *loadInst = llvm::dyn_cast<llvm::LoadInst>(value);  // Cast to load instruction
  if (!loadInst)  // Not a load instruction
    return nullptr;

  const llvm::Value *loadedPointer = LoopBound::Util::stripAddr(loadInst->getPointerOperand());  // Get loaded address
  return (loadedPointer == root) ? loadInst : nullptr;  // Return load if root matches
}

bool loopConditionCannotBeDeduced(LoopBound::LoopParameterDescription description,
                                  llvm::FunctionAnalysisManager *analysisManager,
                                  llvm::DominatorTree &dominatorTree,
                                  llvm::LoopInfo &loopInfo) {
  auto checkExpression = LoopBoundWrapper::findLoopCheckExpr(description, analysisManager, loopInfo);  // Find loop exit condition
  if (checkExpression.has_value()) {  // Condition found
    auto bound = checkExpression.value().calculateCheck(analysisManager, loopInfo);  // Calculate bound value
    return bound.has_value();  // Return true if bound can be deduced
  }
  return true;  // Cannot deduce condition
}

bool loopInitCannotBeDeduced(LoopBound::LoopParameterDescription description) {
  return !description.init.has_value();  // Check if initial value not available
}

bool loopIsCounting(llvm::Loop *loop, llvm::ICmpInst *icmpCondition) {
  if (!loop || !icmpCondition)  // Null pointer check
    return false;

  auto counterInfo = LoopBoundIDEAnalysis::findCounterFromICMP(icmpCondition, loop);  // Extract loop counter
  if (!counterInfo || counterInfo->Roots.empty()) {  // Counter not found
    return false;
  }

  const llvm::Value *counterRoot = LoopBound::Util::stripAddr(counterInfo->Roots[0]);  // Get root of counter variable

  // Look for a store to that root inside the loop that we can parse as increment
  bool foundIncrementStore = false;  // Track if increment found

  for (llvm::BasicBlock *loopBlock : loop->blocks()) {  // Check each block in loop
    for (llvm::Instruction &instruction : *loopBlock) {  // Check each instruction
      auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(&instruction);  // Check if store
      if (!storeInst)  // Not a store instruction
        continue;

      // must store to the same root object
      const llvm::Value *storeDestination = LoopBound::Util::stripAddr(storeInst->getPointerOperand());  // Get store target
      if (storeDestination != counterRoot)  // Not storing to counter
        continue;

      // can we extract a constant increment/mul/div etc?
      if (LoopBoundIDEAnalysis::extractConstIncFromStore(storeInst, counterRoot).has_value()) {  // Extract increment pattern
        foundIncrementStore = true;  // Found valid increment operation
        break;
      }
    }
    if (foundIncrementStore)  // Early exit if found
      break;
  }

  return foundIncrementStore;
}

static bool isMemoryRootWrittenInLoop(const llvm::Value *baseMemory,
                                     llvm::Loop *loop) {
  if (!baseMemory || !loop)  // Null pointer check
    return false;

  const llvm::Value *normalizedBase = LoopBound::Util::stripAddr(baseMemory);  // Normalize base address

  for (llvm::BasicBlock *loopBlock : loop->blocks()) {  // Check each block in loop
    for (llvm::Instruction &instruction : *loopBlock) {  // Check each instruction
      auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(&instruction);  // Check if store
      if (!storeInst)  // Not a store instruction
        continue;

      const llvm::Value *storeDestination =
          LoopBound::Util::stripAddr(storeInst->getPointerOperand());  // Get store target
      if (storeDestination == normalizedBase) {  // Store modifies our base address
        return true;  // Base is written in loop
      }
    }
  }
  return false;  // Base not written in loop
}

bool loopIsDependentNested(const LoopParameterDescription &description,
                           llvm::LoopInfo &loopInfo) {
  if (!description.loop || !description.icmp || !description.counterRoot) {  // Validate inputs
    return false;
  }

  llvm::Loop *currentLoop = description.loop;  // Current loop
  llvm::Loop *parentLoop = currentLoop->getParentLoop();  // Enclosing loop
  if (!parentLoop) {  // No parent loop
    return false;
  }

  const llvm::Value *counterRoot =
      LoopBound::Util::stripAddr(description.counterRoot);  // Get counter variable root

  const llvm::Value *operandZero =
      LoopBound::Util::stripCasts(description.icmp->getOperand(0));  // Left side of comparison
  const llvm::Value *operandOne =
      LoopBound::Util::stripCasts(description.icmp->getOperand(1));  // Right side of comparison

  auto normalizaValue = [](const llvm::Value *val) -> const llvm::Value * {  // Normalize value to base form
    if (!val)  // Null check
      return nullptr;
    return LoopBound::Util::stripAddr(
             LoopBound::Util::stripCasts(val));  // Remove casts and address ops
  };

  auto exprZero = LoopBoundWrapper::peelBasePlusConst(operandZero);  // Parse left expression
  auto exprOne = LoopBoundWrapper::peelBasePlusConst(operandOne);  // Parse right expression

  auto isCounterExpression = [&](const std::optional<CheckExpr> &expression) -> bool {  // Check if expression is counter-related
    if (!expression || expression->isConstant)  // Not valid or is constant
      return false;
    return normalizaValue(expression->Base) == counterRoot;  // Base matches counter
  };

  const bool operandZeroIsCounter = isCounterExpression(exprZero);  // Left is counter side
  const bool operandOneIsCounter = isCounterExpression(exprOne);  // Right is counter side

  const llvm::Value *boundValue = nullptr;  // Value that bounds the loop

  if (operandZeroIsCounter && !operandOneIsCounter) {  // Counter on left, bound on right
    boundValue = operandOne;
  } else if (!operandZeroIsCounter && operandOneIsCounter) {  // Bound on left, counter on right
    boundValue = operandZero;
  } else {
    // ambiguous or malformed -> don't classify as dependent nested
    return false;  // Cannot determine bound
  }

  auto boundExpression = LoopBoundWrapper::peelBasePlusConst(boundValue);  // Parse bound expression
  if (!boundExpression || boundExpression->isConstant || !boundExpression->Base) {  // Invalid bound expression
    return false;
  }

  const llvm::Value *boundBase = normalizaValue(boundExpression->Base);  // Extract base of bound

  // Check all enclosing loops
  for (llvm::Loop *checkLoop = parentLoop; checkLoop != nullptr; checkLoop = checkLoop->getParentLoop()) {  // Check each parent
    if (isMemoryRootWrittenInLoop(boundBase, checkLoop)) {  // Bound written in parent loop
      return true;  // Loop is dependent nested
    }
  }

  return false;  // Loop is not dependent nested
}

LoopBound::LoopType determineLoopType(LoopBound::LoopParameterDescription description,
llvm::FunctionAnalysisManager *analysisManager) {
  auto *preheaderBlock = description.loop->getLoopPreheader();  // Get block before loop
  auto *parentFunction = preheaderBlock->getParent();  // Get containing function

  auto &domTree = analysisManager->getResult<llvm::DominatorTreeAnalysis>(*parentFunction);  // Get dominator tree analysis
  auto &loopInformation = analysisManager->getResult<llvm::LoopAnalysis>(*parentFunction);  // Get loop analysis

  // Check if loop is non uniform
  if (!loopIsUniform(description.loop, domTree)) {  // Check structural uniformity
    return LoopType::MALFORMED_LOOP;  // Non-uniform loop
  }

  if (loopIsDependentNested(description, loopInformation)) {  // Check if bound depends on outer loop
    return LoopType::NESTED_LOOP;  // Dependent nested loop
  }

  if (!loopConditionCannotBeDeduced(description, analysisManager, domTree, loopInformation)) {  // Check if condition deducible
    return LoopType::SYMBOLIC_BOUND_LOOP;  // Has symbolic bound
  }

  if (loopInitCannotBeDeduced(description)) {  // Check if initial value unknown
    return LoopType::SYMBOLIC_BOUND_LOOP;  // Has symbolic initial value
  }

  if (!loopIsCounting(description.loop, description.icmp)) {  // Check if counter increments uniformly
    return LoopType::NON_COUNTING_LOOP;  // Non-counting loop
  }

  return LoopType::NORMAL_LOOP;  // Standard counting loop
}

std::string LoopTypeToString(LoopBound::LoopType loopType) {
  switch (loopType) {  // Convert loop type enum to string
    case LoopBound::LoopType::NORMAL_LOOP:
      return "NORMAL_LOOP";  // Standard counting loop
    case LoopType::MALFORMED_LOOP:
      return "MALFORMED_LOOP";  // Structurally invalid loop
    case LoopType::SYMBOLIC_BOUND_LOOP:
      return "SYMBOLIC_BOUND_LOOP";  // Bound not statically known
    case LoopType::NON_COUNTING_LOOP:
      return "NON_COUNTING_LOOP";  // Counter doesn't increment uniformly
    case LoopType::NESTED_LOOP:
      return "NESTED_LOOP";  // Bound depends on outer loop state
    case LoopType::UNKNOWN_LOOP:
      return "UNKNOWN_LOOP";  // Cannot classify loop
    default:
      return "";  // Invalid loop type
  }
}

LoopType strToLoopType(const std::string &loopTypeString) {
  if (loopTypeString == "NORMAL_LOOP") {  // Check for standard loop
    return LoopType::NORMAL_LOOP;
  }
  if (loopTypeString == "MALFORMED_LOOP") {  // Check for malformed loop
    return LoopType::MALFORMED_LOOP;
  }
  if (loopTypeString == "SYMBOLIC_BOUND_LOOP") {  // Check for symbolic bound
    return LoopType::SYMBOLIC_BOUND_LOOP;
  }
  if (loopTypeString == "NON_COUNTING_LOOP") {  // Check for non-counting loop
    return LoopType::NON_COUNTING_LOOP;
  }
  if (loopTypeString == "NESTED_LOOP") {  // Check for nested loop
    return LoopType::NESTED_LOOP;
  }
  if (loopTypeString == "UNKNOWN_LOOP") {  // Check for unknown loop
    return LoopType::UNKNOWN_LOOP;
  }
  return LoopType::UNKNOWN_LOOP;  // Default to unknown if no match
}

}  // namespace LoopBound::Util
