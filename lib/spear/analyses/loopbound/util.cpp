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
  return static_cast<const llvm::Value *>(fact);
}

const llvm::Value *stripAddr(const llvm::Value *pointer) {
  pointer = pointer->stripPointerCasts();

  while (true) {
    if (auto *gepOperator = llvm::dyn_cast<llvm::GEPOperator>(pointer)) {
      pointer = gepOperator->getPointerOperand()->stripPointerCasts();
      continue;
    }

    if (auto *operator_inst = llvm::dyn_cast<llvm::Operator>(pointer)) {
      switch (operator_inst->getOpcode()) {
        case llvm::Instruction::BitCast:
        case llvm::Instruction::AddrSpaceCast:
          pointer = operator_inst->getOperand(0)->stripPointerCasts();
          continue;
        default:
          break;
      }
    }

    break;
  }

  return pointer;
}

void dumpFact(LoopBound::LoopBoundIDEAnalysis *analysis, LoopBound::LoopBoundDomain::d_t fact) {
  if (!LB_DebugEnabled.load())
    return;
  if (!fact) {
    llvm::errs() << "<null>";
    return;
  }
  if (analysis->isZeroValue(fact)) {
    llvm::errs() << "<ZERO>";
    return;
  }
  const llvm::Value *value = asValue(fact);
  const llvm::Value *strippedValue = stripAddr(value);
  llvm::errs() << value;
  if (strippedValue != value) {
    llvm::errs() << " (strip=" << strippedValue << ")";
  }
}

void dumpInst(LoopBound::LoopBoundDomain::n_t instruction) {
  if (!LB_DebugEnabled.load())
    return;
  if (!instruction) {
    llvm::errs() << "<null-inst>";
    return;
  }
  llvm::errs() << *instruction;
}

void dumpEF(const LoopBound::EF &edgeFunction) {
  if (edgeFunction.template isa<LoopBound::DeltaIntervalIdentity>()) {
    llvm::errs() << "EF=ID";
    return;
  }

  if (edgeFunction.template isa<LoopBound::DeltaIntervalBottom>() ||
      llvm::isa<psr::AllBottom<LoopBound::l_t>>(edgeFunction)) {
    llvm::errs() << "EF=BOT";
    return;
  }

  if (auto *additiveFunc = edgeFunction.template dyn_cast<LoopBound::DeltaIntervalAdditive>()) {
    llvm::errs() << "EF=ADD[" << additiveFunc->lowerBound << "," << additiveFunc->upperBound << "]";
    return;
  }

  if (auto *multiplicativeFunc =
          edgeFunction.template dyn_cast<LoopBound::DeltaIntervalMultiplicative>()) {
    llvm::errs() << "EF=MUL[" << multiplicativeFunc->lowerBound << ","
                 << multiplicativeFunc->upperBound << "]";
    return;
  }

  if (auto *divisionFunc = edgeFunction.template dyn_cast<LoopBound::DeltaIntervalDivision>()) {
    llvm::errs() << "EF=DIV[" << divisionFunc->lowerBound << "," << divisionFunc->upperBound << "]";
    return;
  }

  llvm::errs() << "EF=<other>";
}

const llvm::Value *getUnderlyingObject(const llvm::Value *pointer) {
  if (!pointer) {
    return nullptr;
  }

  const llvm::Value *currentValue = pointer;

  while (currentValue) {
    currentValue = currentValue->stripPointerCasts();

    // Peel GEPs (instruction + operator forms)
    if (auto *gepInst = llvm::dyn_cast<llvm::GetElementPtrInst>(currentValue)) {
      currentValue = gepInst->getPointerOperand();
      continue;
    }
    if (auto *gepOperator = llvm::dyn_cast<llvm::GEPOperator>(currentValue)) {
      currentValue = gepOperator->getPointerOperand();
      continue;
    }

    break;
  }

  return currentValue;
}

