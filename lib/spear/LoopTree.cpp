
#include "LoopTree.h"

#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/IR/IntrinsicInst.h>


LoopTree::LoopTree(llvm::Loop *main, const std::vector<llvm::Loop *>& subloops, LLVMHandler *handler, llvm::ScalarEvolution *scalarEvolution, std::map<std::string, std::map<std::string, std::pair<const llvm::Value*, psr::IDELinearConstantAnalysisDomain::l_t>>> *variablemapping){
    this->mainloop = main;
    this->handler = handler;

    this->boundvars = {};

    this->findBoundVars(scalarEvolution);

    this->_variablemapping = variablemapping;

    for (auto bv : this->boundvars) {
        llvm::errs() << "\t\tBound variable: " << *bv << "\n";
    }

    //Iterate over the given Subloops
    for (auto subLoop : subloops) {
        //For each subloop create a new LoopTree with parameters regarding this subloop
        auto *subLoopTree = new LoopTree(subLoop, subLoop->getSubLoops(), this->handler, scalarEvolution, variablemapping);

        //Add the subtree to the vector of subgraphs
        this->subTrees.push_back(subLoopTree);
    }

    //Calculate the basic blocks only contained in this loop, excluding all blocks present in the subgraphs
    std::vector<llvm::BasicBlock *> calcedBlocks = calcBlocks();
    this->blocks.insert(this->blocks.end(), calcedBlocks.begin(), calcedBlocks.end());

    this->iterations=0;
    //Calculate the iterations of this loop
    this->iterations = this->getLoopUpperBound(this->mainloop, scalarEvolution);
}


std::vector<llvm::BasicBlock *> LoopTree::calcBlocks(){
    //All the blocks present in the loop
    std::vector<llvm::BasicBlock *> initBlocks = this->mainloop->getBlocksVector();
    //Vector for storing the combined blocks of the subloops
    std::vector<llvm::BasicBlock *> combined;
    //Vector for string the calculated difference of this loop and its subloops
    std::vector<llvm::BasicBlock *> difference;

    //Test the leafness of the current LoopTree
    if( !this->isLeaf() ){
        //If we are not in a leaf, we have to calculate the blocks of this tree by building the set-difference
        //of all subloops and the initblocks present in this subloop
        for(auto subloop : this->subTrees){
            //Calculate the union of all subloops
            combined.insert(combined.end(), subloop->mainloop->getBlocksVector().begin(), subloop->mainloop->getBlocksVector().end());
        }

        //Iterate over the blocks in this loop. Find the blocks that are not present in the union but in this loop
        for (auto &basicBlock : initBlocks) {
            if(std::find(combined.begin(), combined.end(), basicBlock) == combined.end()){
                difference.insert(difference.end(), basicBlock);
            }
        }

        return difference;
    }else{
        //If we are in a leaf, we can simply return the init blocks, as there are no subloops
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
        auto StepVars = getSourceVariablesFromSCEV(AddRec->getStepRecurrence(SE), SE, IndVar);
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
            scalarEvolution->getAddExpr(BECount, scalarEvolution->getOne(IndVar->getType()))
        );

        auto boundVars = this->getSourceVariablesFromSCEV(Bound, *scalarEvolution, IndVar);
        if (!boundVars.empty()) {
            this->boundvars = boundVars;
        }
    }
}

long LoopTree::calculateIterations(long start, long end, long step, llvm::Loop::LoopBounds::Direction direction) {
    double numberOfRepetitions = -255;

    if(direction == llvm::Loop::LoopBounds::Direction::Decreasing){
        numberOfRepetitions = ceil((double) start / (double) std::abs(end) - (double)end);
    }else if(direction == llvm::Loop::LoopBounds::Direction::Increasing){
        numberOfRepetitions = ceil((double) end / (double) std::abs(step) - (double)start);
    }

    return (long) numberOfRepetitions;

}

