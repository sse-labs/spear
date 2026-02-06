/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <unordered_set>
#include <utility>
#include <memory>
#include <vector>
#include <string>

#include "HLAC/hlac.h"

namespace HLAC {
LoopNode::LoopNode(llvm::Loop *loop, FunctionNode *function_node) {
    // Store the LLVM loop
    this->loop = loop;
    this->hasSubLoops = !loop->getSubLoops().empty();
    this->bounds = std::make_pair(0, 0);

    // Create loop nodes recursively for subloops
    for (llvm::Loop *sub : loop->getSubLoops()) {
        auto subLN = LoopNode::makeNode(sub, function_node);
        this->Nodes.emplace_back(std::move(subLN));  // store as GenericNode
    }

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
        if (auto* normalnode = dynamic_cast<Node*>(node)) {
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
    std::unordered_set<GenericNode*> inLoop;
    inLoop.reserve(this->Nodes.size());
    for (auto& nup : this->Nodes) inLoop.insert(nup.get());

    // Move all edges contained entirely inside the loop into the loop node
    for (auto it = function_node->Edges.begin(); it != function_node->Edges.end(); ) {
        Edge* edge = it->get();
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
    // Collapse subloops first, while edges still reference their internal nodes.
    for (auto &nodeUP : this->Nodes) {
        if (auto *childLoop = dynamic_cast<LoopNode *>(nodeUP.get())) {
            childLoop->collapseLoop(edgeList);
        }
    }

    // Build set of nodes directly contained in this loop scope.
    std::unordered_set<GenericNode*> inLoop;
    inLoop.reserve(this->Nodes.size());
    for (auto &nup : this->Nodes) {
        inLoop.insert(nup.get());
    }

    // Collapse this loop:
    //    - move edges fully inside this loop into this->Edges
    //    - redirect boundary edges to use this as endpoint
    for (auto it = edgeList.begin(); it != edgeList.end(); ) {
        Edge *e = it->get();
        if (!e) {
            ++it;
            continue;
        }

        bool srcIn = (inLoop.count(e->soure) != 0);
        bool dstIn = (inLoop.count(e->destination) != 0);

        // Edge completely inside this loop: move it into this->Edges
        if (srcIn && dstIn) {
            this->Edges.push_back(std::move(*it));
            it = edgeList.erase(it);
            continue;
        }

        // Boundary edge: redirect endpoints that touch nodes inside this loop
        if (srcIn) e->soure = this;
        if (dstIn) e->destination = this;

        ++it;
    }
}

void LoopNode::constructCallNodes(bool considerDebugFunctions) {
    // Snapshot current nodes
    std::vector<GenericNode*> work;
    work.reserve(this->Nodes.size());
    for (auto &up : this->Nodes) work.push_back(up.get());

    for (GenericNode *base : work) {
        if (!base) continue;

        if (auto *normalnode = dynamic_cast<Node *>(base)) {
            // Collect calls first
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

                CallNode *callNode = callNodeUP.get();
                if (!callNode->isDebugFunction && !considerDebugFunctions) {
                    this->Nodes.emplace_back(std::move(callNodeUP));
                    callNode->collapseCalls(normalnode, this->Nodes, this->Edges);
                }
            }

        } else if (auto *loopNode = dynamic_cast<LoopNode *>(base)) {
            loopNode->constructCallNodes();
        }
    }
}

std::unique_ptr<LoopNode> LoopNode::makeNode(llvm::Loop *loop, FunctionNode *function_node) {
    auto ln = std::make_unique<LoopNode>(loop, function_node);
    return ln;
}

void LoopNode::printDotRepresentation(std::ostream &os) {
    os << "subgraph \"" << this->getDotName() << "\" {\n";
    os << "style=filled;",
    os << "fillcolor=\"#FFFFFF\";",
    os << "color=\"#2B2B2B\";";
    os << "penwidth=2;";
    os << "fontname=\"Courier\";";
    os << "tooltip=" << "\"" << "METDADATA" << "\";";
    os << "  labelloc=\"t\";\n";
    os << "  label=\"" << this->getDotName() << " (min, max)\r\";\n";
    os << "  " << this->getAnchorDotName()
       << " [shape=point, width=0.01, label=\"\", style=invis];\n";

    for (auto &node : this->Nodes) {
        node->printDotRepresentation(os);
    }

    for (auto &edge : this->Edges) {
        edge->printDotRepresentation(os);
    }

    os << "}\n";
}

std::string LoopNode::getDotName() {
    return "cluster_" + this->getAddress();
}

std::string LoopNode::getAnchorDotName() {
    return this->getDotName() + "_anchor";
}

}  // namespace HLAC
