/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <memory>
#include <vector>
#include <llvm/Demangle/Demangle.h>

#include "HLAC/hlac.h"
#include "HLAC/util.h"

namespace HLAC {

HLAC::CallNode::CallNode(llvm::Function *calls, llvm::CallBase *call) {
    this->call = call;
    this->calledFunction = calls;
    this->name = "Call to " + calledFunction->getName().str();
}

HLAC::Node* HLAC::CallNode::findNodeByBB(std::vector<std::unique_ptr<GenericNode>> &nodeList,
                                llvm::BasicBlock *bb) {
    if (!bb) return nullptr;
    for (auto &up : nodeList) {
        if (!up) continue;
        if (auto *n = dynamic_cast<HLAC::Node*>(up.get())) {
            if (n->block == bb) return n;
        }
    }
    return nullptr;
}

void HLAC::CallNode::collapseCalls(HLAC::Node *belongingNode,
                                  std::vector<std::unique_ptr<GenericNode>> &nodeList,
                                  std::vector<std::unique_ptr<Edge>> &edgeList) {
    if (!belongingNode || !this->call) return;

    llvm::BasicBlock *bb = belongingNode->block;
    if (!bb) return;

    if (this->call->getParent() != bb) return;

    // 1) Collect targets from existing HLAC edges, and erase those edges (dedup source->CallNode)
    std::vector<GenericNode*> targets;
    targets.reserve(8);

    bool hadEdgeToCallNode = false;

    for (auto it = edgeList.begin(); it != edgeList.end(); ) {
        Edge *e = it->get();
        if (!e) { ++it; continue; }

        if (e->soure == belongingNode) {
            if (e->destination == this) {
                hadEdgeToCallNode = true;
                it = edgeList.erase(it); // remove duplicates; we will add exactly one later
                continue;
            } else {
                targets.push_back(e->destination);
                it = edgeList.erase(it); // remove old belongingNode -> target edge
                continue;
            }
        }

        ++it;
    }

    // 2) If HLAC had no outgoing edges, derive targets from LLVM CFG successors (optional)
    if (targets.empty()) {
        llvm::Instruction *term = bb->getTerminator();
        if (!term) return;

        unsigned nsucc = term->getNumSuccessors();
        targets.reserve(nsucc);

        for (unsigned i = 0; i < nsucc; ++i) {
            llvm::BasicBlock *succBB = term->getSuccessor(i);

            // Find HLAC node that represents succBB
            HLAC::Node *succNode = nullptr;
            for (auto &up : nodeList) {
                if (auto *n = dynamic_cast<HLAC::Node*>(up.get())) {
                    if (n->block == succBB) { succNode = n; break; }
                }
            }
            if (succNode) targets.push_back(succNode);
        }
    }

    // 3) Add exactly one belongingNode -> CallNode
    if (!edgeExists(edgeList, belongingNode, this)) {
        edgeList.emplace_back(std::make_unique<Edge>(belongingNode, this));
    }

    // 4) Add CallNode -> targets (dedup)
    for (GenericNode *t : targets) {
        if (!t || t == this) continue;
        if (!edgeExists(edgeList, this, t)) {
            edgeList.emplace_back(std::make_unique<Edge>(this, t));
        }
    }

    // If targets is empty here, CallNode stays a leaf (e.g., call in return/unreachable block).
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

void HLAC::CallNode::printDotRepresentation(std::ostream &os) {
    std::string full = llvm::demangle(this->calledFunction->getName().str());

    std::string shortLabel = full;
    shortLabel = Util::shortenStdStreamOps(std::move(shortLabel));
    shortLabel = Util::dropReturnType(std::move(shortLabel));
    shortLabel = Util::stripParameters(std::move(shortLabel));

    os << getDotName() << "["
       << "shape=record,"
       << "style=filled,"
       << "fillcolor=\"#8D89A6\","
       << "color=\"#2B2B2B\","
       << "penwidth=2,"
       << "fontname=\"Courier\","
       << "label=\"{call:\\l| " << Util::dotRecordEscape(shortLabel) << "}\","
       << "tooltip=\"" << Util::escapeDotLabel(full) << "\""
       << "];\n";
}


std::string CallNode::getDotName() {
    return "CallNode" + this->getAddress();
}

}  // namespace HLAC

