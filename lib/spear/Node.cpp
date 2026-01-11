/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <string>
#include <vector>
#include <algorithm>
#include <cfloat>

#include "ProgramGraph.h"
#include "DeMangler.h"
#include "PhasarHandler.h"
#include "PhasarResultRegistry.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"

// Create a Node by setting the parent property with the given ProgramGraph
Node::Node(ProgramGraph *parent, AnalysisStrategy::Strategy strategy) : block(nullptr), energy(0) {
    this->parent = parent;
    this->strategy = strategy;
}

std::string Node::toString() {
    // Init the output-string
    std::string output;

    output.append(block->getName().str());

    // Return the string
    return output;
}

std::string Node::getSourceVarName(llvm::Value *V, llvm::Instruction *Ctx) {
    if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(V)) {
        return std::to_string(CI->getSExtValue());
    }

    if (auto *CF = llvm::dyn_cast<llvm::ConstantFP>(V)) {
        llvm::SmallString<16> Str;
        CF->getValueAPF().toString(Str);
        return Str.str().str();
    }

    llvm::Function *F = Ctx->getFunction();

    for (auto &BB : *F) {
        for (auto &I : BB) {
            if (auto *DbgVal = llvm::dyn_cast<llvm::DbgValueInst>(&I)) {
                if (DbgVal->getValue() == V) {
                    return DbgVal->getVariable()->getName().str();
                }
            }

            if (auto *DbgDecl = llvm::dyn_cast<llvm::DbgDeclareInst>(&I)) {
                if (DbgDecl->getAddress() == V) {
                    return DbgDecl->getVariable()->getName().str();
                }
            }
        }
    }

    if (V->hasName())
        return V->getName().str();

    return "<unknown>";
}

bool Node::evalICMP(llvm::ICmpInst *ICmp, llvm::ConstantInt *left, llvm::ConstantInt *right) {
    using P = llvm::CmpInst::Predicate;


    llvm::APSInt LV(left->getValue());
    llvm::APSInt RV(right->getValue());

    //  Signedness depends on predicate
    bool isSigned = llvm::CmpInst::isSigned(ICmp->getPredicate());
    LV.setIsSigned(isSigned);
    RV.setIsSigned(isSigned);

    switch (ICmp->getPredicate()) {
        case P::ICMP_EQ:  return LV == RV;
        case P::ICMP_NE:  return LV != RV;
        case P::ICMP_SGT: return LV >  RV;
        case P::ICMP_SGE: return LV >= RV;
        case P::ICMP_SLT: return LV <  RV;
        case P::ICMP_SLE: return LV <= RV;
        case P::ICMP_UGT: return LV.ugt(RV);
        case P::ICMP_UGE: return LV.uge(RV);
        case P::ICMP_ULT: return LV.ult(RV);
        case P::ICMP_ULE: return LV.ule(RV);
        default:
            llvm_unreachable("Invalid ICMP predicate");
    }
}

llvm::BasicBlock* Node::getPathName(const llvm::BranchInst *br, bool conditionalresult) {
    llvm::BasicBlock *trueBB  = br->getSuccessor(0);
    llvm::BasicBlock *falseBB = br->getSuccessor(1);

    if (conditionalresult) {
        return trueBB;
    }

    return falseBB;
}

const DomainVal* Node::findDeducedValue(BoundVarMap *resultsAtBlock, std::string varname) {
    for (auto &[key, value] : *resultsAtBlock) {
        if (key == varname) {
            return &value.second;
        }
    }

    return nullptr;
}