const llvm::ConstantInt *tryEvalToConstInt(const llvm::Value *value) {
  if (!value) {
    return nullptr;
  }

  if (auto *constInt = llvm::dyn_cast<llvm::ConstantInt>(value)) {
    return constInt;
  }

  // Handle integer casts of constants
  if (auto *castInst = llvm::dyn_cast<llvm::CastInst>(value)) {
    const llvm::ConstantInt *innerValue = tryEvalToConstInt(castInst->getOperand(0));
    if (!innerValue) {
      return nullptr;
    }
    if (!castInst->getType()->isIntegerTy()) {
      return nullptr;
    }

    llvm::APInt constValue = innerValue->getValue();
    unsigned bitWidth = castInst->getType()->getIntegerBitWidth();

    switch (castInst->getOpcode()) {
      case llvm::Instruction::ZExt:
        constValue = constValue.zext(bitWidth);
        break;
      case llvm::Instruction::SExt:
        constValue = constValue.sext(bitWidth);
        break;
      case llvm::Instruction::Trunc:
        constValue = constValue.trunc(bitWidth);
        break;
      default:
        return nullptr;
    }

    return llvm::ConstantInt::get(castInst->getType()->getContext(), constValue);
  }

  // Fold simple binops of constant ints (recursively)
  auto *binOp = llvm::dyn_cast<llvm::BinaryOperator>(value);
  if (!binOp) {
    return nullptr;
  }

  const llvm::ConstantInt *operandLeft = tryEvalToConstInt(binOp->getOperand(0));
  const llvm::ConstantInt *operandRight = tryEvalToConstInt(binOp->getOperand(1));
  if (!operandLeft || !operandRight) {
    return nullptr;
  }

  llvm::APInt leftValue = operandLeft->getValue();
  llvm::APInt rightValue = operandRight->getValue();
  llvm::APInt resultValue(leftValue.getBitWidth(), 0);

  switch (binOp->getOpcode()) {
    case llvm::Instruction::Add:
      resultValue = leftValue + rightValue;
      break;
    case llvm::Instruction::Sub:
      resultValue = leftValue - rightValue;
      break;
    case llvm::Instruction::Mul:
      resultValue = leftValue * rightValue;
      break;
    case llvm::Instruction::And:
      resultValue = leftValue & rightValue;
      break;
    case llvm::Instruction::Or:
      resultValue = leftValue | rightValue;
      break;
    case llvm::Instruction::Xor:
      resultValue = leftValue ^ rightValue;
      break;
    case llvm::Instruction::Shl:
      resultValue = leftValue.shl(rightValue);
      break;
    case llvm::Instruction::LShr:
      resultValue = leftValue.lshr(rightValue);
      break;
    case llvm::Instruction::AShr:
      resultValue = leftValue.ashr(rightValue);
      break;
    case llvm::Instruction::UDiv:
      if (rightValue == 0)
        return nullptr;
      resultValue = leftValue.udiv(rightValue);
      break;
    case llvm::Instruction::SDiv:
      if (rightValue == 0)
        return nullptr;
      resultValue = leftValue.sdiv(rightValue);
      break;
    default:
      return nullptr;
  }

  return llvm::ConstantInt::get(binOp->getType()->getContext(), resultValue);
}

