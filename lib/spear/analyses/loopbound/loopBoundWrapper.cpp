/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <llvm/Analysis/ScalarEvolution.h>
#include <phasar/DataFlow/IfdsIde/Solver/IDESolver.h>
#include <phasar/PhasarLLVM/DB/LLVMProjectIRDB.h>
#include <phasar.h>

#include <utility>
#include <memory>
#include <vector>

#include "analyses/loopbound/LoopBound.h"
#include "analyses/loopbound/loopBoundWrapper.h"
#include "analyses/loopbound/util.h"

LoopBoundWrapper::LoopBoundWrapper(
    std::shared_ptr<psr::HelperAnalyses> helperAnalyses,
    llvm::FunctionAnalysisManager *analysisManager) {

  if (!helperAnalyses) {
    return;  // Missing helper analyses
  }
  this->FAM = analysisManager;  // Store analysis manager

  auto &interproceduralCFG = helperAnalyses->getICFG();  // Interprocedural CFG

  llvm::Module *module = helperAnalyses->getProjectIRDB().getModule();  // Project module
  if (!module) {
    llvm::errs() << "[LB] module not found\n";
    return;  // Abort if module is missing
  }

  this->loops.clear();  // Reset loop cache
  this->loopClassifiers.clear();  // Reset classifier cache

  for (llvm::Function &function : *module) {
    if (function.isDeclaration()) {
      continue;
    }
    if (function.getName().startswith("llvm.")) {
      continue;
    }

    auto &loopInfo = analysisManager->getResult<llvm::LoopAnalysis>(function);  // Loop analysis

    for (llvm::Loop *topLevelLoop : loopInfo.getTopLevelLoops()) {
      collectLoops(topLevelLoop, this->loops);  // Collect nested loops
    }
  }

  LoopBound::LoopBoundIDEAnalysis analysisProblem(
      analysisManager, &helperAnalyses->getProjectIRDB(), &this->loops);  // Build analysis
  auto analysisResult = psr::solveIDEProblem(analysisProblem, interproceduralCFG);  // Solve once
  this->cachedResults = std::make_unique<ResultsTy>(std::move(analysisResult));  // Cache results

  const auto loopDescriptions = analysisProblem.getLoopParameterDescriptions();  // Get loop params

  for (const auto &description : loopDescriptions) {
    if (!description.loop || !description.counterRoot || !description.icmp) {
      continue;  // Skip incomplete descriptions
    }

    llvm::BasicBlock *headerBlock = description.loop->getHeader();
    if (!headerBlock) {
      continue;  // Missing loop header
    }

    llvm::Function *parentFunction = headerBlock->getParent();
    if (!parentFunction) {
      continue;  // Missing parent function
    }

    if (parentFunction->isDeclaration() || parentFunction->getName().startswith("llvm.")) {
      continue;  // Skip unsupported functions
    }

    auto &loopInfo =
        analysisManager->getResult<llvm::LoopAnalysis>(*parentFunction);  // Loop info for parent

    const llvm::Value *counterRoot =
        LoopBound::Util::stripAddr(description.counterRoot);  // Normalize counter root
    if (!counterRoot) {
      continue;  // Skip if counter root is invalid
    }

    const llvm::StoreInst *incrementStore = this->findStoreIncOfLoop(description);  // Find inc store

    auto incrementInterval = queryIntervalAtInstuction(incrementStore, counterRoot);  // Query inc
    auto predicate = description.icmp->getPredicate();  // Extract predicate

    auto checkExpression = findLoopCheckExpr(description, analysisManager, loopInfo);  // Find check
    if (!checkExpression) {
      continue;  // Skip if no check expression
    }

    LoopClassifier newLoopClassifier(
        parentFunction,
        description.loop,
        incrementInterval,
        description.init,
        predicate,
        checkExpression->calculateCheck(analysisManager, loopInfo),
        description.type);

    this->loopClassifiers.push_back(std::move(newLoopClassifier));  // Store classifier
  }

  if (LoopBound::Util::LB_DebugEnabled) {
      printClassifiers();
  }
}

