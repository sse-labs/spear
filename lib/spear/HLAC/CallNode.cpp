/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <llvm/Demangle/Demangle.h>

#include <memory>
#include <vector>
#include <utility>
#include <string>

#include "HLAC/hlac.h"
#include "HLAC/util.h"

namespace HLAC {

CallNode::CallNode(llvm::Function *calls, llvm::CallBase *call) {
    this->call = call;
    this->calledFunction = calls;
    this->name = "Call to " + calledFunction->getName().str();
    this->isLinkerFunction = calledFunction->isDeclarationForLinker();
    this->isSyscall = false;
    this->isDebugFunction = calledFunction->getName().startswith("llvm.");
}

void CallNode::collapseCalls(Node *belongingNode,
                                  std::vector<std::unique_ptr<GenericNode>> &nodeList,
                                  std::vector<std::unique_ptr<Edge>> &edgeList) {
    // Check if the belonging node and the call are valid
    if (!belongingNode || !this->call) return;

    // Check that the belonging node's block is valid
    llvm::BasicBlock *bb = belongingNode->block;
    if (!bb) return;

    // Validate that our call originates from the referenced basic block
    if (this->call->getParent() != bb) return;

    /**
     * Collect all edges taht start in the belonging node.
     * Afterwards we delete these edges
     */
    std::vector<GenericNode*> targets;
    targets.reserve(8);

    for (auto it = edgeList.begin(); it != edgeList.end(); ) {
        Edge *e = it->get();
        if (!e) {
            ++it;
            continue;
        }

        if (e->soure == belongingNode) {
            // Deal with already existing edges to this CallNode
            // We only want to store one edge
            if (e->destination == this) {
                it = edgeList.erase(it);
                continue;
            }
            // Store the targets that need to be connected to our CallNode
            targets.push_back(e->destination);
            it = edgeList.erase(it);  // Remove the connection belongingNode -> target
            continue;
        }

        ++it;
    }

    // Add an edge belongingNode -> CallNode
    if (!edgeExists(edgeList, belongingNode, this)) {
        edgeList.emplace_back(std::make_unique<Edge>(belongingNode, this));
    }

    // Add CallNode -> targets
    for (GenericNode *t : targets) {
        if (!t || t == this) continue;
        if (!edgeExists(edgeList, this, t)) {
            edgeList.emplace_back(std::make_unique<Edge>(this, t));
        }
    }
}

std::unique_ptr<CallNode> CallNode::makeNode(llvm::Function *function, llvm::CallBase *instruction) {
    auto callnode = std::make_unique<CallNode>(function, instruction);
    return callnode;
}

bool CallNode::edgeExists(const std::vector<std::unique_ptr<Edge>> &edgeList, GenericNode *s, GenericNode *d) {
    for (auto &eup : edgeList) {
        const Edge *e = eup.get();
        if (e && e->soure == s && e->destination == d) return true;
    }
    return false;
}

void CallNode::printDotRepresentation(std::ostream &os) {
    // Demangle name of the function
    std::string full = llvm::demangle(this->calledFunction->getName().str());

    // Call dot string cleaning pipeline
    std::string shortLabel = full;
    shortLabel = Util::shortenStdStreamOps(std::move(shortLabel));
    shortLabel = Util::dropReturnType(std::move(shortLabel));
    shortLabel = Util::stripParameters(std::move(shortLabel));

    // Print dot representation to the given OS
    os << getDotName() << "["
       << "shape=record,"
       << "style=filled,"
       << "fillcolor=\"#8D89A6\","
       << "color=\"#2B2B2B\","
       << "penwidth=2,"
       << "fontname=\"Courier\","
       << "label=\"{"
       << "call:\\l"
       << "| " << Util::dotRecordEscape(shortLabel)
       << "| { LINKERFUNC=" << isLinkerFunction
       <<" | DEBUGFUNC=" << isDebugFunction
       << " | SYSCALL=" << isSyscall << " }"
       << "}\""
       << "];\n";
}


std::string CallNode::getDotName() {
    return "CallNode" + this->getAddress();
}

}  // namespace HLAC