long LoopTree::iterationsFromLoopBound(llvm::Optional<llvm::Loop::LoopBounds> *lb, long ev) {
    long boundValue = -1;

    if (lb->has_value()) {
        llvm::Loop::LoopBounds *loopBound = lb->getPointer();

        auto &endValueObj = loopBound->getFinalIVValue();
        auto &startValueObj = loopBound->getInitialIVValue();
        auto stepValueObj = loopBound->getStepValue();
        auto direction = loopBound->getDirection();

        long startValue = -1;
        long stepValue = -1;
        long endValue = -1;

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
    }else {
        if (ev != -1) {
            boundValue = ev;
        }
    }

    return boundValue;
}


long LoopTree::getLoopUpperBound(llvm::Loop *loop,
                                 llvm::ScalarEvolution *scalarEvolution) {
    long boundValue = this->handler->valueIfIndeterminable;

    // --- 1. Get loop bound from ScalarEvolution if available --------------------
    auto loopBound = loop->getBounds(*scalarEvolution);

    // --- 2. Identify the block whose terminator performs the compare -----------
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

    // --- 3. Find the icmp in the chosen block ----------------------------------
    llvm::ICmpInst *icmp = nullptr;
    for (llvm::Instruction &inst : *bb) {
        if (auto *c = llvm::dyn_cast<llvm::ICmpInst>(&inst)) {
            icmp = c;
            break;
        }
    }

    if (!icmp) {
        llvm::errs() << "No compare instruction found in loop control block " << bbName << "\n";
        return boundValue;
    }

    // --- 4. Extract the RHS compare operand (usually the upper bound) -----------
    llvm::Value *rhs = icmp->getOperand(1);

    std::string varName = rhs->hasName()
        ? rhs->getName().str()
        : "<unnamed_" + std::to_string(reinterpret_cast<uintptr_t>(rhs)) + ">";

    // --- 5. Lookup Phasar analysis *by block* to get correct value --------------
    long endValue = this->handler->valueIfIndeterminable;

    try {
        auto &blockMap = this->_variablemapping->at(bbName);

        if (blockMap.count(varName)) {
            auto &entry = blockMap[varName];
            endValue = entry.second.assertGetValue();

            llvm::errs() << "\tPHSR:("
                         << varName << " -> " << endValue
                         << ") in block " << bbName << "\n";
        } else {
            llvm::errs() << "\tNo entry for " << varName
                         << " in block map for " << bbName << "\n";
        }
    } catch (...) {
        llvm::errs() << "\tBlock " << bbName << " not found in variable mapping\n";
    }

    // --- 6. Compute numeric iterations from loopBound + Phasar endValue ---------
    boundValue = iterationsFromLoopBound(&loopBound, endValue);

    if (boundValue != -1) {
        llvm::errs() << "\t\tComputed loop bound = " << boundValue << "\n";
        return boundValue;
    }

    // --- 7. Fallback: use ScalarEvolution trip count ----------------------------
    const llvm::SCEV *tripCount = scalarEvolution->getBackedgeTakenCount(loop);

    if (auto *c = llvm::dyn_cast<llvm::SCEVConstant>(tripCount)) {
        boundValue = c->getValue()->getSExtValue();
        llvm::errs() << "\tTrip count = " << boundValue << "\n";
    } else {
        llvm::errs() << "\tTrip count symbolic = ";
        tripCount->print(llvm::errs());
        llvm::errs() << "\n";
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
    }else{
        for (auto subLoopTree: this->subTrees) {
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
    if(this->isLeaf()){
        std::vector<llvm::BasicBlock *> latches;
        latches.push_back(this->mainloop->getLoopLatch());
        return latches;
    }else{
        std::vector<llvm::BasicBlock *> latches;

        for (auto subTree : this->subTrees) {
            std::vector<llvm::BasicBlock *> subTreeLatches = subTree->getLatches();
            subTreeLatches.push_back(this->mainloop->getLoopLatch());
            for(auto &latch : subTreeLatches){
                if(std::find(latches.begin(), latches.end(), latch) == latches.end()){
                    latches.push_back(latch);
                }
            }
        }

        return latches;
    }
}

LoopTree::~LoopTree(){
    for(auto loopTree : this->subTrees){
        delete loopTree;
    }


}