/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ConfigParser.h"
#include "HLAC/HLACHashing.h"
#include "HLAC/hlac.h"
#include "HLAC/util.h"
#include "Logger.h"
#include "analyses/loopbound/LoopBoundEdgeFunction.h"

namespace HLAC {

namespace {

bool nodeBelongsToLoopScope(const llvm::Loop *owningLoop, const GenericNode *genericNode) {
    if (owningLoop == nullptr || genericNode == nullptr) {
        return false;
    }

    if (const auto *normalNode = dynamic_cast<const Node *>(genericNode)) {
        return normalNode->block != nullptr && owningLoop->contains(normalNode->block);
    }

    if (const auto *loopNode = dynamic_cast<const LoopNode *>(genericNode)) {
        return loopNode->loop != nullptr &&
               loopNode->loop->getHeader() != nullptr &&
               owningLoop->contains(loopNode->loop->getHeader());
    }

    return false;
}

}  // namespace

LoopNode::LoopNode(llvm::Loop *loop, FunctionNode *function_node, ResultRegistry registry,
                   FunctionNode *parentFunctionNode) {
    // Store the LLVM loop
    this->registry = registry;
    this->loop = loop;
    this->hasSubLoops = !loop->getSubLoops().empty();
    auto unknownLoopValue = static_cast<long>(ConfigParser::getAnalysisConfiguration().fallback["loops"]["UNKNOWN_LOOP"]);
    this->bounds = LoopBound::DeltaInterval::interval(0, unknownLoopValue, LoopBound::DeltaInterval::ValueType::Additive);
    this->parentFunction = parentFunctionNode;
    this->nodeType = NodeType::LOOPNODE;

    auto fName = function_node->function->getName().str();
    auto loopName = loop->getName().str();

    auto loopRegistry = registry.getLoopBoundResults();
    if (loopRegistry.contains(fName)) {
        auto functionLoopRegistry = loopRegistry.at(fName);
        if (functionLoopRegistry.contains(loopName)) {
            this->bounds = functionLoopRegistry.at(loopName);
        }
    }

    // Create loop nodes recursively for subloops
    for (llvm::Loop *sub : loop->getSubLoops()) {
        auto subLN = LoopNode::makeNode(sub, function_node, registry, function_node);
        this->Nodes.emplace_back(std::move(subLN));  // store as GenericNode
    }

    // Collect all basicblocks that are contained within our loop
    std::unordered_set<const llvm::BasicBlock *> loop_basic_blocks;
    loop_basic_blocks.reserve(loop->getBlocks().size());
    for (auto *basic_block : loop->getBlocks()) {
        loop_basic_blocks.insert(basic_block);
    }
    this->Nodes.reserve(loop_basic_blocks.size());

    // Move nodes from the given function node to this loop node
    for (auto function_node_iterator = function_node->Nodes.begin();
         function_node_iterator != function_node->Nodes.end();) {
        GenericNode *node = function_node_iterator->get();
        llvm::BasicBlock *block = nullptr;

        // Cast the current node to a normal node and extract the contained basic block
        if (auto *normalnode = dynamic_cast<Node *>(node)) {
            block = normalnode->block;
        }

        if (block && loop_basic_blocks.count(block)) {
            this->Nodes.push_back(std::move(*function_node_iterator));                    // move node
            function_node_iterator = function_node->Nodes.erase(function_node_iterator);  // erase empty slot
        } else {
            ++function_node_iterator;
        }
    }

    // Move all edges that semantically belong to this loop scope into this->Edges.
    // This must include edges whose endpoints are normal nodes inside nested subloops,
    // because child loop collapsing will later work on this scope-local edge container.
    for (auto functionEdgeIterator = function_node->Edges.begin();
         functionEdgeIterator != function_node->Edges.end();) {
        Edge *edge = functionEdgeIterator->get();
        if (edge == nullptr) {
            ++functionEdgeIterator;
            continue;
        }

        const bool sourceBelongsToLoopScope = nodeBelongsToLoopScope(this->loop, edge->soure);
        const bool destinationBelongsToLoopScope = nodeBelongsToLoopScope(this->loop, edge->destination);

        if (sourceBelongsToLoopScope && destinationBelongsToLoopScope) {
            this->Edges.push_back(std::move(*functionEdgeIterator));
            functionEdgeIterator = function_node->Edges.erase(functionEdgeIterator);
        } else {
            ++functionEdgeIterator;
        }
    }

    this->hash = LoopNode::calculateHash();
}

static std::string basicBlockToString(const llvm::BasicBlock* basicBlock) {
    if (basicBlock == nullptr) {
        return "<null>";
    }

    std::string output;
    llvm::raw_string_ostream stream(output);

    // Print block as operand (e.g. %34 instead of full body)
    basicBlock->printAsOperand(stream, false);

    return stream.str();
}

static std::string genericNodeTypeToString(const HLAC::GenericNode* genericNode) {
    if (genericNode == nullptr) {
        return "<null>";
    }

    if (dynamic_cast<const HLAC::Node*>(genericNode) != nullptr) {
        return "Node";
    }

    if (dynamic_cast<const HLAC::LoopNode*>(genericNode) != nullptr) {
        return "LoopNode";
    }

    if (dynamic_cast<const HLAC::VirtualNode*>(genericNode) != nullptr) {
        const auto* virtualNode = dynamic_cast<const HLAC::VirtualNode*>(genericNode);

        if (virtualNode->isEntry) {
            return "VirtualEntry";
        }

        if (virtualNode->isExit) {
            return "VirtualExit";
        }

        return "VirtualNode";
    }

    if (dynamic_cast<const HLAC::CallNode*>(genericNode) != nullptr) {
        return "CallNode";
    }

    if (dynamic_cast<const HLAC::FunctionNode*>(genericNode) != nullptr) {
        return "FunctionNode";
    }

    return "GenericNode";
}

void LoopNode::collapseLoop(std::vector<std::unique_ptr<Edge>> &edgeList) {
    // Collapse subloops first against this loop scope.
    // Child loops must not inspect an outer edge container.
    for (auto &nodeUniquePointer : this->Nodes) {
        if (auto *childLoopNode = dynamic_cast<LoopNode *>(nodeUniquePointer.get())) {
            childLoopNode->collapseLoop(this->Edges);
        }
    }

    Logger::getInstance().log(
        "Collapse loop debug for function: " + this->parentFunction->name,
        LOGLEVEL::INFO
    );
    Logger::getInstance().log(
        "Collapse loop debug: loop node = " + this->getDotName(),
        LOGLEVEL::INFO
    );
    Logger::getInstance().log(
        "Collapse loop debug: header = " + basicBlockToString(this->loop->getHeader()),
        LOGLEVEL::INFO
    );

    llvm::SmallVector<llvm::BasicBlock *, 8> latchBlocks;
    this->loop->getLoopLatches(latchBlocks);
    for (llvm::BasicBlock *latchBlock : latchBlocks) {
        Logger::getInstance().log(
            "Collapse loop debug: latch = " + basicBlockToString(latchBlock),
            LOGLEVEL::INFO
        );
    }

    llvm::SmallVector<llvm::BasicBlock *, 8> exitingBlocks;
    this->loop->getExitingBlocks(exitingBlocks);
    for (llvm::BasicBlock *exitingBlock : exitingBlocks) {
        Logger::getInstance().log(
            "Collapse loop debug: exiting = " + basicBlockToString(exitingBlock),
            LOGLEVEL::INFO
        );
    }

    // Build the set of nodes directly contained in this loop scope.
    std::unordered_set<GenericNode *> nodesInsideLoop;
    nodesInsideLoop.reserve(this->Nodes.size());

    for (int nodeIndex = 0; nodeIndex < static_cast<int>(this->Nodes.size()); ++nodeIndex) {
        auto &nodeUniquePointer = this->Nodes[nodeIndex];

        if (auto *normalNode = dynamic_cast<Node *>(nodeUniquePointer.get())) {
            const bool isHeader = (normalNode->block == this->loop->getHeader());
            const bool isLatch = this->loop->isLoopLatch(normalNode->block);
            const bool isExiting = this->loop->isLoopExiting(normalNode->block);

            Logger::getInstance().log(
                "Collapse loop debug: node[" + std::to_string(nodeIndex) + "] = Node(" +
                basicBlockToString(normalNode->block) + ") {header=" + std::to_string(isHeader ? 1 : 0) +
                ", latch=" + std::to_string(isLatch ? 1 : 0) +
                ", exiting=" + std::to_string(isExiting ? 1 : 0) + "}",
                LOGLEVEL::INFO
            );
        } else if (dynamic_cast<LoopNode *>(nodeUniquePointer.get()) != nullptr) {
            Logger::getInstance().log(
                "Collapse loop debug: node[" + std::to_string(nodeIndex) + "] = LoopNode",
                LOGLEVEL::INFO
            );
        } else if (dynamic_cast<VirtualNode *>(nodeUniquePointer.get()) != nullptr) {
            Logger::getInstance().log(
                "Collapse loop debug: node[" + std::to_string(nodeIndex) + "] = VirtualNode",
                LOGLEVEL::INFO
            );
        } else if (dynamic_cast<CallNode *>(nodeUniquePointer.get()) != nullptr) {
            Logger::getInstance().log(
                "Collapse loop debug: node[" + std::to_string(nodeIndex) + "] = CallNode",
                LOGLEVEL::INFO
            );
        } else {
            Logger::getInstance().log(
                "Collapse loop debug: node[" + std::to_string(nodeIndex) + "] = GenericNode",
                LOGLEVEL::INFO
            );
        }

        nodesInsideLoop.insert(nodeUniquePointer.get());
    }

    auto virtualEntryNode = VirtualNode::makeVirtualPoint(true, false, this);
    auto virtualExitNode = VirtualNode::makeVirtualPoint(false, true, this);

    this->Nodes.push_back(std::move(virtualEntryNode));
    const int virtualEntryIndex = static_cast<int>(this->Nodes.size()) - 1;

    this->Nodes.push_back(std::move(virtualExitNode));
    const int virtualExitIndex = static_cast<int>(this->Nodes.size()) - 1;

    VirtualNode *virtualEntry = dynamic_cast<VirtualNode *>(this->Nodes[virtualEntryIndex].get());
    VirtualNode *virtualExit = dynamic_cast<VirtualNode *>(this->Nodes[virtualExitIndex].get());

    if (virtualEntry == nullptr || virtualExit == nullptr) {
        Logger::getInstance().log(
            "Collapse loop debug: failed to create virtual entry/exit nodes for loop " + this->getDotName(),
            LOGLEVEL::ERROR
        );
        return;
    }

    // Collapse this loop:
    //    - move edges fully inside this loop into this->Edges
    //    - redirect boundary edges to use this as endpoint
    //    - create internal virtual entry/exit edges from the REAL boundary edges
    for (auto edgeIterator = edgeList.begin(); edgeIterator != edgeList.end();) {
        Edge *edge = edgeIterator->get();
        if (edge == nullptr) {
            ++edgeIterator;
            continue;
        }

        const bool sourceIsInsideLoop = (nodesInsideLoop.count(edge->soure) != 0);
        const bool destinationIsInsideLoop = (nodesInsideLoop.count(edge->destination) != 0);

        std::string sourceDescription = genericNodeTypeToString(edge->soure);
        std::string destinationDescription = genericNodeTypeToString(edge->destination);

        if (auto *sourceNode = dynamic_cast<Node *>(edge->soure)) {
            sourceDescription += "(" + basicBlockToString(sourceNode->block) + ")";
        }

        if (auto *destinationNode = dynamic_cast<Node *>(edge->destination)) {
            destinationDescription += "(" + basicBlockToString(destinationNode->block) + ")";
        }

        Logger::getInstance().log(
            "Collapse loop debug: inspect edge " + sourceDescription + " -> " + destinationDescription +
            " {srcIn=" + std::to_string(sourceIsInsideLoop ? 1 : 0) +
            ", dstIn=" + std::to_string(destinationIsInsideLoop ? 1 : 0) + "}",
            LOGLEVEL::INFO
        );

        // Edge completely inside this loop: move it into this->Edges unchanged.
        if (sourceIsInsideLoop && destinationIsInsideLoop) {
            Logger::getInstance().log(
                "Collapse loop debug: moving internal edge into loop",
                LOGLEVEL::INFO
            );
            this->Edges.push_back(std::move(*edgeIterator));
            edgeIterator = edgeList.erase(edgeIterator);
            continue;
        }

        // Boundary edge entering the loop:
        //   outside -> inside
        // Redirect the parent edge to outside -> LoopNode
        // and create the internal edge VirtualEntry -> originalInsideDestination.
        if (!sourceIsInsideLoop && destinationIsInsideLoop) {
            Logger::getInstance().log(
                "Collapse loop debug: creating virtual entry edge VirtualEntry -> " + destinationDescription,
                LOGLEVEL::INFO
            );

            auto internalEntryEdge = std::make_unique<Edge>(virtualEntry, edge->destination);
            this->Edges.push_back(std::move(internalEntryEdge));

            Logger::getInstance().log(
                "Collapse loop debug: redirecting edge destination from " + destinationDescription + " to LoopNode",
                LOGLEVEL::INFO
            );
            edge->destination = this;

            ++edgeIterator;
            continue;
        }

        // Boundary edge leaving the loop:
        //   inside -> outside
        // Redirect the parent edge to LoopNode -> outside
        // and create the internal edge originalInsideSource -> VirtualExit.
        if (sourceIsInsideLoop && !destinationIsInsideLoop) {
            Logger::getInstance().log(
                "Collapse loop debug: creating virtual exit edge " + sourceDescription + " -> VirtualExit",
                LOGLEVEL::INFO
            );

            auto internalExitEdge = std::make_unique<Edge>(edge->soure, virtualExit);
            this->Edges.push_back(std::move(internalExitEdge));

            Logger::getInstance().log(
                "Collapse loop debug: redirecting edge source from " + sourceDescription + " to LoopNode",
                LOGLEVEL::INFO
            );
            edge->soure = this;

            ++edgeIterator;
            continue;
        }

        // Edge completely outside this loop.
        ++edgeIterator;
    }

    Logger::getInstance().log(
        "Collapse loop debug: final internal edge set for loop " + this->getDotName(),
        LOGLEVEL::INFO
    );

    for (const auto &edgeUniquePointer : this->Edges) {
        const Edge *edge = edgeUniquePointer.get();
        if (edge == nullptr) {
            continue;
        }

        std::string sourceDescription = genericNodeTypeToString(edge->soure);
        std::string destinationDescription = genericNodeTypeToString(edge->destination);

        if (auto *sourceNode = dynamic_cast<Node *>(edge->soure)) {
            sourceDescription += "(" + basicBlockToString(sourceNode->block) + ")";
        }

        if (auto *destinationNode = dynamic_cast<Node *>(edge->destination)) {
            destinationDescription += "(" + basicBlockToString(destinationNode->block) + ")";
        }

        Logger::getInstance().log(
            "Collapse loop debug: internal edge " + sourceDescription + " -> " + destinationDescription,
            LOGLEVEL::INFO
        );
    }

    this->refreshBackEdges();
}

void LoopNode::debugDumpEdges() const {
    llvm::BasicBlock* headerBlock = this->loop->getHeader();

    llvm::SmallVector<llvm::BasicBlock*, 8> latchBlocks;
    this->loop->getLoopLatches(latchBlocks);

    Logger::getInstance().log("Loop dump for function: " + this->parentFunction->name, LOGLEVEL::INFO);
    Logger::getInstance().log("Header: " + basicBlockToString(headerBlock), LOGLEVEL::INFO);

    for (llvm::BasicBlock* latchBlock : latchBlocks) {
        Logger::getInstance().log("Latch: " + basicBlockToString(latchBlock), LOGLEVEL::INFO);
    }

    for (const auto& edgeUniquePointer : this->Edges) {
        const Edge* edge = edgeUniquePointer.get();
        if (edge == nullptr) {
            continue;
        }

        std::string sourceDescription = genericNodeTypeToString(edge->soure);
        std::string destinationDescription = genericNodeTypeToString(edge->destination);

        if (auto* sourceNode = dynamic_cast<HLAC::Node*>(edge->soure)) {
            sourceDescription += " " + basicBlockToString(sourceNode->block);
        }

        if (auto* destinationNode = dynamic_cast<HLAC::Node*>(edge->destination)) {
            destinationDescription += " " + basicBlockToString(destinationNode->block);
        }

        Logger::getInstance().log(
            "Edge: " + sourceDescription + " -> " + destinationDescription,
            LOGLEVEL::INFO
        );
    }
}

void LoopNode::refreshBackEdges() {
    this->backEdges.clear();

    llvm::BasicBlock* loopHeader = this->loop->getHeader();
    if (loopHeader == nullptr) {
        return;
    }

    for (auto& edgeUniquePointer : this->Edges) {
        Edge* edge = edgeUniquePointer.get();
        if (edge == nullptr) {
            continue;
        }

        auto* destinationNode = dynamic_cast<Node*>(edge->destination);
        if (destinationNode == nullptr) {
            continue;
        }

        if (destinationNode->block != loopHeader) {
            continue;
        }

        // Ignore artificial loop entry edge
        if (dynamic_cast<VirtualNode*>(edge->soure) != nullptr) {
            continue;
        }

        this->backEdges.push_back(edge);
    }
}

void LoopNode::constructCallNodes(bool considerDebugFunctions) {
    // Snapshot current nodes
    std::vector<GenericNode *> work;
    work.reserve(this->Nodes.size());
    for (auto &up : this->Nodes) {
        work.push_back(up.get());
    }

    for (GenericNode *base : work) {
        if (!base) {
            continue;
        }

        if (auto *normalnode = dynamic_cast<Node *>(base)) {
            // Collect calls first
            std::vector<llvm::CallBase *> calls;
            for (auto it = normalnode->block->begin(); it != normalnode->block->end(); ++it) {
                if (auto *cb = llvm::dyn_cast<llvm::CallBase>(&*it)) {
                    calls.push_back(cb);
                }
            }

            for (llvm::CallBase *callbase : calls) {
                // callbase might have been erased by a previous transformation
                if (!callbase || !callbase->getParent()) {
                    continue;
                }

                llvm::Function *calledFunction = callbase->getCalledFunction();
                if (calledFunction) {
                    auto callNodeUP = CallNode::makeNode(calledFunction, callbase, this->parentFunction);

                    CallNode *callNode = callNodeUP.get();
                    if (!callNode->isDebugFunction || !considerDebugFunctions) {
                        this->Nodes.emplace_back(std::move(callNodeUP));
                        callNode->collapseCalls(normalnode, this->Nodes, this->Edges);
                    }
                }
            }

        } else if (auto *loopNode = dynamic_cast<LoopNode *>(base)) {
            loopNode->constructCallNodes(considerDebugFunctions);
        }
    }

    // Call construction may split, replace or remove edges.
    // Therefore the previously cached backedge pointer may be stale now.
    this->refreshBackEdges();
}

std::unique_ptr<LoopNode> LoopNode::makeNode(llvm::Loop *loop, FunctionNode *function_node, ResultRegistry registry,
                                             FunctionNode *parentFunctionNode) {
    auto ln = std::make_unique<LoopNode>(loop, function_node, registry, parentFunctionNode);
    return ln;
}

void LoopNode::printDotRepresentation(std::ostream &os) {
    os << "subgraph \"" << this->getDotName() << "\" {\n";
    os << "style=filled;";
    os << "fillcolor=\"#FFFFFF\";";
    os << "color=\"#2B2B2B\";";
    os << "penwidth=2;";
    os << "style=\"rounded,filled\";";
    os << "fontname=\"Courier\";";
    os << "tooltip=" << "\"" << "METDADATA" << "\";";
    os << "  labelloc=\"t\";\n";
    os << "  label=\"" << this->getDotName() << "(" << this->bounds.getLowerBound() << ", "
       << this->bounds.getUpperBound() << ")" << "\r\";\n";
    os << "  " << this->getAnchorDotName() << " [shape=point, width=0.01, label=\"\", style=invis];\n";

    for (auto &node : this->Nodes) {
        node->printDotRepresentation(os);
    }

    for (auto &edge : this->Edges) {
        edge->printDotRepresentation(os);
    }

    os << "}\n";
}

void LoopNode::printDotRepresentationWithSolution(std::ostream &os, std::vector<double> solution) {
    os << "subgraph \"" << this->getDotName() << "\" {\n";
    os << "style=filled;";
    os << "fillcolor=\"#FFFFFF\";";
    os << "color=\"#2B2B2B\";";
    os << "penwidth=2;";
    os << "style=\"rounded,filled\";";
    os << "fontname=\"Courier\";";
    os << "tooltip=" << "\"" << "METDADATA" << "\";";
    os << "  labelloc=\"t\";\n";
    os << "  label=\"" << this->getDotName() << "(" << this->bounds.getLowerBound() << ", "
       << this->bounds.getUpperBound() << ")" << "\r\";\n";
    os << "  " << this->getAnchorDotName() << " [shape=point, width=0.01, label=\"\", style=invis];\n";

    for (auto &node : this->Nodes) {
        node->printDotRepresentationWithSolution(os, solution);
    }

    for (auto &edge : this->Edges) {
        edge->printDotRepresentationWithSolution(os, solution);
    }

    os << "}\n";
}

std::string LoopNode::getDotName() {
    return "cluster_" + this->getAddress();
}

std::string LoopNode::getAnchorDotName() {
    return this->getDotName() + "_anchor";
}

double LoopNode::getEnergy() {
    double energy = 0.0;

    return energy;
}

std::string LoopNode::calculateHash() {
    return Hasher::getHashForNode(this);
}

}  // namespace HLAC