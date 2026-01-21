/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <unordered_set>
#include <utility>
#include <memory>
#include <vector>

#include "HLAC/hlac.h"

namespace HLAC {
LoopNode::LoopNode(llvm::Loop *loop, FunctionNode *function_node) {
    this->loop = loop;
    this->hasSubLoops = !loop->getSubLoops().empty();

    // Collect all basicblocks that are contained within our loop
    std::unordered_set<const llvm::BasicBlock*> loop_basic_blocks;
    loop_basic_blocks.reserve(loop->getBlocks().size());
    for (auto* basic_block : loop->getBlocks()) loop_basic_blocks.insert(basic_block);
    this->Nodes.reserve(loop_basic_blocks.size());

    // Move nodes from the given function node to this loop node
    for (auto function_node_iterator = function_node->Nodes.begin();
        function_node_iterator != function_node->Nodes.end(); ) {
        GenericNode* node = function_node_iterator->get();
        llvm::BasicBlock* block = nullptr;

        // Cast the current node to a normal node and extract the contained basic block
        if (auto* normalnode = dynamic_cast<HLAC::Node*>(node)) {
            block = normalnode->block;
        }

        if (block && loop_basic_blocks.count(block)) {
            this->Nodes.push_back(std::move(*function_node_iterator));  // move node
            function_node_iterator = function_node->Nodes.erase(function_node_iterator);  // erase empty slot
        } else {
            ++function_node_iterator;
        }
    }

    // Build pointer set of nodes now owned by the loop
    std::unordered_set<HLAC::GenericNode*> inLoop;
    inLoop.reserve(this->Nodes.size());
    for (auto& nup : this->Nodes) inLoop.insert(nup.get());

    // Move all edges contained entirely inside the loop into the loop node
    for (auto it = function_node->Edges.begin(); it != function_node->Edges.end(); ) {
        HLAC::Edge* edge = it->get();
        bool srcIn = (inLoop.count(edge->soure) != 0);
        bool dstIn = (inLoop.count(edge->destination) != 0);

        if (srcIn && dstIn) {
            this->Edges.push_back(std::move(*it));
            it = function_node->Edges.erase(it);
        } else {
            ++it;
        }
    }
}

void LoopNode::collapseLoop(std::vector<std::unique_ptr<Edge>> &edgeList) {
    // Move the nodes contained in the loop to an unordered set
    std::unordered_set<GenericNode*> inLoop;
    inLoop.reserve(this->Nodes.size());
    for (auto &n : this->Nodes) {
        inLoop.insert(n.get());
    }

    // Check each edge
    for (auto &eup : edgeList) {
        Edge *e = eup.get();

        // Determine wether the edge starts and/or ends inside the loopnode
        bool srcIn = inLoop.count(e->soure) != 0;
        bool dstIn = inLoop.count(e->destination) != 0;

        // Skip edges that a completely contained inside the loop node
        if (srcIn && dstIn) {
            continue;
        }

        // If we encounter an edge that has its source inside our loopnode move the source to this loopnode
        if (srcIn) {
            e->soure = this;
        }

        // If we encounter an edge that has its destination inside our loopnode move the destination
        // to this loopnode
        if (dstIn) {
            e->destination = this;
        }
    }

    // Repeat collapse for all subloop nodes
    for (auto &node : this->Nodes) {
        HLAC::GenericNode *base = node.get();
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(base)) {
            loopNode->collapseLoop(this->Edges);
        }
    }
}

void HLAC::LoopNode::constructCallNodes() {
    // Snapshot current nodes
    std::vector<HLAC::GenericNode*> work;
    work.reserve(this->Nodes.size());
    for (auto &up : this->Nodes) work.push_back(up.get());

    for (HLAC::GenericNode *base : work) {
        if (!base) continue;

        if (auto *normalnode = dynamic_cast<HLAC::Node *>(base)) {
            // Collect calls first (important if collapseCalls splits blocks/erases calls)
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

std::unique_ptr<LoopNode> LoopNode::makeNode(llvm::Loop *loop, FunctionNode *function_node) {
    auto ln = std::make_unique<LoopNode>(loop, function_node);
    return ln;
}

void HLAC::LoopNode::printDotRepresentation(std::ostream &os) {
    os << "subgraph \"" << this->getDotName() << "\" {\n";
    os << "style=filled;",
    os << "fillcolor=\"#FFFFFF\";",
    os << "color=\"#2B2B2B\";";
    os << "penwidth=2;";
    os << "fontname=\"Courier\";";
    os << "  labelloc=\"t\";\n";
    os << "  label=\"" << this->getDotName() << "\\l\";\n";

    for (auto &node : this->Nodes) {
        node->printDotRepresentation(os);
    }

    for (auto &edge : this->Edges) {
        edge->printDotRepresentation(os);
    }

    os << "}\n";
}

std::string HLAC::LoopNode::getDotName() {
    return "cluster_" + this->getAddress();
}

}  // namespace HLAC