// Calculate the energy of this Node. Is capable of dealing with if-conditions
double Node::getNodeEnergy(LLVMHandler *handler) {
    auto context = llvm::LLVMContext();
    // Init the result of the calculation
    double sum = 0.0;

    // Calculate the adjacent nodes of this node
    auto adjacentNodes = this->getAdjacentNodes();

    llvm::BasicBlock *bToTake = nullptr;

    const llvm::Instruction *TI = this->block->getTerminator();
    if (TI) {
        const auto *BI = llvm::dyn_cast<llvm::BranchInst>(TI);
        bool isAnIf = BI && BI->isConditional();
        /*llvm::outs() << "Block " << this->block->getName().str() << " if: " << isAnIf << "\n";
        llvm::outs() << "\t Adjacent => " << adjacentNodes.size() << "\n";*/

        if (isAnIf && adjacentNodes.size() == 2) {
            auto IDEresult = PhasarResultRegistry::get().getResults();
            auto resultsAtBlock = IDEresult[this->block->getName().str()];

            llvm::Value *cond = BI->getCondition();
            llvm::outs() << this->block->getName().str() << ":\n";
            llvm::outs() << "\t Block " << this->block->getName().str() << " if: " << isAnIf << "\n";
            llvm::outs() << "\t Adjacent => " << adjacentNodes.size() << "\n";
            llvm::outs() << "\t Conditional => " << BI->isConditional() << "\n";
            TI->print(llvm::outs());
            llvm::outs() << "\n";
            llvm::outs() << "\t Condition: ";
            cond->print(llvm::outs());
            llvm::outs() << "\n";

            if (auto *ICmp = llvm::dyn_cast<llvm::ICmpInst>(cond)) {
                llvm::Value *lhs = ICmp->getOperand(0);
                llvm::Value *rhs = ICmp->getOperand(1);

                llvm::outs() << "\t\t lhs => ";
                lhs->print(llvm::outs());
                llvm::outs() << "\n";
                // llvm::outs() << "(" << this->getSourceVarName(lhs, ICmp) << ")";
                llvm::outs() << "\n";
                llvm::outs() << "\t\t rhs => ";
                rhs->print(llvm::outs());
                llvm::outs() << "\n";
                // llvm::outs() << "(" << this->getSourceVarName(rhs, ICmp) << ")";

                if (auto *LCI = llvm::dyn_cast<llvm::ConstantInt>(lhs)) {
                    if (auto *RCI = llvm::dyn_cast<llvm::ConstantInt>(rhs)) {
                        llvm::outs() << "\tBoth constants" << "\n";

                        auto res = this->evalICMP(ICmp, LCI, RCI);
                        bToTake = this->getPathName(BI, res);

                        llvm::outs() << "\t\t Branch name to take => " << bToTake->getName().str() << "\n";
                    } else {
                        llvm::outs() << "\tLHS constant, RHS not" << "\n";
                        auto lconstval = llvm::dyn_cast<llvm::ConstantInt>(lhs);
                        std::string rvarname = this->getSourceVarName(rhs, ICmp);
                        auto rval = this->findDeducedValue(&resultsAtBlock, rvarname);

                        lconstval->print(llvm::outs());
                        llvm::outs() << "\n";

                        if (rval != nullptr) {
                            llvm::outs() << rvarname << "(" << *rval << ")" "\n";
                            //  Convert the value to constantint so we can evaluate it...
                            llvm::LLVMContext lc;
                            llvm::ConstantInt *alternativeRCI = llvm::ConstantInt::get(
                                llvm::Type::getInt64Ty(lc),
                                *rval->getValueOrNull(),
                                true);

                            auto res = this->evalICMP(ICmp, lconstval, alternativeRCI);
                            bToTake = this->getPathName(BI, res);

                            llvm::outs() << "\t\t Branch name to take => " << bToTake->getName().str() << "\n";
                        }
                    }
                } else {
                    if (auto *RCI = llvm::dyn_cast<llvm::ConstantInt>(rhs)) {
                        llvm::outs() << "\tLHS not, RHS constant" << "\n";
                        auto rconstval = llvm::dyn_cast<llvm::ConstantInt>(rhs);
                        std::string lvarname = this->getSourceVarName(lhs, ICmp);
                        auto lval = this->findDeducedValue(&resultsAtBlock, lvarname);

                        rconstval->print(llvm::outs());
                        llvm::outs() << "\n";

                        if (lval != nullptr) {
                            llvm::outs() << lvarname << "(" << *lval << ")" "\n";
                            //  Convert the value to constantint so we can evaluate it...
                            llvm::LLVMContext lc;
                            llvm::ConstantInt *alternativeRCI = llvm::ConstantInt::get(
                                llvm::Type::getInt64Ty(lc),
                                *lval->getValueOrNull(),
                                true);

                            auto res = this->evalICMP(ICmp, rconstval, alternativeRCI);
                            bToTake = this->getPathName(BI, res);

                            llvm::outs() << "\t\t Branch name to take => " << bToTake->getName().str() << "\n";
                        }
                    } else {
                        llvm::outs() << "\tBoth variable" << "\n";
                    }
                }
            }

            llvm::outs() << "\n";

            /*auto resultsAtBlock = IDEresult[this->block->getName().str()];
            for (auto& [ent, entval] : resultsAtBlock) {
                llvm::outs() << "\t\t" << ent << " |-> " << entval.second << "\n";
            }*/
        }
    }

    if (bToTake != nullptr) {
        //  We deduced the next block that will be taken
        auto nToLookAt = std::find_if(
            adjacentNodes.begin(),
            adjacentNodes.end(),
            [bToTake](Node *n) {
            return n->block->getName() == bToTake->getName();
        });

        if (nToLookAt != adjacentNodes.end()) {
            Node * actualNode = *nToLookAt;
            llvm::outs() << "\t\t Adjacent node found..." << "\n";
            double actualNodeEnergy = actualNode->getNodeEnergy(handler);
            sum += actualNodeEnergy;
        }
    } else {
        //  We could not find the next path

        // If there are adjacent nodes...
        if (!adjacentNodes.empty()) {
            // Find the smallest energy-value-path of all the adjacent nodes
            // Init the minimal pathvalue
            auto compare = 0.00;

            switch (this->strategy) {
                case AnalysisStrategy::WORSTCASE :
                     compare = DBL_MIN;

                    // Iterate over the adjacent nodes
                    for (auto node : adjacentNodes) {
                        // Calculate the sum of the node
                        if (!node->isExceptionFollowUp()) {
                            double locsum = node->getNodeEnergy(handler);

                            // Set the minimal energy value if the calculated energy is smaller than the current minimum
                            if (locsum > compare) {
                                compare = locsum;
                            }
                        }
                    }

                    sum += compare;
                    break;
                case AnalysisStrategy::BESTCASE :
                    compare = DBL_MAX;

                    // Iterate over the adjacent nodes
                    for (auto node : adjacentNodes) {
                        // Calculate the sum of the node
                        double locsum = node->getNodeEnergy(handler);

                        // Set the minimal energy value if the calculated energy is smaller than the current minimum
                        if (!node->isExceptionFollowUp()) {
                            if (locsum < compare) {
                                compare = locsum;
                            }
                        }
                    }

                    sum += compare;
                    break;
                case AnalysisStrategy::AVERAGECASE :
                    double locsum = 0.00;

                    if (adjacentNodes.size() > 1) {
                        double leftSum = adjacentNodes[0]->getNodeEnergy(handler);
                        double rightSum = adjacentNodes[1]->getNodeEnergy(handler);

                        if (handler->inefficient <= handler->efficient) {
                            if (adjacentNodes[0]->isExceptionFollowUp()) {
                                locsum += rightSum;
                            } else if (adjacentNodes[1]->isExceptionFollowUp()) {
                                locsum += leftSum;
                            } else {
                                locsum += std::max(leftSum, rightSum);
                                handler->inefficient++;
                            }

                        } else {
                            if (adjacentNodes[0]->isExceptionFollowUp()) {
                                locsum += rightSum;
                            } else if (adjacentNodes[1]->isExceptionFollowUp()) {
                                locsum += leftSum;
                            } else {
                                locsum += std::min(leftSum, rightSum);
                                handler->efficient++;
                            }
                        }
                    } else {
                        locsum = adjacentNodes[0]->getNodeEnergy(handler);
                    }
                    sum += locsum;

                    break;
            }
        }
    }

    auto nodename = this->block->getName();

    // Calculate the energy-cost of this node's basic blocks and add it to the sum
    double localEnergy = handler->getNodeSum(this);
    sum = sum + localEnergy;

    this->energy = localEnergy;

    // Return the calculated energy
    return sum;
}

