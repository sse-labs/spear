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

LoopNode::LoopNode(llvm::Loop *loop, FunctionNode *function_node, ResultRegistry registry,
                   FunctionNode *parentFunctionNode) {
    // Store the LLVM loop
    this->registry = registry;
    this->loop = loop;
    this->hasSubLoops = !loop->getSubLoops().empty();
    auto unknownLoopValue = static_cast<int64_t>(
        ConfigParser::getAnalysisConfiguration().fallback["loops"]["UNKNOWN_LOOP"]);
    this->bounds = LoopBound::DeltaInterval::interval(0, unknownLoopValue,
        LoopBound::DeltaInterval::ValueType::Additive);
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

    if (function_node->function->getName().str() == "_ZL19stbi_write_jpg_coreP19stbi__write_contextiiiPKvi") {
        std::cout << "Loop " << loop->getName().str() << " bound: " << this->bounds << std::endl;
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

    // Build pointer set of nodes now owned by the loop
    std::unordered_set<GenericNode *> inLoop;
    inLoop.reserve(this->Nodes.size());
    for (auto &nup : this->Nodes) {
        inLoop.insert(nup.get());
    }

    // Move all edges contained entirely inside the loop into the loop node
    for (auto it = function_node->Edges.begin(); it != function_node->Edges.end();) {
        Edge *edge = it->get();
        bool srcIn = (inLoop.count(edge->soure) != 0);
        bool dstIn = (inLoop.count(edge->destination) != 0);

        if (srcIn && dstIn) {
            this->Edges.push_back(std::move(*it));
            it = function_node->Edges.erase(it);
        } else {
            ++it;
        }
    }

    this->hash = LoopNode::calculateHash();
}

void LoopNode::collapseLoop(std::vector<std::unique_ptr<Edge>> &edgeList) {
    // Collapse subloops first, while edges still reference their internal nodes.
    for (auto &nodeUniquePointer : this->Nodes) {
        if (auto *childLoop = dynamic_cast<LoopNode *>(nodeUniquePointer.get())) {
            childLoop->collapseLoop(edgeList);
        }
    }

    std::unordered_set<GenericNode *> nodesInsideLoop;
    nodesInsideLoop.reserve(this->Nodes.size());

    int headerIndex = -1;
    std::vector<int> exitingBlockIndices;

    llvm::BasicBlock *loopHeader = this->loop->getHeader();

    for (int nodeIndex = 0; nodeIndex < static_cast<int>(this->Nodes.size()); ++nodeIndex) {
        GenericNode *genericNode = this->Nodes[nodeIndex].get();
        nodesInsideLoop.insert(genericNode);

        auto *normalNode = dynamic_cast<Node *>(genericNode);
        if (normalNode == nullptr) {
            continue;
        }

        if (normalNode->block == loopHeader) {
            headerIndex = nodeIndex;
        }

        if (this->loop->isLoopExiting(normalNode->block)) {
            exitingBlockIndices.push_back(nodeIndex);
        }
    }

    if (headerIndex < 0) {
        Logger::getInstance().log(
            "Loop collapse failed: could not find loop header node.",
            LOGLEVEL::ERROR);
        return;
    }

    if (exitingBlockIndices.empty()) {
        Logger::getInstance().log(
            "Loop collapse failed: could not find loop exiting block.",
            LOGLEVEL::ERROR);
        return;
    }

    auto virtualEntryNode = VirtualNode::makeVirtualPoint(true, false, this);
    auto virtualExitNode = VirtualNode::makeVirtualPoint(false, true, this);

    this->Nodes.push_back(std::move(virtualEntryNode));
    const int virtualEntryIndex = static_cast<int>(this->Nodes.size()) - 1;

    this->Nodes.push_back(std::move(virtualExitNode));
    const int virtualExitIndex = static_cast<int>(this->Nodes.size()) - 1;

    this->Edges.push_back(std::make_unique<Edge>(
        Edge(this->Nodes[virtualEntryIndex].get(), this->Nodes[headerIndex].get())));

    for (int exitingBlockIndex : exitingBlockIndices) {
        this->Edges.push_back(std::make_unique<Edge>(
            Edge(this->Nodes[exitingBlockIndex].get(), this->Nodes[virtualExitIndex].get())));
    }

    for (auto edgeIterator = edgeList.begin(); edgeIterator != edgeList.end();) {
        Edge *edge = edgeIterator->get();
        if (edge == nullptr) {
            ++edgeIterator;
            continue;
        }

        const bool sourceInsideLoop = nodesInsideLoop.count(edge->soure) != 0;
        const bool destinationInsideLoop = nodesInsideLoop.count(edge->destination) != 0;

        if (sourceInsideLoop && destinationInsideLoop) {
            this->Edges.push_back(std::move(*edgeIterator));
            edgeIterator = edgeList.erase(edgeIterator);
            continue;
        }

        if (sourceInsideLoop) {
            edge->soure = this;
        }

        if (destinationInsideLoop) {
            edge->destination = this;
        }

        ++edgeIterator;
    }

    this->refreshBackEdges();
}

static std::string basicBlockToString(const llvm::BasicBlock* basicBlock) {
    if (basicBlock == nullptr) {
        return "<null>";
    }

    std::string output;
    llvm::raw_string_ostream stream(output);
    basicBlock->printAsOperand(stream, false);
    return stream.str();
}

static std::string genericNodeTypeToString(const HLAC::GenericNode* genericNode) {
    if (dynamic_cast<const HLAC::Node*>(genericNode) != nullptr) {
        return "Node";
    }
    if (dynamic_cast<const HLAC::LoopNode*>(genericNode) != nullptr) {
        return "LoopNode";
    }
    if (dynamic_cast<const HLAC::VirtualNode*>(genericNode) != nullptr) {
        return "VirtualNode";
    }
    if (dynamic_cast<const HLAC::CallNode*>(genericNode) != nullptr) {
        return "CallNode";
    }
    return "GenericNode";
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
            LOGLEVEL::INFO);
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
    os << "style=filled;", os << "fillcolor=\"#FFFFFF\";", os << "color=\"#2B2B2B\";";
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
    os << "style=filled;", os << "fillcolor=\"#FFFFFF\";", os << "color=\"#2B2B2B\";";
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
