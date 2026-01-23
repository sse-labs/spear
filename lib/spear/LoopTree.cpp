/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "LoopTree.h"

#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/IR/IntrinsicInst.h>

#include <vector>
#include <utility>
#include <string>
#include <map>

#include "analyses/loopbound.h"

LoopTree::LoopTree(
    llvm::Loop *main,
    const std::vector<llvm::Loop *>& subloops,
    LLVMHandler *handler,
    llvm::ScalarEvolution *scalarEvolution) {
    this->mainloop = main;
    this->handler = handler;

    this->boundvars = {};

    this->findBoundVars(scalarEvolution);

    for (auto bv : this->boundvars) {
        llvm::errs() << "\t\tBound variable: " << *bv << "\n";
    }

    // Iterate over the given Subloops
    for (auto subLoop : subloops) {
        // For each subloop create a new LoopTree with parameters regarding this subloop
        auto *subLoopTree = new LoopTree(subLoop,
            subLoop->getSubLoops(),
            this->handler, scalarEvolution);

        // Add the subtree to the vector of subgraphs
        this->subTrees.push_back(subLoopTree);
    }

    // Calculate the basic blocks only contained in this loop, excluding all blocks present in the subgraphs
    std::vector<llvm::BasicBlock *> calcedBlocks = calcBlocks();
    this->blocks.insert(this->blocks.end(), calcedBlocks.begin(), calcedBlocks.end());

    this->iterations = 0;
    // Calculate the iterations of this loop
    this->iterations = this->getLoopUpperBound(this->mainloop, scalarEvolution);
}


std::vector<llvm::BasicBlock *> LoopTree::calcBlocks() {
    // All the blocks present in the loop
    std::vector<llvm::BasicBlock *> initBlocks = this->mainloop->getBlocksVector();
    // Vector for storing the combined blocks of the subloops
    std::vector<llvm::BasicBlock *> combined;
    // Vector for string the calculated difference of this loop and its subloops
    std::vector<llvm::BasicBlock *> difference;

    // Test the leafness of the current LoopTree
    if ( !this->isLeaf() ) {
        // If we are not in a leaf, we have to calculate the blocks of this tree by building the set-difference
        // of all subloops and the initblocks present in this subloop
        for (auto subloop : this->subTrees) {
            // Calculate the union of all subloops
            combined.insert(combined.end(),
                subloop->mainloop->getBlocksVector().begin(),
                subloop->mainloop->getBlocksVector().end());
        }

        // Iterate over the blocks in this loop. Find the blocks that are not present in the union but in this loop
        for (auto &basicBlock : initBlocks) {
            if (std::find(combined.begin(), combined.end(), basicBlock) == combined.end()) {
                difference.insert(difference.end(), basicBlock);
            }
        }

        return difference;
    } else {
        // If we are in a leaf, we can simply return the init blocks, as there are no subloops
        return initBlocks;
    }
}