// Calculate the adjacent Nodes
std::vector<Node *> Node::getAdjacentNodes() {
    // Init the vector
    std::vector<Node *> adjacent;

    // Get the edgdes starting at this node from the parent ProgramGraph
    for (auto edge : this->parent->findEdgesStartingAtNode(this)) {
        // Add the end of the edge to the adjacent vector
        adjacent.push_back(edge->end);
    }

    // Return the adjacent nodes vector
    return adjacent;
}

bool Node::isExceptionFollowUp() {
    return this->block->isLandingPad();
}

double Node::getMaxEnergy() {
    double maxEng = 0.0;

    // Calculate the adjacent nodes of this node
    auto adjacentNodes = this->getAdjacentNodes();

    // If there are adjacent nodes...
    if (!adjacentNodes.empty()) {
        // Find the smallest energy-value-path of all the adjacent nodes
        // Init the minimal pathvalue

        for (auto node : adjacentNodes) {
            // Calculate the sum of the node
            if (!node->isExceptionFollowUp()) {
                double locMaxEng = node->getMaxEnergy();

                // Set the minimal energy value if the calculated energy is smaller than the current minimum
                if (locMaxEng > maxEng) {
                    maxEng = locMaxEng;
                }
            }
        }
    }

    // Calculate the energy-cost of this node's basic blocks and add it to the sum
    double localEnergy = this->energy;
    if (localEnergy > maxEng) {
        maxEng = localEnergy;
    }

    // Return the calculated energy
    return maxEng;
}

