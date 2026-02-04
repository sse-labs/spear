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
std::unique_ptr<psr::HelperAnalyses> helperAnalyses, llvm::FunctionAnalysisManager *FAM) {
    if (!helperAnalyses) {
        return;
    }

    this->FAM = FAM;

    // Get the iterprocedual control flow graph from phasar
    auto &ICFG = helperAnalyses->getICFG();

    // Check if main is even present in the program
    llvm::Function *Main = helperAnalyses->getProjectIRDB().getFunctionDefinition("main");
    if (!Main) {
        llvm::errs() << "[LB] main not found\n";
        return;
    }

    // Get all the loops and safe them in our internal field
    auto &LoopInfo = FAM->getResult<llvm::LoopAnalysis>(*Main);
    auto &ScalarEvolution = FAM->getResult<llvm::ScalarEvolutionAnalysis>(*Main);

    this->loops.clear();
    this->loops.reserve(LoopInfo.getLoopsInPreorder().size());

    for (llvm::Loop *TopLevelLoop : LoopInfo.getTopLevelLoops()) {
        collectLoops(TopLevelLoop, this->loops);
    }

    // Create the analysis problem and solve it
    LoopBound::LoopBoundIDEAnalysis Problem(FAM, &helperAnalyses->getProjectIRDB(), &this->loops);
    auto Result = psr::solveIDEProblem(Problem, ICFG);
    this->cachedResults = std::make_unique<ResultsTy>(std::move(Result));

    // Query the generated loop descriptions from our analysis
    const auto LoopDescs = Problem.getLoopParameterDescriptions();

    // Iterate over them and create our loop classifiers
    for (const auto &description : LoopDescs) {
        // Check validity of the loop description
        if (!description.loop || !description.counterRoot) {
            continue;
        }

        // Additionally check if the loop corresponds to the main function
        const llvm::Function *mainFunctionCandidate = description.loop->getHeader()->getParent();
        if (!mainFunctionCandidate || mainFunctionCandidate->getName() != "main") {
            continue;
        }

        // Query the address of the loop counter
        const llvm::Value *Root = LoopBound::Util::stripAddr(description.counterRoot);
        if (!Root) {
            continue;
        }

        const llvm::StoreInst *IncStore = this->findStoreIncOfLoop(description);

        auto increment = queryIntervalAtInstuction(IncStore, Root);
        auto predicate = description.icmp->getPredicate();
        auto checkval = findLoopCheckExpr(description, LoopInfo);

        LoopClassifier newLoopClassifier(
            description.loop,
            increment,
            description.init,
            predicate,
            checkval->calculateCheck(FAM, LoopInfo));

        this->loopClassifiers.push_back(newLoopClassifier);
    }

    printClassifiers();
}

std::optional<int64_t> CheckExpr::calculateCheck(llvm::FunctionAnalysisManager *FAM, llvm::LoopInfo &LIInfo) {
    if (!this->isConstant && this->BaseLoad) {
        const llvm::Function *CF = this->BaseLoad->getFunction();
        if (CF) {
            auto &DT = FAM->getResult<llvm::DominatorTreeAnalysis>(
            *const_cast<llvm::Function *>(CF));

            if (auto C = LoopBound::Util::tryDeduceConstFromLoad(this->BaseLoad, DT, LIInfo)) {
                auto tmp = *C + this->Offset;
                if (MulBy) {
                    return tmp * MulBy.value();
                }

                if (DivBy) {
                    return tmp / DivBy.value();
                }

                return tmp;
            }
        }
    }

    // Handle the simple case that check is a constant i.e. for(int i = 9; i > 0; i--)
    if (this->isConstant) {
        return this->Offset;
    }

    return std::nullopt;
}

void LoopBoundWrapper::collectLoops(llvm::Loop *L, std::vector<llvm::Loop *> &Out) {
    if (!L) {
        return;
    }

    Out.push_back(L);

    for (llvm::Loop *SubLoop : L->getSubLoops()) {
        collectLoops(SubLoop, Out);
    }
}

