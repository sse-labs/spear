/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <memory>
#include <vector>

#include "HLAC/hlac.h"

namespace HLAC {

HLAC::CallNode::CallNode(llvm::Function *calls, llvm::CallBase *call) {
    this->call = call;
    this->calledFunction = calls;
}

void HLAC::CallNode::collapseCalls(HLAC::Node *belongingNode,
    std::vector<std::unique_ptr<GenericNode>> &nodeList,
    std::vector<std::unique_ptr<Edge>> &edgeList) {
    if (!belongingNode || !this->call) return;

    llvm::BasicBlock *bb = belongingNode->block;
    if (!bb) return;

    // Ensure call is really in this block
    if (this->call->getParent() != bb) return;

    // Collect the original outgoing targets from belongingNode
    std::vector<GenericNode*> targets;
    targets.reserve(8);

    for (auto &eup : edgeList) {
        Edge *e = eup.get();
        if (!e) continue;
        if (e->soure == belongingNode) {
            // Don't route an existing edge to this through itself
            if (e->destination == this) continue;
            targets.push_back(e->destination);
        }
    }

    if (targets.empty()) {
        // No outgoing edges to rewire
        return;
    }

    // Redirecting all edges starting in belongingNode to CallNode,
    // and then add CallNode -> target edges.
    for (auto &eup : edgeList) {
        Edge *e = eup.get();
        if (!e) continue;

        if (e->soure == belongingNode && e->destination != this) {
            // belongingNode -> target becomes belongingNode -> CallNode
            e->destination = this;
        }
    }

    // Ensure belongingNode -> CallNode edge exists
    if (!edgeExists(edgeList, belongingNode, this)) {
        edgeList.emplace_back(std::make_unique<Edge>(belongingNode, this));
    }

    // Add CallNode -> original targets
    for (GenericNode *t : targets) {
        if (!t) continue;
        if (t == this) continue;
        if (!edgeExists(edgeList, this, t)) {
            edgeList.emplace_back(std::make_unique<Edge>(this, t));
        }
    }
}

std::unique_ptr<CallNode> HLAC::CallNode::makeNode(llvm::Function *function, llvm::CallBase *instruction) {
    auto callnode = std::make_unique<CallNode>(function, instruction);
    return callnode;
}

bool HLAC::CallNode::edgeExists(const std::vector<std::unique_ptr<Edge>> &edgeList, GenericNode *s, GenericNode *d) {
    for (auto &eup : edgeList) {
        const Edge *e = eup.get();
        if (e && e->soure == s && e->destination == d) return true;
    }
    return false;
}

}  // namespace HLAC