std::vector<const llvm::Value *> LoopTree::getSourceVariablesFromSCEV(const llvm::SCEV *Expr,
                                                                      llvm::ScalarEvolution &SE,
                                                                      llvm::PHINode *IndVar) const {
    std::vector<const llvm::Value *> Vars;

    if (auto *Unknown = llvm::dyn_cast<llvm::SCEVUnknown>(Expr)) {
        const llvm::Value *V = Unknown->getValue();
        // Skip the induction variable itself
        if (V != IndVar) {
            Vars.push_back(V);
        }
        return Vars;
    }

    // Recursively explore operands for composite SCEVs
    if (auto *NA = llvm::dyn_cast<llvm::SCEVNAryExpr>(Expr)) {
        for (const llvm::SCEV *Op : NA->operands()) {
            auto SubVars = getSourceVariablesFromSCEV(Op, SE, IndVar);
            Vars.insert(Vars.end(), SubVars.begin(), SubVars.end());
        }
    } else if (auto *Cast = llvm::dyn_cast<llvm::SCEVCastExpr>(Expr)) {
        auto SubVars = getSourceVariablesFromSCEV(Cast->getOperand(), SE, IndVar);
        Vars.insert(Vars.end(), SubVars.begin(), SubVars.end());
    } else if (auto *UDiv = llvm::dyn_cast<llvm::SCEVUDivExpr>(Expr)) {
        auto LHSVars = getSourceVariablesFromSCEV(UDiv->getLHS(), SE, IndVar);
        auto RHSVars = getSourceVariablesFromSCEV(UDiv->getRHS(), SE, IndVar);
        Vars.insert(Vars.end(), LHSVars.begin(), LHSVars.end());
        Vars.insert(Vars.end(), RHSVars.begin(), RHSVars.end());
    } else if (auto *AddRec = llvm::dyn_cast<llvm::SCEVAddRecExpr>(Expr)) {
        auto StartVars = getSourceVariablesFromSCEV(AddRec->getStart(), SE, IndVar);
        auto StepVars = getSourceVariablesFromSCEV(
            AddRec->getStepRecurrence(SE),
            SE,
            IndVar);

        Vars.insert(Vars.end(), StartVars.begin(), StartVars.end());
        Vars.insert(Vars.end(), StepVars.begin(), StepVars.end());
    }

    return Vars;
}

void LoopTree::findBoundVars(llvm::ScalarEvolution *scalarEvolution) {
    llvm::errs() << "\tLoop " << this->mainloop->getName() << "\n";
    // Get the induction variable using SCEV
    llvm::PHINode *IndVar = this->mainloop->getInductionVariable(*scalarEvolution);
    if (!IndVar) {
        return;
    }

    // Approximate loop bound
    const llvm::SCEV *BECount = scalarEvolution->getExitCount(this->mainloop, this->mainloop->getLoopLatch());
    if (!llvm::isa<llvm::SCEVCouldNotCompute>(BECount)) {
        const llvm::SCEV *Bound = scalarEvolution->getAddExpr(
            scalarEvolution->getUnknown(IndVar),
            scalarEvolution->getAddExpr(BECount, scalarEvolution->getOne(IndVar->getType())));

        auto boundVars = this->getSourceVariablesFromSCEV(Bound, *scalarEvolution, IndVar);
        // assert(boundVars.size() <= 1);
        if (!boundVars.empty()) {
            this->boundvars = boundVars;
        }
    }
}

uint64_t LoopTree::calculateIterations(uint64_t start,
    uint64_t end,
    uint64_t step,
    llvm::Loop::LoopBounds::Direction direction) {
    double numberOfRepetitions = -255;

    assert(end > 0);
    assert(step > 0);

    if (direction == llvm::Loop::LoopBounds::Direction::Decreasing) {
        numberOfRepetitions = ceil(static_cast<double>(start) / static_cast<double>(end) - static_cast<double>(end));
    } else if (direction == llvm::Loop::LoopBounds::Direction::Increasing) {
        numberOfRepetitions = ceil(static_cast<double>(end) / static_cast<double>(step) - static_cast<double>(start));
    }

    return static_cast<uint64_t>(numberOfRepetitions);
}

uint64_t LoopTree::iterationsFromLoopBound(std::optional<llvm::Loop::LoopBounds> *lb, uint64_t ev) {
    uint64_t boundValue = -1;

    if (lb->has_value()) {
        llvm::Loop::LoopBounds loopBound = lb->value();

        auto &endValueObj = loopBound.getFinalIVValue();
        auto &startValueObj = loopBound.getInitialIVValue();
        auto stepValueObj = loopBound.getStepValue();
        auto direction = loopBound.getDirection();

        uint64_t startValue = -1;
        uint64_t stepValue = -1;
        uint64_t endValue = -1;

        auto* constantIntEnd   = llvm::dyn_cast<llvm::ConstantInt>(&endValueObj);
        auto* constantIntStart = llvm::dyn_cast<llvm::ConstantInt>(&startValueObj);
        auto* constantIntStep  = llvm::dyn_cast<llvm::ConstantInt>(stepValueObj);

        // Must have constant start + step
        // End may be constant OR replaced by ev when end is NOT constant
        if (constantIntStart && constantIntStep && (constantIntEnd || ev != -1)) {
            // Use constant end if valid
            if (constantIntEnd) {
                endValue = constantIntEnd->getSExtValue();
            } else {
                endValue = ev;
            }

            startValue = constantIntStart->getSExtValue();
            stepValue  = constantIntStep->getSExtValue();

            boundValue = this->calculateIterations(startValue, endValue, stepValue, direction);
        }
    } else {
        if (ev != -1) {
            boundValue = ev;
        }
    }

    return boundValue;
}


