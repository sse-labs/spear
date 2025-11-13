
#include "LoopTree.h"

#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/IR/IntrinsicInst.h>


LoopTree::LoopTree(llvm::Loop *main, const std::vector<llvm::Loop *>& subloops, LLVMHandler *handler, llvm::ScalarEvolution *scalarEvolution, llvm::LoopInfo *li){
    this->mainloop = main;
    this->handler = handler;

    this->LI = li;

    //Iterate over the given Subloops
    for (auto subLoop : subloops) {
        //For each subloop create a new LoopTree with parameters regarding this subloop
        auto *subLoopTree = new LoopTree(subLoop, subLoop->getSubLoops(), this->handler, scalarEvolution, li);

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

// Recursively extract all source Values (variables) from a SCEV expression
void printSourceVariablesFromSCEV(const llvm::SCEV *Expr,
                                  llvm::ScalarEvolution &SE,
                                  llvm::PHINode *IndVar) {
    if (auto *Unknown = llvm::dyn_cast<llvm::SCEVUnknown>(Expr)) {
        const llvm::Value *V = Unknown->getValue();
        // Skip the induction variable itself
        if (V != IndVar) {
            llvm::errs() << "    Loop bound variable: ";
            if (V->hasName())
                llvm::errs() << V->getName() << "\n";
            else
                llvm::errs() << "<unnamed>\n";
        }
        return;
    }

    // Recursively explore operands for composite SCEVs
    if (auto *NA = llvm::dyn_cast<llvm::SCEVNAryExpr>(Expr)) {
        for (const llvm::SCEV *Op : NA->operands())
            printSourceVariablesFromSCEV(Op, SE, IndVar);
    } else if (auto *Cast = llvm::dyn_cast<llvm::SCEVCastExpr>(Expr)) {
        printSourceVariablesFromSCEV(Cast->getOperand(), SE, IndVar);
    } else if (auto *UDiv = llvm::dyn_cast<llvm::SCEVUDivExpr>(Expr)) {
        printSourceVariablesFromSCEV(UDiv->getLHS(), SE, IndVar);
        printSourceVariablesFromSCEV(UDiv->getRHS(), SE, IndVar);
    } else if (auto *AddRec = llvm::dyn_cast<llvm::SCEVAddRecExpr>(Expr)) {
        // Recurse on start and step using SE
        printSourceVariablesFromSCEV(AddRec->getStart(), SE, IndVar);
        printSourceVariablesFromSCEV(AddRec->getStepRecurrence(SE), SE, IndVar);
    }
}

long LoopTree::getLoopUpperBound(llvm::Loop *loop, llvm::ScalarEvolution *scalarEvolution) const{
    //Get the Latch instruction responsible for containing the compare instruction
    //Init the boundValue with a default value if we are not comparing with a natural number
    long boundValue = this->handler->valueIfIndeterminable;
    auto loopBound = loop->getBounds(*scalarEvolution);
    //Assume the number to compare with is the second argument of the instruction

    llvm::errs() << "Loop " << loop->getName() << "\n";

    // Get the induction variable using SCEV
    llvm::PHINode *IndVar = loop->getInductionVariable(*scalarEvolution);
    if (!IndVar) {
        return boundValue;
    }


    llvm::errs() << "  Induction variable: " << IndVar->getName() << "\n";

    // Get the backedge taken count
    const llvm::SCEV *ExitCount = scalarEvolution->getBackedgeTakenCount(L);

    if (!llvm::isa<llvm::SCEVCouldNotCompute>(ExitCount)) {
        if (auto *AR = llvm::dyn_cast<llvm::SCEVAddRecExpr>(ExitCount)) {
            const llvm::SCEV *Start = AR->getStart();
            const llvm::SCEV *Step  = AR->getStepRecurrence(*scalarEvolution);
            llvm::errs() << "  Start value: " << *Start << "\n";
            llvm::errs() << "  Step value: " << *Step << "\n";
        } else {
            llvm::errs() << "  Exit count: " << *ExitCount << "\n";
        }
    }

    // Approximate loop bound
    const llvm::SCEV *BECount = scalarEvolution->getExitCount(L, L->getLoopLatch());
    if (!llvm::isa<llvm::SCEVCouldNotCompute>(BECount)) {
        const llvm::SCEV *Bound = scalarEvolution->getAddExpr(
            scalarEvolution->getUnknown(IndVar),
            scalarEvolution->getAddExpr(BECount, scalarEvolution->getOne(IndVar->getType()))
        );
        llvm::errs() << "  Approximated loop bound: " << *Bound << "\n";

        printSourceVariablesFromSCEV(Bound, *scalarEvolution, IndVar);
    }

    /*for (auto *L : *this->LI) {
        llvm::PHINode *IndVar = L->getCanonicalInductionVariable();
        llvm::Value *Candidate = nullptr;

        if (IndVar)
            Candidate = IndVar;
        else {
            // Fallback: look for affine PHI nodes manually
            for (auto &I : *L->getHeader()) {
                if (auto *PN = llvm::dyn_cast<llvm::PHINode>(&I)) {
                    const llvm::SCEV *S = scalarEvolution->getSCEV(PN);
                    if (const llvm::SCEVAddRecExpr *AR =
                            llvm::dyn_cast<llvm::SCEVAddRecExpr>(S)) {
                        if (AR->isAffine()) {
                            Candidate = PN;
                            break;
                        }
                            }
                }
            }
        }

        if (!Candidate)
            continue;

        // Print IR-level name
        llvm::errs() << "Loop induction variable IR name: "
                     << Candidate->getName() << "\n";

        // Try to find corresponding source-level variable via debug info
        auto *F = L->getHeader()->getParent();
        for (auto &BB : *F) {
            for (auto &I : BB) {
                if (auto *DbgVal = llvm::dyn_cast<llvm::DbgValueInst>(&I)) {
                    if (DbgVal->getValue() == Candidate) {
                        if (auto *Var = DbgVal->getVariable()) {
                            llvm::errs() << " → Source variable name: "
                                         << Var->getName() << "\n";
                        }
                    }
                } else if (auto *DbgDecl = llvm::dyn_cast<llvm::DbgDeclareInst>(&I)) {
                    if (DbgDecl->getAddress() == Candidate) {
                        if (auto *Var = DbgDecl->getVariable()) {
                            llvm::errs() << " → Source variable name: "
                                         << Var->getName() << "\n";
                        }
                    }
                }
            }
        }


        llvm::BasicBlock *Header = L->getHeader();

        for (auto &I : *Header) {
            if (auto *BI = llvm::BranchInst::dyn_cast(&I)) {
                if (BI->isConditional()) {
                    if (auto *ICmp = llvm::dyn_cast<llvm::ICmpInst>(BI->getCondition())) {
                        llvm::Value *LHS = ICmp->getOperand(0);
                        llvm::Value *RHS = ICmp->getOperand(1);

                        llvm::ConstantInt *Bound = llvm::dyn_cast<llvm::ConstantInt>(RHS);
                        if (!Bound)
                            Bound = llvm::dyn_cast<llvm::ConstantInt>(LHS);

                        if (Bound) {
                            llvm::errs() << "Loop bound: " << *Bound << "\n";
                            llvm::errs() << "Induction variable: "
                                         << (Bound == LHS ? *RHS : *LHS) << "\n";
                        }
                    }
                }
            }
        }
    }*/

    if(loopBound.has_value()){
        auto &endValueObj = loopBound->getFinalIVValue();
        auto &startValueObj = loopBound->getInitialIVValue();
        auto stepValueObj = loopBound->getStepValue();
        auto direction = loopBound->getDirection();

        long endValue;
        long startValue;
        long stepValue;

        auto* constantIntEnd = llvm::dyn_cast<llvm::ConstantInt>(&endValueObj);
        auto* constantIntStart = llvm::dyn_cast<llvm::ConstantInt>(&startValueObj);
        auto* constantIntStep = llvm::dyn_cast<llvm::ConstantInt>(stepValueObj);

        if (constantIntEnd && constantIntStart && constantIntStep ) {
            if (constantIntEnd->getBitWidth() <= 32 && constantIntStart->getBitWidth() <= 32 && constantIntStep->getBitWidth() <= 32) {
                endValue = constantIntEnd->getSExtValue();
                startValue = constantIntStart->getSExtValue();
                stepValue = constantIntStep->getSExtValue();

                if(direction == llvm::Loop::LoopBounds::Direction::Decreasing){
                    double numberOfRepetitions = ceil((double)startValue / (double) std::abs(stepValue) - (double)endValue);
                    boundValue = (long) numberOfRepetitions;
                }else if(direction == llvm::Loop::LoopBounds::Direction::Increasing){
                    double numberOfRepetitions = ceil((double)endValue / (double) std::abs(stepValue) - (double)startValue);
                    boundValue = (long) numberOfRepetitions;
                }
            }
        }
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