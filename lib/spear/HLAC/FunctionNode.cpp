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
#include <string>

#include "LLVMHandler.h"
#include "HLAC/hlac.h"
#include "HLAC/util.h"

#define SPR_IGNORE_DEBUG_FUNCTIONS 1

namespace HLAC {

FunctionNode::FunctionNode(llvm::Function *function, llvm::FunctionAnalysisManager *function_analysis_manager) {
    // Set the parameter of the FunctionNode
    this->function = function;
    this->name = function->getName();
    this->isLinkerFunction = function->isDeclarationForLinker();

    // Determine if the function is a LLVM debug function
    if (HLAC::Util::starts_with(this->name, "llvm.")) {
        this->isDebugFunction = true;
    }

    // Check if the function is the main function
    if (HLAC::Util::starts_with(this->name, "main")) {
        this->isMainFunction = true;
    }

    // If we can analyze the function e.g. we do not have a linker function
    if (!this->isLinkerFunction) {
        // Create nodes inside the function node
        std::unordered_map<const llvm::BasicBlock *, GenericNode *> bb2node;
        bb2node.reserve(function->size());

        // Create all NormalNodes by iteration over the list of basic blocks
        for (auto &basic_block : *function) {
            auto normal_node = Node::makeNode(&basic_block);
            GenericNode *raw = normal_node.get();
            this->Nodes.push_back(std::move(normal_node));
            bb2node.emplace(&basic_block, raw);
        }

        // Create all Edges from the basic blocks
        for (auto &basic_block : *function) {
            GenericNode *src = bb2node.at(&basic_block);

            llvm::Instruction *term = basic_block.getTerminator();
            if (!term) continue;

            const unsigned nSucc = term->getNumSuccessors();
            for (unsigned i = 0; i < nSucc; ++i) {
                const llvm::BasicBlock *succBB = term->getSuccessor(i);

                auto it = bb2node.find(succBB);
                if (it == bb2node.end()) continue;

                GenericNode *dst = it->second;

                auto e = FunctionNode::makeEdge(src, dst);

                this->Edges.push_back(std::move(e));
            }
        }

        // Query LLVM for information about loops
        llvm::DominatorTree domtree{};
        domtree.recalculate(*function);
        auto &loopAnalysis = function_analysis_manager->getResult<llvm::LoopAnalysis>(*function);
        auto &scalarEvolution = function_analysis_manager->getResult<llvm::ScalarEvolutionAnalysis>(*function);

        // Get the vector of Top-Level loops present in the program
        auto loops = loopAnalysis.getTopLevelLoops();

        // Construct all LoopNodes
        constructLoopNodes(loops);

        // Construct all CallNodes
        constructCallNodes(SPR_IGNORE_DEBUG_FUNCTIONS);
    }
}

void FunctionNode::constructLoopNodes(std::vector<llvm::Loop *> &loops) {
    for (auto &loop : loops) {
        auto loopNode = LoopNode::makeNode(loop, this);
        loopNode->collapseLoop(this->Edges);

        this->Nodes.push_back(std::move(loopNode));
    }
}

void FunctionNode::constructCallNodes(bool considerDebugFunctions) {
    // Create shadowed worklist of Nodes that need to be considered
    // We shadow the list here to avoid dereferencing nodes while we are still working on them
    std::vector<GenericNode*> work;
    work.reserve(this->Nodes.size());
    for (auto &up : this->Nodes) work.push_back(up.get());

    for (GenericNode *base : work) {
        if (!base) continue;

        // If the currently viewed Node is a NormalNode we need to extract all calls and create new CallNodes for each
        // of them
        if (auto *normalnode = dynamic_cast<Node *>(base)) {
            std::string sourcename = normalnode->block->getName().str();
            // List of calls we need to consider
            std::vector<llvm::CallBase*> calls;
            // Search for all calls inside the NormalNode's basic block
            for (auto it = normalnode->block->begin(); it != normalnode->block->end(); ++it) {
                if (auto *cb = llvm::dyn_cast<llvm::CallBase>(&*it)) {
                    calls.push_back(cb);
                }
            }

            // Check each found call
            for (llvm::CallBase *callbase : calls) {
                // callbase might have been erased by a previous transformation
                if (!callbase || !callbase->getParent()) continue;

                llvm::Function *calledFunction = callbase->getCalledFunction();

                // Construct the CallNode
                auto callNodeUP = CallNode::makeNode(calledFunction, callbase);
                CallNode *callNode = callNodeUP.get();

                if (!callNode->isDebugFunction || !considerDebugFunctions) {
                    // Add the CallNode to the list of Nodes and rewrite the edges of this FunctionNode
                    this->Nodes.emplace_back(std::move(callNodeUP));
                    callNode->collapseCalls(normalnode, this->Nodes, this->Edges);
                }
            }

        } else if (auto *loopNode = dynamic_cast<LoopNode *>(base)) {
            // If we encountered a LoopNode we need to construct CallNodes in the contained Nodes
            // Call the constructCallNodes function recursively
            loopNode->constructCallNodes(SPR_IGNORE_DEBUG_FUNCTIONS);
        }
    }
}

std::unique_ptr<FunctionNode> FunctionNode::makeNode(
    llvm::Function* function,
    llvm::FunctionAnalysisManager *fam) {
    auto fn = std::make_unique<FunctionNode>(function, fam);
    return fn;
}

std::unique_ptr<Edge> FunctionNode::makeEdge(GenericNode *src, GenericNode *dst) {
    auto edge = std::make_unique<Edge>(src, dst);
    return edge;
}

void FunctionNode::printDotRepresentation(std::ostream &os) {
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

std::string FunctionNode::getDotName() {
    return "FunctionNode " + this->name;
}

}  // namespace HLAC
