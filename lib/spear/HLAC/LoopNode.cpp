#include "HLAC/hlac.h"
#include <type_traits>
#include <unordered_set>

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
        for (auto function_node_iterator = function_node->Nodes.begin(); function_node_iterator != function_node->Nodes.end(); ) {
            HLAC::GenericNode* node = function_node_iterator->get();
            llvm::BasicBlock* block = nullptr;

            // Cast the current node to a normal node and extract the contained basic block
            if (auto* normalnode = dynamic_cast<HLAC::Node*>(node)) {
                block = normalnode->block;
            }

            if (block && loop_basic_blocks.count(block)) {
                this->Nodes.push_back(std::move(*function_node_iterator));   // move node
                function_node_iterator = function_node->Nodes.erase(function_node_iterator); // erase empty slot
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

    std::unique_ptr<LoopNode> LoopNode::makeNode(llvm::Loop *loop, FunctionNode *function_node) {
        auto ln = std::make_unique<LoopNode>(loop, function_node);
        return ln;
    }
}
