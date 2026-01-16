//
// Created by max on 1/16/26.
//

#include "HLAC/hlac.h"
#include <unordered_map>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/CFG.h>

HLAC::FunctionNode::FunctionNode(llvm::Function *function) {
    this->function = function;
    std::unordered_map<const llvm::BasicBlock*, HLAC::GenericNode*> bb2node;
    bb2node.reserve(function->size());

    // Create the nodes
    for (auto &basic_block : *function) {
        auto normal_node = HLAC::Node::makeNode(&basic_block);
        HLAC::GenericNode* raw = normal_node.get();
        this->Nodes.push_back(std::move(normal_node));
        bb2node.emplace(&basic_block, raw);
    }

    // Create the edges
    for (auto &basic_block : *function) {
        HLAC::GenericNode* src = bb2node.at(&basic_block);

        llvm::Instruction* term = basic_block.getTerminator();
        if (!term) continue;

        const unsigned nSucc = term->getNumSuccessors();
        for (unsigned i = 0; i < nSucc; ++i) {
            const llvm::BasicBlock* succBB = term->getSuccessor(i);

            auto it = bb2node.find(succBB);
            if (it == bb2node.end()) continue;

            HLAC::GenericNode* dst = it->second;

            auto e = HLAC::FunctionNode::makeEdge(src, dst);

            src->outgoingEdges.push_back(dst);
            dst->incomingEdges.push_back(e.get());
            this->Edges.push_back(std::move(e));
        }
    }
}

std::unique_ptr<HLAC::FunctionNode> HLAC::FunctionNode::makeNode(llvm::Function* function) {
    auto fn = std::make_unique<FunctionNode>(function);
    return fn;
}

std::unique_ptr<HLAC::Edge> HLAC::FunctionNode::makeEdge(GenericNode *src, GenericNode *dst) {
    auto edge = std::make_unique<Edge>(src, dst);

    return edge;
}

std::unique_ptr<HLAC::LoopNode> HLAC::FunctionNode::makeNode(llvm::Loop* loop) {
    auto ln = std::make_unique<LoopNode>();
    ln->loop = loop;

    return ln;
}