bool LoopBoundWrapper::hasCachedValueAt(const llvm::Instruction *I,
                                       const llvm::Value *Fact) const {
    if (!this->cachedResults || !I || !Fact) return false;

    const llvm::Value *CleanFact = LoopBound::Util::stripAddr(Fact);
    if (!CleanFact) return false;

    const auto &R  = *this->cachedResults;
    const auto &At = R.resultsAt(I);

    auto It = At.find(CleanFact);
    if (It == At.end()) return false;

    const auto &V = It->second;
    return !V.isBottom() && !V.isTop() && !V.isEmpty();
}

std::optional<LoopBound::DeltaInterval>
LoopBoundWrapper::queryIntervalAtInstuction(const llvm::Instruction *Inst,
                                            const llvm::Value *Fact) {
    if (!Inst || !Fact || !this->cachedResults) {
        return std::nullopt;
    }

    const llvm::Value *CleanFact = LoopBound::Util::stripAddr(Fact);
    if (!CleanFact) {
        return std::nullopt;
    }

    auto V = this->cachedResults->resultAt(Inst, CleanFact);

    if (V.isBottom() || V.isTop() || V.isEmpty()) {
        return std::nullopt;
    }
    return V;
}

const llvm::StoreInst *LoopBoundWrapper::findStoreIncOfLoop(const LoopBound::LoopParameterDescription &description) {
    // Check whether the description is valid and contains the needed information
    if (!description.loop || !description.counterRoot) {
        return nullptr;
    }

    // Query the root of the loop (the counter variable) and strip it of any pointer shenanigans
    const llvm::Value *Root = LoopBound::Util::stripAddr(description.counterRoot);
    if (!Root) {
        return nullptr;
    }

    // Search the loop for any store that saves to the root
    for (llvm::BasicBlock *BB : description.loop->blocks()) {
        for (llvm::Instruction &I : *BB) {
            auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I);
            if (!SI) {
                continue;
            }

            if (LoopBound::LoopBoundIDEAnalysis::extractConstIncFromStore(SI, Root).has_value()) {
                return SI;
            }
        }
    }
    return nullptr;
}

void LoopBoundWrapper::printClassifiers() {
    llvm::errs() << "\nLoop Classifiers:\n";
    for (const auto &classifier : this->loopClassifiers) {
        llvm::errs() << "[LB] ==========================\n";
        llvm::errs() << "[LB] " << "Name: " << classifier.loop->getName() << "\n";

        if (classifier.increment) {
            llvm::errs() << "[LB] " << "Inc: " << "[" << classifier.increment->getLowerBound()
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
            llvm::errs() << "[LB] " << "Bound: " << "[" << classifier.bound->getLowerBound()
            << ", " << classifier.bound->getUpperBound() << "]" "\n";
        } else {
            llvm::errs() << "[LB] " << "Bound: " << "UNBOUND" << "\n";
        }

        llvm::errs() << "[LB] ==========================\n";
        llvm::errs() << "\n";
    }
}

std::optional<CheckExpr> LoopBoundWrapper::findLoopCheckExpr(
const LoopBound::LoopParameterDescription &description, llvm::LoopInfo &LIInfo) {
    if (!description.loop || !description.icmp) return std::nullopt;

    const llvm::Value *A  = description.icmp->getOperand(0);
    const llvm::Value *Bv = description.icmp->getOperand(1);

    const llvm::Value *CA = LoopBound::Util::getMemRootFromValue(A);
    const llvm::Value *CB = LoopBound::Util::getMemRootFromValue(Bv);

    const llvm::Value *OtherSide = nullptr;

    if (CA && CA == description.counterRoot) {
        OtherSide = Bv;
    } else if (CB && CB == description.counterRoot) {
        OtherSide = A;
    } else {
        return std::nullopt;
    }

    OtherSide = LoopBound::Util::stripCasts(OtherSide);

    // Handling of constant checks
    if (auto *C = llvm::dyn_cast<llvm::Constant>(OtherSide)) {
        // Strip constant casts like zext/sext/trunc on constants
        if (auto *SC = llvm::dyn_cast<llvm::ConstantExpr>(C)) {
            if (SC->isCast()) {
                if (auto *OpC = llvm::dyn_cast<llvm::Constant>(SC->getOperand(0)))
                    C = OpC;
            }
        }
        if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(C)) {
            return CheckExpr{nullptr, nullptr, CI->getSExtValue(), true};
        }
    }


    // Try to derive simple offset + constant check
    if (auto E = peelBasePlusConst(OtherSide)) {
        return *E;
    }

    // Fallback if constant check is hidden behind load
    if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(OtherSide)) {
        if (auto *Fn = const_cast<llvm::Function *>(LI->getFunction())) {
            auto &DT = this->FAM->getResult<llvm::DominatorTreeAnalysis>(*Fn);
            if (auto Cst = LoopBound::Util::tryDeduceConstFromLoad(LI, DT, LIInfo)) {
                return CheckExpr{nullptr, nullptr, *Cst, false};
            }
        }
    }

    return std::nullopt;
}