uint64_t LoopTree::getLoopUpperBound(llvm::Loop *loop,
                                 llvm::ScalarEvolution *scalarEvolution) {
    uint64_t boundValue = this->handler->valueIfIndeterminable;

    // Query the loopbound with scalar evolution
    auto loopBound = loop->getBounds(*scalarEvolution);

    // Find the basic block where we perform the loop check. We have to use the constant derived by phasar in this block
    llvm::BasicBlock *latch = loop->getLoopLatch();
    llvm::BasicBlock *exiting = loop->getExitingBlock();

    llvm::BasicBlock *bb = latch ? latch : exiting;
    if (!bb) {
        llvm::errs() << "Loop has no latch or exiting block\n";
        return boundValue;
    }

    std::string bbName = bb->hasName()
       ? bb->getName().str()
       : "<unnamed_bb_" + std::to_string(reinterpret_cast<uintptr_t>(bb)) + ">";


    std::string varName = "";

    // --- Fallback: use ScalarEvolution trip count ----------------------------
    const llvm::SCEV *tripCount = scalarEvolution->getBackedgeTakenCount(loop);

    if (auto *c = llvm::dyn_cast<llvm::SCEVConstant>(tripCount)) {
        boundValue = c->getValue()->getSExtValue() + 1;
        llvm::errs() << "\t\tTrip count = " << boundValue << "\n";
    } else {
        llvm::errs() << "\t\tTrip count symbolic = ";
        tripCount->print(llvm::errs());
        llvm::errs() << "\n";
        llvm::errs() << "\t\t\t=> Fallback loop bound = " << boundValue << "\n";
        boundValue = this->handler->valueIfIndeterminable;
    }

    return boundValue;
}


bool LoopTree::isLeaf() const {
    return this->subTrees.empty();
}

void LoopTree::printPreOrder() {
    if (this->isLeaf()) {
        llvm::outs() << "-------------------------------------------\n";
        llvm::outs() << this->mainloop->getName() << " (LEAF) " << "i=" << this->iterations << "\n";
        llvm::outs() << "-------------------------------------------\n";
        for (auto basicBlock : this->blocks) {
            basicBlock->print(llvm::outs());
        }
    } else {
        for (auto subLoopTree : this->subTrees) {
            subLoopTree->printPreOrder();
        }
        llvm::outs() << "-------------------------------------------\n";
        llvm::outs() << this->mainloop->getName() << " (NODE) "<< "i=" << this->iterations  << "\n";
        llvm::outs() << "-------------------------------------------\n";
        for (auto basicBlock : this->blocks) {
            basicBlock->print(llvm::outs());
        }
    }
}

std::vector<llvm::BasicBlock *> LoopTree::getLatches() {
    if (this->isLeaf()) {
        std::vector<llvm::BasicBlock *> latches;
        latches.push_back(this->mainloop->getLoopLatch());
        return latches;
    }
    std::vector<llvm::BasicBlock *> latches;

    for (auto subTree : this->subTrees) {
        std::vector<llvm::BasicBlock *> subTreeLatches = subTree->getLatches();
        subTreeLatches.push_back(this->mainloop->getLoopLatch());
        for (auto &latch : subTreeLatches) {
            if (std::find(latches.begin(), latches.end(), latch) == latches.end()) {
                latches.push_back(latch);
            }
        }
    }

    return latches;
}

LoopTree::~LoopTree() {
    for (auto loopTree : this->subTrees) {
        delete loopTree;
    }
}