const llvm::StoreInst *findDominatingStoreToObject(const llvm::LoadInst *loadInst,
                                                   const llvm::Value *object,
                                                   llvm::DominatorTree &dominatorTree) {
  if (!loadInst || !object) {
    return nullptr;
  }

  const llvm::StoreInst *bestStore = nullptr;

  const llvm::Function *function = loadInst->getFunction();
  if (!function) {
    return nullptr;
  }

  for (const llvm::BasicBlock &basicBlock : *function) {
    for (const llvm::Instruction &instruction : basicBlock) {
      auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(&instruction);
      if (!storeInst) {
        continue;
      }

      const llvm::Value *storeObject = getUnderlyingObject(storeInst->getPointerOperand());
      if (!storeObject || storeObject != object) {
        continue;
      }

      if (!dominatorTree.dominates(storeInst, loadInst)) {
        continue;
      }

      if (!bestStore) {
        bestStore = storeInst;
        continue;
      }

      // Prefer a later dominating store if dominance-ordered
      if (dominatorTree.dominates(bestStore, storeInst)) {
        bestStore = storeInst;
        continue;
      }

      // Same block: pick whichever appears later before the load
      if (storeInst->getParent() == loadInst->getParent() && bestStore->getParent() == loadInst->getParent()) {
        const llvm::BasicBlock *block = loadInst->getParent();
        const llvm::StoreInst *lastSeenStore = nullptr;
        for (const llvm::Instruction &currentInst : *block) {
          if (&currentInst == bestStore)
            lastSeenStore = bestStore;
          if (&currentInst == storeInst)
            lastSeenStore = storeInst;
          if (&currentInst == loadInst)
            break;
        }
        bestStore = lastSeenStore;
      } else if (storeInst->getParent() == loadInst->getParent() && bestStore->getParent() != loadInst->getParent()) {
        bestStore = storeInst;
      }
    }
  }

  return bestStore;
}

const llvm::ICmpInst *peelToICmp(const llvm::Value *value) {
  if (!value) {
    return nullptr;
  }

  const llvm::Value *currentValue = value;

  // strip casts
  while (auto *castInst = llvm::dyn_cast<llvm::CastInst>(currentValue)) {
    currentValue = castInst->getOperand(0);
  }

  // handle xor with true as logical NOT
  if (auto *binOp = llvm::dyn_cast<llvm::BinaryOperator>(currentValue)) {
    if (binOp->getOpcode() == llvm::Instruction::Xor) {
      if (auto *constInt = llvm::dyn_cast<llvm::ConstantInt>(binOp->getOperand(1))) {
        if (constInt->isOne()) {
          currentValue = binOp->getOperand(0);
          while (auto *castInst2 = llvm::dyn_cast<llvm::CastInst>(currentValue)) {
            currentValue = castInst2->getOperand(0);
          }
        }
      }
    }
  }

  return llvm::dyn_cast<llvm::ICmpInst>(currentValue);
}

const llvm::Value *getMemRootFromValue(const llvm::Value *value) {
  if (!value) {
    return nullptr;
  }

  const llvm::Value *currentValue = value;

  while (auto *castInst = llvm::dyn_cast<llvm::CastInst>(currentValue)) {
    currentValue = castInst->getOperand(0);
  }

  if (auto *loadInst = llvm::dyn_cast<llvm::LoadInst>(currentValue)) {
    const llvm::Value *loadedPointer = loadInst->getPointerOperand();
    return stripAddr(loadedPointer);
  }

  if (auto *gepInst = llvm::dyn_cast<llvm::GetElementPtrInst>(currentValue)) {
    const llvm::Value *gepPointer = gepInst;
    return stripAddr(gepPointer);
  }

  return nullptr;
}

std::optional<int64_t> tryDeduceConstFromLoad(
const llvm::LoadInst *loadInst, llvm::DominatorTree &dominatorTree, llvm::LoopInfo &loopInfo) {
  if (!loadInst)
    return std::nullopt;

  const llvm::Value *object = getUnderlyingObject(loadInst->getPointerOperand());
  if (!object)
    return std::nullopt;

  // If Obj is written in any loop that can affect this load, do not treat it as const.
  // In particular: if there is a store to Obj in any loop that contains this load
  // (or any parent loop), it's loop-variant.
  llvm::Loop *currentLoop = loopInfo.getLoopFor(loadInst->getParent());
  if (currentLoop) {
    for (llvm::Loop *loopLevel = currentLoop; loopLevel != nullptr; loopLevel = loopLevel->getParentLoop()) {
      for (llvm::BasicBlock *loopBlock : loopLevel->blocks()) {
        for (llvm::Instruction &instruction : *loopBlock) {
          auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(&instruction);
          if (!storeInst)
            continue;
          const llvm::Value *storeDestination = stripAddr(storeInst->getPointerOperand());
          if (storeDestination == object) {
            return std::nullopt;
          }
        }
      }
    }
  }

  const llvm::StoreInst *definingStore = findDominatingStoreToObject(loadInst, object, dominatorTree);
  if (!definingStore)
    return std::nullopt;

  const llvm::ConstantInt *constValue = tryEvalToConstInt(definingStore->getValueOperand());
  if (!constValue)
    return std::nullopt;

  return constValue->getSExtValue();
}