std::optional<int64_t> CheckExpr::calculateCheck(
    llvm::FunctionAnalysisManager *analysisManager, llvm::LoopInfo &loopInfo) {
    if (!this->isConstant && this->BaseLoad) {
        const llvm::Function *currentFunction = this->BaseLoad->getFunction();  // Load owner
        if (currentFunction) {
            auto &dominatorTree = analysisManager->getResult<llvm::DominatorTreeAnalysis>(
            *const_cast<llvm::Function *>(currentFunction));  // Dominator tree

            if (auto constValue =
                LoopBound::Util::tryDeduceConstFromLoad(this->BaseLoad, dominatorTree, loopInfo)) {
                auto combinedValue = *constValue + this->Offset;  // Add offset
                if (MulBy) {
                    return combinedValue * MulBy.value();  // Apply scaling
                }

                if (DivBy) {
                    return combinedValue / DivBy.value();  // Apply division
                }

                return combinedValue;  // Final value
            }
        }
    }

    if (this->isConstant) {
        return this->Offset;  // Constant check value
    }

    return std::nullopt;  // No constant derivation
}

void LoopBoundWrapper::collectLoops(
    llvm::Loop *loop, std::vector<llvm::Loop *> &outputLoops) {
    if (!loop) {
        return;  // Nothing to collect
    }

    outputLoops.push_back(loop);  // Store current loop

    for (llvm::Loop *subLoop : loop->getSubLoops()) {
        collectLoops(subLoop, outputLoops);  // Recurse into subloops
    }
}

bool LoopBoundWrapper::hasCachedValueAt(
    const llvm::Instruction *instruction, const llvm::Value *factValue) const {
    if (!this->cachedResults || !instruction || !factValue) return false;  // Validate inputs

    const llvm::Value *cleanFact = LoopBound::Util::stripAddr(factValue);  // Normalize fact
    if (!cleanFact) return false;  // Invalid fact

    const auto &results = *this->cachedResults;  // Cached results
    const auto &resultsAtInst = results.resultsAt(instruction);  // Results at instruction

    auto foundIt = resultsAtInst.find(cleanFact);
    if (foundIt == resultsAtInst.end()) return false;  // No value cached

    const auto &intervalValue = foundIt->second;  // Extract interval
    return !intervalValue.isBottom() && !intervalValue.isTop() && !intervalValue.isEmpty();
}

std::optional<LoopBound::DeltaInterval>
LoopBoundWrapper::queryIntervalAtInstuction(
    const llvm::Instruction *instruction, const llvm::Value *factValue) {
    if (!instruction || !factValue || !this->cachedResults) {
        return std::nullopt;  // Missing inputs
    }

    const llvm::Value *cleanFact = LoopBound::Util::stripAddr(factValue);  // Normalize fact
    if (!cleanFact) {
        return std::nullopt;  // Invalid fact
    }

    auto intervalValue = this->cachedResults->resultAt(instruction, cleanFact);  // Query result

    if (intervalValue.isBottom() || intervalValue.isTop() || intervalValue.isEmpty()) {
        return std::nullopt;  // Unusable interval
    }
    return intervalValue;  // Return computed interval
}

const llvm::StoreInst *LoopBoundWrapper::findStoreIncOfLoop(
    const LoopBound::LoopParameterDescription &description) {
    if (!description.loop || !description.counterRoot) {
        return nullptr;  // Missing loop or counter
    }

    const llvm::Value *counterRoot =
        LoopBound::Util::stripAddr(description.counterRoot);  // Normalize counter root
    if (!counterRoot) {
        return nullptr;  // Invalid counter root
    }

    for (llvm::BasicBlock *basicBlock : description.loop->blocks()) {
        for (llvm::Instruction &instruction : *basicBlock) {
            auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(&instruction);
            if (!storeInst) {
                continue;  // Not a store
            }

            if (LoopBound::LoopBoundIDEAnalysis::extractConstIncFromStore(
                storeInst, counterRoot).has_value()) {
                return storeInst;  // Found increment store
            }
        }
    }
    return nullptr;  // No increment store found
}