json Node::getJsonRepresentation() {
    json nodeObject = json::object();
    if (block != nullptr) {
        nodeObject["type"] = NodeType::NODE;
        nodeObject["name"] = block->getName().str();
        nodeObject["energy"] = energy;
        nodeObject["instructions"] = json::array();

        for (int k=0; k < instructions.size(); k++) {
            json instructionObject = json::object();
            InstructionElement Inst = instructions[k];

            instructionObject["opcode"] = Inst.inst->getOpcodeName();
            instructionObject["energy"] = Inst.energy;

            if (llvm::isa<llvm::CallInst>(Inst.inst) || llvm::isa<llvm::CallBrInst>(Inst.inst)) {
                auto calleeInst = llvm::cast<llvm::CallInst>(Inst.inst);
                // Get the called function
                auto *calledFunction = calleeInst->getCalledFunction();
                // Add the function to the list

                if (calledFunction != nullptr) {
                    std::string functionName = DeMangler::demangle(calledFunction->getName().str());
                    instructionObject["calledFunction"] = calledFunction->getName().str();
                } else {
                    auto operand = calleeInst->getCalledOperand();
                    auto val = operand->stripPointerCasts();
                    if (val != nullptr) {
                        auto ref = val->getName();
                        auto refname = ref.str();
                        instructionObject["calledFunction"] = refname;
                    }
                }

            } else if (llvm::isa<llvm::InvokeInst>(Inst.inst)) {
                auto calleeInst = llvm::cast<llvm::InvokeInst>(Inst.inst);
                // Get the called function
                auto *calledFunction = calleeInst->getCalledFunction();
                // Add the function to the list
                if (calledFunction != nullptr) {
                    std::string functionName = DeMangler::demangle(calledFunction->getName().str());
                    instructionObject["calledFunction"] = calledFunction->getName().str();
                } else {
                    auto operand = calleeInst->getCalledOperand();
                    auto val = operand->stripPointerCasts();
                    if (val != nullptr) {
                        auto ref = val->getName();
                        auto refname = ref.str();
                        instructionObject["calledFunction"] = refname;
                    }
                }
            }

            json locationObj = json::object();
            const llvm::DebugLoc &dbl = Inst.inst->getDebugLoc();
            unsigned int line = -1;
            unsigned int col  = -1;
            std::string filename = "undefined";

            //  Check if the debug information is present
            //  If the instruction i.e. is inserted by the compiler no debug info is present
            if (dbl) {
                line = dbl.getLine();
                col = dbl->getColumn();
                filename = dbl->getFile()->getDirectory().str() + "/" + dbl->getFile()->getFilename().str();
            }

            locationObj["line"] = line;
            locationObj["column"] = col;
            locationObj["file"] = filename;
            instructionObject["location"] = locationObj;
            nodeObject["instructions"][k] = instructionObject;
        }
    }

    return nodeObject;
}