std::string predicateToSymbol(llvm::CmpInst::Predicate predicate) {
  if (predicate) {
    switch (predicate) {
      case llvm::CmpInst::ICMP_EQ:
        return "==";
      case llvm::CmpInst::ICMP_NE:
        return "!=";
      case llvm::CmpInst::ICMP_UGT:
        return ">";
      case llvm::CmpInst::ICMP_ULT:
        return "<";
      case llvm::CmpInst::ICMP_UGE:
        return ">=";
      case llvm::CmpInst::ICMP_ULE:
        return "<=";
      case llvm::CmpInst::ICMP_SGT:
        return ">";
      case llvm::CmpInst::ICMP_SLT:
        return "<";
      case llvm::CmpInst::ICMP_SGE:
        return ">=";
      case llvm::CmpInst::ICMP_SLE:
        return "<=";
      default:
        return "UNKNOWN PREDICATE";
    }
  }
  return "UNDEFINED";
}

llvm::CmpInst::Predicate flipPredicate(llvm::CmpInst::Predicate predicate) {
  switch (predicate) {
    case llvm::CmpInst::Predicate::ICMP_SLT:
      return llvm::CmpInst::Predicate::ICMP_SGT;
    case llvm::CmpInst::Predicate::ICMP_SLE:
      return llvm::CmpInst::Predicate::ICMP_SGE;
    case llvm::CmpInst::Predicate::ICMP_SGT:
      return llvm::CmpInst::Predicate::ICMP_SLT;
    case llvm::CmpInst::Predicate::ICMP_SGE:
      return llvm::CmpInst::Predicate::ICMP_SLE;

    case llvm::CmpInst::Predicate::ICMP_ULT:
      return llvm::CmpInst::Predicate::ICMP_UGT;
    case llvm::CmpInst::Predicate::ICMP_ULE:
      return llvm::CmpInst::Predicate::ICMP_UGE;
    case llvm::CmpInst::Predicate::ICMP_UGT:
      return llvm::CmpInst::Predicate::ICMP_ULT;
    case llvm::CmpInst::Predicate::ICMP_UGE:
      return llvm::CmpInst::Predicate::ICMP_ULE;

    default:
      return predicate;
  }
}

int64_t floorDiv(int64_t dividend, int64_t divisor) {
  int64_t quotient = dividend / divisor;
  int64_t remainder = dividend % divisor;
  if (remainder != 0 && ((remainder > 0) != (divisor > 0))) {
    --quotient;
  }
  return quotient;
}

int64_t ceilDiv(int64_t dividend, int64_t divisor) {
  int64_t quotient = dividend / divisor;
  int64_t remainder = dividend % divisor;
  if (remainder != 0 && ((remainder > 0) == (divisor > 0))) {
    ++quotient;
  }
  return quotient;
}

int64_t exactDiv(int64_t dividend, int64_t divisor) {
  if (dividend == 0 || divisor == 0) {
    return 0;
  }

  return dividend / divisor;
}

const llvm::Value *stripCasts(const llvm::Value *value) {
  while (auto *castInst = llvm::dyn_cast<llvm::CastInst>(value)) {
    value = castInst->getOperand(0);
  }
  return value;
}

