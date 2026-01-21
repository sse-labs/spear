/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Dominators.h>

#include <unordered_map>
#include <utility>
#include <memory>
#include <vector>

#include "LLVMHandler.h"
#include "HLAC/hlac.h"



HLAC::FunctionNode::FunctionNode(llvm::Function *function, llvm::FunctionAnalysisManager *function_analysis_manager) {
    this->function = function;
    this->name = function->getName();
    this->isLinkerFunction = function->isDeclarationForLinker();

    if (this->name.startswith("llvm.")) {
        this->isDebugFunction = true;
    }

    if (this->name.starts_with("main")) {
        this->isMainFunction = true;
    }

    if (!this->isLinkerFunction) {
        std::unordered_map<const llvm::BasicBlock *, HLAC::GenericNode *> bb2node;
        bb2node.reserve(function->size());

        // Create the nodes
        for (auto &basic_block : *function) {
            auto normal_node = HLAC::Node::makeNode(&basic_block);
            GenericNode *raw = normal_node.get();
            this->Nodes.push_back(std::move(normal_node));
            bb2node.emplace(&basic_block, raw);
        }

        // Create the edges
        for (auto &basic_block : *function) {
            HLAC::GenericNode *src = bb2node.at(&basic_block);

            llvm::Instruction *term = basic_block.getTerminator();
            if (!term) continue;

            const unsigned nSucc = term->getNumSuccessors();
            for (unsigned i = 0; i < nSucc; ++i) {
                const llvm::BasicBlock *succBB = term->getSuccessor(i);

                auto it = bb2node.find(succBB);
                if (it == bb2node.end()) continue;

                HLAC::GenericNode *dst = it->second;

                auto e = HLAC::FunctionNode::makeEdge(src, dst);

                this->Edges.push_back(std::move(e));
            }
        }
        llvm::DominatorTree domtree{};
        domtree.recalculate(*function);
        auto &loopAnalysis = function_analysis_manager->getResult<llvm::LoopAnalysis>(*function);
        auto &scalarEvolution = function_analysis_manager->getResult<llvm::ScalarEvolutionAnalysis>(*function);

        // Get the vector of Top-Level loops present in the program
        auto loops = loopAnalysis.getTopLevelLoops();
        constructLoopNodes(loops);
        constructCallNodes();
    }
}

void HLAC::FunctionNode::constructLoopNodes(std::vector<llvm::Loop *> &loops) {
    for (auto &loop : loops) {
        llvm::outs() << loop->getName() << "\n";
        auto loopNode = LoopNode::makeNode(loop, this);
        loopNode->collapseLoop(this->Edges);

        this->Nodes.push_back(std::move(loopNode));
    }
}

void HLAC::FunctionNode::constructCallNodes() {
    std::vector<HLAC::GenericNode*> work;
    work.reserve(this->Nodes.size());
    for (auto &up : this->Nodes) work.push_back(up.get());

    for (HLAC::GenericNode *base : work) {
        if (!base) continue;

        if (auto *normalnode = dynamic_cast<HLAC::Node *>(base)) {
            std::string sourcename = normalnode->block->getName().str();
            std::vector<llvm::CallBase*> calls;
            for (auto it = normalnode->block->begin(); it != normalnode->block->end(); ++it) {
                if (auto *cb = llvm::dyn_cast<llvm::CallBase>(&*it)) {
                    calls.push_back(cb);
                }
            }

            for (llvm::CallBase *callbase : calls) {
                // callbase might have been erased by a previous transformation
                if (!callbase || !callbase->getParent()) continue;

                llvm::Function *calledFunction = callbase->getCalledFunction();
                auto callNodeUP = CallNode::makeNode(calledFunction, callbase);

                HLAC::CallNode *callNode = callNodeUP.get();
                if (!callNode->calledFunction->getName().starts_with("llvm.")) {
                    this->Nodes.emplace_back(std::move(callNodeUP));
                    callNode->collapseCalls(normalnode, this->Nodes, this->Edges);
                }
            }

        } else if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(base)) {
            loopNode->constructCallNodes();
        }
    }
}

std::unique_ptr<HLAC::FunctionNode> HLAC::FunctionNode::makeNode(
    llvm::Function* function,
    llvm::FunctionAnalysisManager *fam) {
    auto fn = std::make_unique<FunctionNode>(function, fam);
    return fn;
}

std::unique_ptr<HLAC::Edge> HLAC::FunctionNode::makeEdge(GenericNode *src, GenericNode *dst) {
    auto edge = std::make_unique<Edge>(src, dst);

    return edge;
}

void HLAC::FunctionNode::printDotRepresentation(std::ostream &os) {
    os << "digraph " << "\"" << this->getDotName() << "\"" << " {" << std::endl;
    os << "graph [pad=\".1\", ranksep=\"1.0\", nodesep=\"1.0\"];" << std::endl;
    os << "compound=true;" << std::endl;
    os << "fontname=\"Courier\";" << std::endl;
    os << "  labelloc=\"t\";\n";
    os << "label=" << "\"" << this->getDotName() << "\";" << std::endl;

    for (auto &node : this->Nodes) {
        node->printDotRepresentation(os);
    }

    for (auto &edge : this->Edges) {
        edge->printDotRepresentation(os);
    }

    os << "}" << std::endl;
}

std::string HLAC::FunctionNode::getDotName() {
    return "FunctionNode " + this->name.str();
}
