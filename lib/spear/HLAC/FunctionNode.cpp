//
// Created by max on 1/16/26.
//

#include "HLAC/hlac.h"
#include <unordered_map>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Dominators.h>

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
        for (auto &basic_block: *function) {
            auto normal_node = HLAC::Node::makeNode(&basic_block);
            HLAC::GenericNode *raw = normal_node.get();
            this->Nodes.push_back(std::move(normal_node));
            bb2node.emplace(&basic_block, raw);
        }

        // Create the edges
        for (auto &basic_block: *function) {
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

                src->outgoingEdges.push_back(dst);
                dst->incomingEdges.push_back(e.get());
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
    }
}

void HLAC::FunctionNode::constructLoopNodes(std::vector<llvm::Loop *> &loops) {

    for (auto &loop:loops) {
        llvm::outs() << loop->getName() << "\n";
        auto loopNode = LoopNode::makeNode(loop, this);
        loopNode->collapseLoop(this->Edges);

        this->Nodes.push_back(std::move(loopNode));
    }

}

std::unique_ptr<HLAC::FunctionNode> HLAC::FunctionNode::makeNode(llvm::Function* function, llvm::FunctionAnalysisManager *fam) {
    auto fn = std::make_unique<FunctionNode>(function, fam);
    return fn;
}

std::unique_ptr<HLAC::Edge> HLAC::FunctionNode::makeEdge(GenericNode *src, GenericNode *dst) {
    auto edge = std::make_unique<Edge>(src, dst);

    return edge;
}