std::vector<LoopClassifier> LoopBoundWrapper::getClassifiers() {
    return this->loopClassifiers;
}

std::optional<CheckExpr> LoopBoundWrapper::peelBasePlusConst(const llvm::Value *V) {
    if (!V) return std::nullopt;
    V = LoopBound::Util::stripCasts(V);

    // Constant-only expression no scalation factor, just an offset
    if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(V)) {
        return CheckExpr{nullptr, nullptr, CI->getSExtValue(), true};
    }

    // Checkexpression with offset zero behind load
    if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(V)) {
        const llvm::Value *Root = LoopBound::Util::getMemRootFromValue(LI);
        if (!Root) Root = LoopBound::Util::stripAddr(LI->getPointerOperand());
        Root = LoopBound::Util::stripAddr(Root);

        return CheckExpr{Root, LI, 0, false};
    }

    // Check is result of calculation with add/sub/mul/div and constant
    if (auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(V)) {
        const auto Op = BO->getOpcode();
        const llvm::Value *L = LoopBound::Util::stripCasts(BO->getOperand(0));
        const llvm::Value *R = LoopBound::Util::stripCasts(BO->getOperand(1));

        if (Op == llvm::Instruction::Add) {
            if (auto *RC = llvm::dyn_cast<llvm::ConstantInt>(R)) {
                if (auto E = peelBasePlusConst(L)) {
                    E->Offset += RC->getSExtValue();
                    return E;
                }
            }
            if (auto *LC = llvm::dyn_cast<llvm::ConstantInt>(L)) {
                if (auto E = peelBasePlusConst(R)) {
                    E->Offset += LC->getSExtValue();
                    return E;
                }
            }
            return std::nullopt;
        }

        if (Op == llvm::Instruction::Sub) {
            if (auto *RC = llvm::dyn_cast<llvm::ConstantInt>(R)) {
                if (auto E = peelBasePlusConst(L)) {
                    E->Offset -= RC->getSExtValue();
                    return E;
                }
            }
            // (C - X) not representable as base+const
            return std::nullopt;
        }

        if (Op == llvm::Instruction::Mul) {
            // only allow multiply by constant
            if (auto *RC = llvm::dyn_cast<llvm::ConstantInt>(R)) {
                const int64_t C = RC->getSExtValue();
                if (auto E = peelBasePlusConst(L)) {
                    // Only allow calculation if no division is currently occurring
                    if (E->DivBy.has_value()) return std::nullopt;

                    E->MulBy = E->MulBy.has_value() ? (*E->MulBy * C) : C;
                    E->Offset *= C;  // (base+offset)*C
                    return E;
                }
            }
            if (auto *LC = llvm::dyn_cast<llvm::ConstantInt>(L)) {
                const int64_t C = LC->getSExtValue();
                if (auto E = peelBasePlusConst(R)) {
                    if (E->DivBy.has_value()) return std::nullopt;

                    E->MulBy = E->MulBy.has_value() ? (*E->MulBy * C) : C;
                    E->Offset *= C;
                    return E;
                }
            }
            return std::nullopt;
        }

        if (Op == llvm::Instruction::SDiv || Op == llvm::Instruction::UDiv) {
            // Only allow division by constant on the RHS: X / C
            auto *RC = llvm::dyn_cast<llvm::ConstantInt>(R);
            if (!RC) return std::nullopt;

            const int64_t C = RC->getSExtValue();
            if (C == 0) return std::nullopt;

            if (auto E = peelBasePlusConst(L)) {
                // Only allow if multiplication is not occurring
                if (E->MulBy.has_value()) return std::nullopt;

                // (base+offset)/C
                E->DivBy = E->DivBy.has_value() ? (*E->DivBy * C) : C;
                return E;
            }
            return std::nullopt;
        }
        return std::nullopt;
    }
    return std::nullopt;
}