bool predicatesCoditionHolds(llvm::CmpInst::Predicate predicate, int64_t leftValue, int64_t rightValue) {
  switch (predicate) {
    // signed
    case llvm::CmpInst::ICMP_SLT:
      return leftValue <  rightValue;
    case llvm::CmpInst::ICMP_SLE:
      return leftValue <= rightValue;
    case llvm::CmpInst::ICMP_SGT:
      return leftValue >  rightValue;
    case llvm::CmpInst::ICMP_SGE:
      return leftValue >= rightValue;

    // unsigned
    case llvm::CmpInst::ICMP_ULT:
      return static_cast<uint64_t>(leftValue) <  static_cast<uint64_t>(rightValue);
    case llvm::CmpInst::ICMP_ULE:
      return static_cast<uint64_t>(leftValue) <= static_cast<uint64_t>(rightValue);
    case llvm::CmpInst::ICMP_UGT:
      return static_cast<uint64_t>(leftValue) >  static_cast<uint64_t>(rightValue);
    case llvm::CmpInst::ICMP_UGE:
      return static_cast<uint64_t>(leftValue) >= static_cast<uint64_t>(rightValue);

    default:
      return false;
  }
}

bool loopIsUniform(llvm::Loop *loop, llvm::DominatorTree &dominatorTree) {
  if (!loop)
    return false;

  // Still important for your init/inc inference
  llvm::BasicBlock *preheader = loop->getLoopPreheader();
  llvm::BasicBlock *latchBlock = loop->getLoopLatch();
  if (!preheader || !latchBlock)
    return false;

  // We need at least one "main" exiting condition that bounds the backedge.
  auto *latchBranch = llvm::dyn_cast<llvm::BranchInst>(latchBlock->getTerminator());
  if (latchBranch && latchBranch->isConditional()) {
    if (LoopBound::Util::peelToICmp(latchBranch->getCondition())) {
      return true;
    }
  }

  // Otherwise: find an exiting block whose terminator condition is an ICmp
  // and which dominates the latch (so it constrains the backedge count).
  llvm::SmallVector<llvm::BasicBlock*, 8> exitingBlocks;
  loop->getExitingBlocks(exitingBlocks);

  for (llvm::BasicBlock *exitingBlock : exitingBlocks) {
    auto *branchInst = llvm::dyn_cast<llvm::BranchInst>(exitingBlock->getTerminator());
    if (!branchInst || !branchInst->isConditional())
      continue;

    const llvm::ICmpInst *icmpInst = LoopBound::Util::peelToICmp(branchInst->getCondition());
    if (!icmpInst)
      continue;

    // If this test dominates the latch, it bounds how often we can reach the latch.
    if (dominatorTree.dominates(exitingBlock, latchBlock)) {
      return true;
    }
  }

  return false;
}

static const llvm::LoadInst *getDirectLoadFromRoot(const llvm::Value *value,
                                                   const llvm::Value *root) {
  if (!value || !root)
    return nullptr;

  value = LoopBound::Util::stripCasts(value);

  // Accept add/sub with 0 around the load (common noise)
  if (auto *binOp = llvm::dyn_cast<llvm::BinaryOperator>(value)) {
    auto opcode = binOp->getOpcode();
    if (opcode == llvm::Instruction::Add || opcode == llvm::Instruction::Sub) {
      const llvm::Value *operandLeft = LoopBound::Util::stripCasts(binOp->getOperand(0));
      const llvm::Value *operandRight = LoopBound::Util::stripCasts(binOp->getOperand(1));

      // x + 0 or x - 0
      if (auto *constInt = llvm::dyn_cast<llvm::ConstantInt>(operandRight)) {
        if (constInt->isZero()) {
          value = operandLeft;
        }
      }
    }
  }

  auto *loadInst = llvm::dyn_cast<llvm::LoadInst>(value);
  if (!loadInst)
    return nullptr;

  const llvm::Value *loadedPointer = LoopBound::Util::stripAddr(loadInst->getPointerOperand());
  return (loadedPointer == root) ? loadInst : nullptr;
}