void LoopBoundWrapper::printClassifiers() {
    llvm::errs() << "\nLoop Classifiers:\n";
    for (const auto &classifier : this->loopClassifiers) {
        llvm::errs() << "[LB] ==========================\n";
        llvm::errs() << "[LB] " << "Function: "
                     << classifier.function->getName() << "\n";
        llvm::errs() << "[LB] " << "Name: " << classifier.loop->getName() << "\n";
        llvm::errs() << "[LB] " << "Type: "
                     << LoopBound::Util::LoopTypeToString(classifier.type) << "\n";

        if (classifier.increment) {
            llvm::errs() << "[LB] " << "Inc: "
                         << "[" << classifier.increment->getLowerBound()
                         << ", " << classifier.increment->getUpperBound() << "]" << "\n";
        } else {
            llvm::errs() << "[LB] " << "Inc: " << "[" << "NONE" << "]" << "\n";
        }

        if (classifier.init) {
            llvm::errs() << "[LB] " << "Init: " << classifier.init.value() << "\n";
        } else {
            llvm::errs() << "[LB] " << "Init: " << "NONE" << "\n";
        }

        if (classifier.predicate) {
            llvm::errs() << "[LB] " << "Predicate: "
                         << LoopBound::Util::predicateToSymbol(classifier.predicate) << "\n";
        } else {
            llvm::errs() << "[LB] " << "Predicate: " << "NONE" << "\n";
        }

        if (classifier.check) {
            llvm::errs() << "[LB] " << "Check: " << classifier.check.value() << "\n";
        } else {
            llvm::errs() << "[LB] " << "Check: " << "NONE" << "\n";
        }

        if (classifier.bound) {
            llvm::errs() << "[LB] " << "Bound: "
                         << "[" << classifier.bound->getLowerBound()
                         << ", " << classifier.bound->getUpperBound() << "]"
                         << " (" << classifier.bound->getValueTypeAsStr() << ")" << "\n";
        } else {
            llvm::errs() << "[LB] " << "Bound: " << "UNBOUND" << "\n";
        }

        llvm::errs() << "[LB] ==========================\n";
        llvm::errs() << "\n";
    }
}

std::optional<CheckExpr> LoopBoundWrapper::findLoopCheckExpr(
const LoopBound::LoopParameterDescription &description,
llvm::FunctionAnalysisManager *analysisManager, llvm::LoopInfo &loopInfo) {
    if (!description.loop || !description.icmp) return std::nullopt;  // Missing loop or compare

    const llvm::Value *leftOperand  = description.icmp->getOperand(0);  // Left operand
    const llvm::Value *rightOperand = description.icmp->getOperand(1);  // Right operand

    const llvm::Value *leftRoot =
        LoopBound::Util::getMemRootFromValue(leftOperand);  // Left memory root
    const llvm::Value *rightRoot =
        LoopBound::Util::getMemRootFromValue(rightOperand);  // Right memory root

    const llvm::Value *otherSideValue = nullptr;  // Non-counter operand

    if (leftRoot && leftRoot == description.counterRoot) {
        otherSideValue = rightOperand;  // Counter on left
    } else if (rightRoot && rightRoot == description.counterRoot) {
        otherSideValue = leftOperand;  // Counter on right
    } else {
        return std::nullopt;  // Counter not found
    }

    otherSideValue = LoopBound::Util::stripCasts(otherSideValue);  // Normalize

    if (auto *constantValue = llvm::dyn_cast<llvm::Constant>(otherSideValue)) {
        if (auto *constExpr = llvm::dyn_cast<llvm::ConstantExpr>(constantValue)) {
            if (constExpr->isCast()) {
                if (auto *innerConst = llvm::dyn_cast<llvm::Constant>(constExpr->getOperand(0)))
                    constantValue = innerConst;  // Strip constant cast
            }
        }
        if (auto *constInt = llvm::dyn_cast<llvm::ConstantInt>(constantValue)) {
            return CheckExpr{nullptr, nullptr, constInt->getSExtValue(), true};  // Constant check
        }
    }

    if (auto expression = peelBasePlusConst(otherSideValue)) {
        return *expression;  // Derived check expression
    }

    if (auto *loadInst = llvm::dyn_cast<llvm::LoadInst>(otherSideValue)) {
        if (auto *functionPtr = const_cast<llvm::Function *>(loadInst->getFunction())) {
            auto &dominatorTree =
                analysisManager->getResult<llvm::DominatorTreeAnalysis>(*functionPtr);
            if (auto constValue =
                LoopBound::Util::tryDeduceConstFromLoad(loadInst, dominatorTree, loopInfo)) {
                return CheckExpr{nullptr, nullptr, *constValue, false};  // Fallback constant
            }
        }
    }

    return std::nullopt;  // No expression found
}

std::vector<LoopClassifier> LoopBoundWrapper::getClassifiers() {
    return this->loopClassifiers;  // Return stored classifiers
}

