/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <phasar.h>

#include "analyses/loopbound/loopBoundWrapper.h"

#include <phasar/DataFlow/IfdsIde/Solver/IDESolver.h>
#include <phasar/PhasarLLVM/DB/LLVMProjectIRDB.h>

#include "analyses/loopbound/LoopBound.h"
#include "analyses/loopbound/util.h"

LoopBoundWrapper::LoopBoundWrapper(std::unique_ptr<psr::HelperAnalyses> helperAnalyses, llvm::FunctionAnalysisManager *FAM) {
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
    this->loops.reserve(LoopInfo.getTopLevelLoopsVector().size());
    for (llvm::Loop *L : LoopInfo) {
        if (L && !L->getParentLoop()) {
            this->loops.push_back(L);
        }
    }

    // Create the analysis problem and solve it
    LoopBound::LoopBoundIDEAnalysis Problem(&helperAnalyses->getProjectIRDB(), &this->loops);
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

        // Search the blocks of our loop for the storage that saves the increments
        // to our loop counter. At this instruction our Phasar analysis will save the
        // increment interval.
        const llvm::StoreInst *IncStore = this->findStoreIncOfLoop(description);

        auto increment = queryIntervalAtInstuction(IncStore, Root);
        auto predicate = description.icmp->getPredicate();
        auto checkval = findLoopCheckVal(description);

        LoopClassifier newLoopClassifier(
            description.loop,
            increment,
            description.init,
            predicate,
            checkval
        );

        this->loopClassifiers.push_back(newLoopClassifier);
    }

    printClassifiers();
}

bool LoopBoundWrapper::hasCachedValueAt(const llvm::Instruction *I,
                      const llvm::Value *Fact) const {
    if (!this->cachedResults) return false;

    const auto &R  = *this->cachedResults;
    const auto &At = R.resultsAt(I);            // typically: map<Value*, DeltaInterval> (or similar)

    auto It = At.find(Fact);
    if (It == At.end()) return false;

    const auto &V = It->second;

    // Define “has a value” as: not unreachable, not unknown, not empty (adjust if needed)
    return !V.isBottom() && !V.isTop() && !V.isEmpty();
}

std::optional<LoopBound::DeltaInterval> LoopBoundWrapper::queryIntervalAtInstuction(const llvm::Instruction *inst, const llvm::Value *fact) {
    // Check validity of parameters
    if (!inst || !fact) {
        return std::nullopt;
    }

    if (!this->cachedResults) {
        return std::nullopt;
    }

    // Find the true address of the fact by stripping all pointer chains
    const llvm::Value *cleanedAddrOfFact = LoopBound::Util::stripAddr(fact);
    if (!cleanedAddrOfFact) {
        return std::nullopt;
    }

    // Check if we have a value for the given fact in our results
    if (hasCachedValueAt(inst, fact)) {
        // Query the fact from the results
        const auto &Map = this->cachedResults->resultsAt(inst);
        auto resultIterator = Map.find(cleanedAddrOfFact);

        if (resultIterator == Map.end()) {
            return std::nullopt;
        }
        return resultIterator->second;
    } else {
        return this->cachedResults->resultAt(inst, cleanedAddrOfFact);
    }
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

            // If we find a valid store, try to extract the increment
            // If an increment can be deduced, we have at least one store that increments
            // our counter variable, hence we have found a valid store that we can return
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
            llvm::errs() << "[LB] " << "Inc: " << "[" << classifier.increment->getLowerBound() << ", " << classifier.increment->getUpperBound() << "]" << "\n";
        } else {
            llvm::errs() << "[LB] " << "Inc: " << "[" << "NONE" << "]" << "\n";
        }

        if (classifier.init) {
            llvm::errs() << "[LB] " << "Init: " << classifier.init << "\n";
        } else {
            llvm::errs() << "[LB] " << "Init: " << "NONE" << "\n";
        }

        if (classifier.predicate) {
            llvm::errs() << "[LB] " << "Predicate: " << LoopBound::Util::predicateToSymbol(classifier.predicate) << "\n";
        } else {
            llvm::errs() << "[LB] " << "Predicate: " << "NONE" << "\n";
        }

        if (classifier.check) {
            llvm::errs() << "[LB] " << "Check: " << classifier.check << "\n";
        } else {
            llvm::errs() << "[LB] " << "Check: " << "NONE" << "\n";
        }

        if (classifier.check) {
            llvm::errs() << "[LB] " << "Bound: " << "[" << classifier.bound->getLowerBound() << ", " << classifier.bound->getUpperBound() << "]" "\n";
        } else {
            llvm::errs() << "[LB] " << "Bound: " << "UNBOUND" << "\n";
        }

        llvm::errs() << "[LB] ==========================\n";
        llvm::errs() << "\n";
    }
}

std::optional<int64_t>
LoopBoundWrapper::findLoopCheckVal(const LoopBound::LoopParameterDescription &description) {
    if (!description.loop || !description.icmp) {
        return std::nullopt;
    }

    const llvm::Value *A  = description.icmp->getOperand(0);
    const llvm::Value *Bv = description.icmp->getOperand(1);

    const llvm::Value *CA = LoopBound::Util::getMemRootFromValue(A);
    const llvm::Value *CB = LoopBound::Util::getMemRootFromValue(Bv);

    const llvm::Value *CounterSide = nullptr;
    const llvm::Value *OtherSide   = nullptr;

    if (CA && CA == description.counterRoot) {
        CounterSide = A;
        OtherSide   = Bv;
    } else if (CB && CB == description.counterRoot) {
        CounterSide = Bv;
        OtherSide   = A;
    } else {
        return std::nullopt;
    }

    const llvm::Value *StrippedOther = OtherSide;
    while (auto *Cast = llvm::dyn_cast<llvm::CastInst>(StrippedOther)) {
        StrippedOther = Cast->getOperand(0);
    }

    if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(StrippedOther)) {
        return static_cast<int64_t>(CI->getSExtValue());
    }

    if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(StrippedOther)) {
        if (llvm::Function *Fn = const_cast<llvm::Function *>(LI->getFunction())) {
            auto &DT = this->FAM->getResult<llvm::DominatorTreeAnalysis>(*Fn);
            if (auto Cst = LoopBound::Util::tryDeduceConstFromLoad(LI, DT)) {
                return Cst;
            }
        }
    }

    return std::nullopt;
}

std::vector<LoopClassifier> LoopBoundWrapper::getClassifiers() {
    return this->loopClassifiers;
}