bool loopConditionCannotBeDeduced(LoopBound::LoopParameterDescription description,
                                  llvm::FunctionAnalysisManager *analysisManager,
                                  llvm::DominatorTree &dominatorTree,
                                  llvm::LoopInfo &loopInfo) {
  auto checkExpression = LoopBoundWrapper::findLoopCheckExpr(description,
  analysisManager, loopInfo);
  if (checkExpression.has_value()) {
    auto bound = checkExpression.value().calculateCheck(analysisManager, loopInfo);
    return bound.has_value();
  }
  return true;
}

bool loopInitCannotBeDeduced(LoopBound::LoopParameterDescription description) {
  return !description.init.has_value();
}

bool loopIsCounting(llvm::Loop *loop, llvm::ICmpInst *icmpCondition) {
  if (!loop || !icmpCondition)
    return false;

  auto counterInfo = LoopBoundIDEAnalysis::findCounterFromICMP(icmpCondition, loop);
  if (!counterInfo || counterInfo->Roots.empty()) {
    return false;
  }

  const llvm::Value *counterRoot = LoopBound::Util::stripAddr(counterInfo->Roots[0]);

  // Look for a store to that root inside the loop that we can parse as increment
  bool foundIncrementStore = false;

  for (llvm::BasicBlock *loopBlock : loop->blocks()) {
    for (llvm::Instruction &instruction : *loopBlock) {
      auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(&instruction);
      if (!storeInst)
        continue;

      // must store to the same root object
      const llvm::Value *storeDestination = LoopBound::Util::stripAddr(storeInst->getPointerOperand());
      if (storeDestination != counterRoot)
        continue;

      // can we extract a constant increment/mul/div etc?
      if (LoopBoundIDEAnalysis::extractConstIncFromStore(storeInst, counterRoot).has_value()) {
        foundIncrementStore = true;
        break;
      }
    }
    if (foundIncrementStore)
      break;
  }

  return foundIncrementStore;
}

static bool isMemoryRootWrittenInLoop(const llvm::Value *baseMemory,
                                     llvm::Loop *loop) {
  if (!baseMemory || !loop)
    return false;

  const llvm::Value *normalizedBase = LoopBound::Util::stripAddr(baseMemory);

  for (llvm::BasicBlock *loopBlock : loop->blocks()) {
    for (llvm::Instruction &instruction : *loopBlock) {
      auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(&instruction);
      if (!storeInst)
        continue;

      const llvm::Value *storeDestination =
          LoopBound::Util::stripAddr(storeInst->getPointerOperand());
      if (storeDestination == normalizedBase) {
        return true;
      }
    }
  }
  return false;
}

bool loopIsDependentNested(const LoopParameterDescription &description,
                           llvm::LoopInfo &loopInfo) {
  if (!description.loop || !description.icmp || !description.counterRoot) {
    return false;
  }

  llvm::Loop *currentLoop = description.loop;
  llvm::Loop *parentLoop = currentLoop->getParentLoop();
  if (!parentLoop) {
    return false;
  }

  const llvm::Value *counterRoot =
      LoopBound::Util::stripAddr(description.counterRoot);

  const llvm::Value *operandZero =
      LoopBound::Util::stripCasts(description.icmp->getOperand(0));
  const llvm::Value *operandOne =
      LoopBound::Util::stripCasts(description.icmp->getOperand(1));

  auto normalizaValue = [](const llvm::Value *val) -> const llvm::Value * {
    if (!val)
      return nullptr;
    return LoopBound::Util::stripAddr(
             LoopBound::Util::stripCasts(val));
  };

  auto exprZero = LoopBoundWrapper::peelBasePlusConst(operandZero);
  auto exprOne = LoopBoundWrapper::peelBasePlusConst(operandOne);

  // Check if expression is counter-related
  auto isCounterExpression = [&](const std::optional<CheckExpr> &expression) -> bool {
    if (!expression || expression->isConstant)
      return false;
    return normalizaValue(expression->Base) == counterRoot;
  };

  const bool operandZeroIsCounter = isCounterExpression(exprZero);
  const bool operandOneIsCounter = isCounterExpression(exprOne);

  const llvm::Value *boundValue = nullptr;

  if (operandZeroIsCounter && !operandOneIsCounter) {
    boundValue = operandOne;
  } else if (!operandZeroIsCounter && operandOneIsCounter) {
    boundValue = operandZero;
  } else {
    // ambiguous or malformed -> don't classify as dependent nested
    return false;
  }

  // Parse bound expression
  auto boundExpression = LoopBoundWrapper::peelBasePlusConst(boundValue);
  if (!boundExpression || boundExpression->isConstant || !boundExpression->Base) {
    return false;
  }

  const llvm::Value *boundBase = normalizaValue(boundExpression->Base);

  // Check all enclosing loops
  // Check each parent
  for (llvm::Loop *checkLoop = parentLoop; checkLoop != nullptr; checkLoop = checkLoop->getParentLoop()) {
    if (isMemoryRootWrittenInLoop(boundBase, checkLoop)) {
      return true;
    }
  }

  return false;
}