std::optional<CheckExpr> LoopBoundWrapper::peelBasePlusConst(const llvm::Value *value) {
    if (!value) return std::nullopt;  // Null input
    value = LoopBound::Util::stripCasts(value);  // Strip casts

    if (auto *constInt = llvm::dyn_cast<llvm::ConstantInt>(value)) {
        // Constant expression
        return CheckExpr{nullptr, nullptr, constInt->getSExtValue(), true};
    }

    if (auto *loadInst = llvm::dyn_cast<llvm::LoadInst>(value)) {
        const llvm::Value *rootValue = LoopBound::Util::getMemRootFromValue(loadInst);
        if (!rootValue) rootValue = LoopBound::Util::stripAddr(loadInst->getPointerOperand());
        rootValue = LoopBound::Util::stripAddr(rootValue);

        return CheckExpr{rootValue, loadInst, 0, false};  // Base load with zero offset
    }

    if (auto *binaryOp = llvm::dyn_cast<llvm::BinaryOperator>(value)) {
        const auto opcode = binaryOp->getOpcode();  // Operation kind
        const llvm::Value *leftOperand =
            LoopBound::Util::stripCasts(binaryOp->getOperand(0));  // Left operand
        const llvm::Value *rightOperand =
            LoopBound::Util::stripCasts(binaryOp->getOperand(1));  // Right operand

        if (opcode == llvm::Instruction::Add) {
            if (auto *rightConst = llvm::dyn_cast<llvm::ConstantInt>(rightOperand)) {
                if (auto expression = peelBasePlusConst(leftOperand)) {
                    expression->Offset += rightConst->getSExtValue();  // Add right offset
                    return expression;
                }
            }
            if (auto *leftConst = llvm::dyn_cast<llvm::ConstantInt>(leftOperand)) {
                if (auto expression = peelBasePlusConst(rightOperand)) {
                    expression->Offset += leftConst->getSExtValue();  // Add left offset
                    return expression;
                }
            }
            return std::nullopt;  // Not representable
        }

        if (opcode == llvm::Instruction::Sub) {
            if (auto *rightConst = llvm::dyn_cast<llvm::ConstantInt>(rightOperand)) {
                if (auto expression = peelBasePlusConst(leftOperand)) {
                    expression->Offset -= rightConst->getSExtValue();  // Subtract offset
                    return expression;
                }
            }
            return std::nullopt;  // Not representable
        }

        if (opcode == llvm::Instruction::Mul) {
            if (auto *rightConst = llvm::dyn_cast<llvm::ConstantInt>(rightOperand)) {
                const int64_t scaleValue = rightConst->getSExtValue();
                if (auto expression = peelBasePlusConst(leftOperand)) {
                    if (expression->DivBy.has_value()) return std::nullopt;  // Disallow div then mul

                    expression->MulBy =
                        expression->MulBy.has_value() ? (*expression->MulBy * scaleValue)
                                                      : scaleValue;
                    expression->Offset *= scaleValue;  // Scale offset
                    return expression;
                }
            }
            if (auto *leftConst = llvm::dyn_cast<llvm::ConstantInt>(leftOperand)) {
                const int64_t scaleValue = leftConst->getSExtValue();
                if (auto expression = peelBasePlusConst(rightOperand)) {
                    if (expression->DivBy.has_value()) return std::nullopt;  // Disallow div then mul

                    expression->MulBy =
                        expression->MulBy.has_value() ? (*expression->MulBy * scaleValue)
                                                      : scaleValue;
                    expression->Offset *= scaleValue;  // Scale offset
                    return expression;
                }
            }
            return std::nullopt;  // Not representable
        }

        if (opcode == llvm::Instruction::SDiv || opcode == llvm::Instruction::UDiv) {
            auto *rightConst = llvm::dyn_cast<llvm::ConstantInt>(rightOperand);
            if (!rightConst) return std::nullopt;  // Division requires constant

            const int64_t divisorValue = rightConst->getSExtValue();
            if (divisorValue == 0) return std::nullopt;  // Division by zero

            if (auto expression = peelBasePlusConst(leftOperand)) {
                if (expression->MulBy.has_value()) return std::nullopt;  // Disallow mul then div

                expression->DivBy =
                    expression->DivBy.has_value() ? (*expression->DivBy * divisorValue)
                                                  : divisorValue;
                return expression;  // Store divisor
            }
            return std::nullopt;  // Not representable
        }
        return std::nullopt;  // Unsupported opcode
    }
    return std::nullopt;  // Unsupported expression
}
