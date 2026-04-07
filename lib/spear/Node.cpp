/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <string>
#include <vector>
#include <algorithm>

#include "ProgramGraph.h"
#include "DeMangler.h"
#include "PhasarHandler.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"

// Enables Node caching to mitigate recalculation of the same nodes every time
#define CACHINGENABLED true

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
    if (hasCachedTotalEnergy) {
        return cachedTotalEnergy;
    }

    if (isCurrentlyEvaluating) {
        // Cycle detected
        // You need a real strategy here. Returning 0.0 is only a placeholder.
        return 0.0;
    }

    isCurrentlyEvaluating = true;

    double sum = 0.0;
    auto adjacentNodes = this->getAdjacentNodes();

    if (!adjacentNodes.empty()) {
        switch (this->strategy) {
            case AnalysisStrategy::WORSTCASE: {
                double maxPathEnergy = 0.0;
                bool foundValidSuccessor = false;

                for (auto *adjacentNode : adjacentNodes) {
                    if (adjacentNode->isExceptionFollowUp()) {
                        continue;
                    }

                    double successorEnergy = adjacentNode->getNodeEnergy(handler);
                    if (!foundValidSuccessor || successorEnergy > maxPathEnergy) {
                        maxPathEnergy = successorEnergy;
                        foundValidSuccessor = true;
                    }
                }

                if (foundValidSuccessor) {
                    sum += maxPathEnergy;
                }
                break;
            }

            case AnalysisStrategy::BESTCASE: {
                double minPathEnergy = 0.0;
                bool foundValidSuccessor = false;

                for (auto *adjacentNode : adjacentNodes) {
                    if (adjacentNode->isExceptionFollowUp()) {
                        continue;
                    }

                    double successorEnergy = adjacentNode->getNodeEnergy(handler);
                    if (!foundValidSuccessor || successorEnergy < minPathEnergy) {
                        minPathEnergy = successorEnergy;
                        foundValidSuccessor = true;
                    }
                }

                if (foundValidSuccessor) {
                    sum += minPathEnergy;
                }
                break;
            }

            case AnalysisStrategy::AVERAGECASE: {
                double pathEnergy = 0.0;

                if (adjacentNodes.size() > 1) {
                    Node *leftNode = adjacentNodes[0];
                    Node *rightNode = adjacentNodes[1];

                    double leftEnergy = leftNode->isExceptionFollowUp() ? 0.0 : leftNode->getNodeEnergy(handler);
                    double rightEnergy = rightNode->isExceptionFollowUp() ? 0.0 : rightNode->getNodeEnergy(handler);

                    if (leftNode->isExceptionFollowUp()) {
                        pathEnergy = rightEnergy;
                    } else if (rightNode->isExceptionFollowUp()) {
                        pathEnergy = leftEnergy;
                    } else if (handler->inefficient <= handler->efficient) {
                        pathEnergy = std::max(leftEnergy, rightEnergy);
                        handler->inefficient++;
                    } else {
                        pathEnergy = std::min(leftEnergy, rightEnergy);
                        handler->efficient++;
                    }
                } else {
                    Node *onlySuccessor = adjacentNodes[0];
                    if (!onlySuccessor->isExceptionFollowUp()) {
                        pathEnergy = onlySuccessor->getNodeEnergy(handler);
                    }
                }

                sum += pathEnergy;
                break;
            }
        }
    }

    double localEnergy = handler->getNodeSum(this);
    this->energy = localEnergy;
    sum += localEnergy;

    cachedTotalEnergy = sum;
    if (CACHINGENABLED) {
        hasCachedTotalEnergy = true;
    }
    isCurrentlyEvaluating = false;

    return cachedTotalEnergy;
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