LoopBound::LoopType determineLoopType(LoopBound::LoopParameterDescription description,
llvm::FunctionAnalysisManager *analysisManager) {
  auto *preheaderBlock = description.loop->getLoopPreheader();
  auto *parentFunction = preheaderBlock->getParent();

  // Get dominator tree analysis
  auto &domTree = analysisManager->getResult<llvm::DominatorTreeAnalysis>(*parentFunction);
  // Get loop analysis
  auto &loopInformation = analysisManager->getResult<llvm::LoopAnalysis>(*parentFunction);

  // Check if loop is non uniform
  if (!loopIsUniform(description.loop, domTree)) {
    return LoopType::MALFORMED_LOOP;
  }

  if (loopIsDependentNested(description, loopInformation)) {
    return LoopType::NESTED_LOOP;
  }

  if (!loopConditionCannotBeDeduced(description, analysisManager, domTree, loopInformation)) {
    // Check if condition deducible
    return LoopType::SYMBOLIC_BOUND_LOOP;
  }

  if (loopInitCannotBeDeduced(description)) {
    return LoopType::SYMBOLIC_BOUND_LOOP;
  }

  if (!loopIsCounting(description.loop, description.icmp)) {
    return LoopType::NON_COUNTING_LOOP;
  }

  return LoopType::NORMAL_LOOP;
}

std::string LoopTypeToString(LoopBound::LoopType loopType) {
  switch (loopType) {
    case LoopBound::LoopType::NORMAL_LOOP:
      return "NORMAL_LOOP";
    case LoopType::MALFORMED_LOOP:
      return "MALFORMED_LOOP";
    case LoopType::SYMBOLIC_BOUND_LOOP:
      return "SYMBOLIC_BOUND_LOOP";
    case LoopType::NON_COUNTING_LOOP:
      return "NON_COUNTING_LOOP";
    case LoopType::NESTED_LOOP:
      return "NESTED_LOOP";
    case LoopType::UNKNOWN_LOOP:
      return "UNKNOWN_LOOP";
    default:
      return "";
  }
}

LoopType strToLoopType(const std::string &loopTypeString) {
  if (loopTypeString == "NORMAL_LOOP") {
    return LoopType::NORMAL_LOOP;
  }
  if (loopTypeString == "MALFORMED_LOOP") {
    return LoopType::MALFORMED_LOOP;
  }
  if (loopTypeString == "SYMBOLIC_BOUND_LOOP") {
    return LoopType::SYMBOLIC_BOUND_LOOP;
  }
  if (loopTypeString == "NON_COUNTING_LOOP") {
    return LoopType::NON_COUNTING_LOOP;
  }
  if (loopTypeString == "NESTED_LOOP") {
    return LoopType::NESTED_LOOP;
  }
  if (loopTypeString == "UNKNOWN_LOOP") {
    return LoopType::UNKNOWN_LOOP;
  }
  return LoopType::UNKNOWN_LOOP;
}

}  // namespace LoopBound::Util
