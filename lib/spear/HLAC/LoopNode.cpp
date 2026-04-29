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

static std::string basicBlockToString(const llvm::BasicBlock *basicBlock) {
    if (basicBlock == nullptr) {
        return "<null>";
    }

    std::string output;
    llvm::raw_string_ostream stream(output);
    basicBlock->printAsOperand(stream, false);
    return stream.str();
}

static std::string genericNodeTypeToString(const HLAC::GenericNode *genericNode) {
    if (dynamic_cast<const HLAC::Node *>(genericNode) != nullptr) {
        return "Node";
    }

    if (dynamic_cast<const HLAC::LoopNode *>(genericNode) != nullptr) {
        return "LoopNode";
    }

    if (dynamic_cast<const HLAC::VirtualNode *>(genericNode) != nullptr) {
        return "VirtualNode";
    }

    if (dynamic_cast<const HLAC::CallNode *>(genericNode) != nullptr) {
        return "CallNode";
    }

    return "GenericNode";
}

LoopNode::LoopNode(llvm::Loop *loop, FunctionNode *function_node, ResultRegistry registry,
                   FunctionNode *parentFunctionNode) {
    // Store the LLVM loop
    this->registry = registry;
    this->loop = loop;
    this->hasSubLoops = !loop->getSubLoops().empty();

    auto unknownLoopValue = static_cast<int64_t>(
        ConfigParser::getAnalysisConfiguration().fallback["loops"]["UNKNOWN_LOOP"]);

    this->bounds = LoopBound::DeltaInterval::interval(
        0,
        unknownLoopValue,
        LoopBound::DeltaInterval::ValueType::Additive);

    this->parentFunction = parentFunctionNode;
    this->nodeType = NodeType::LOOPNODE;

    auto functionName = function_node->function->getName().str();
    auto loopName = loop->getName().str();

    auto loopRegistry = registry.getLoopBoundResults();
    if (loopRegistry.contains(functionName)) {
        auto functionLoopRegistry = loopRegistry.at(functionName);
        if (functionLoopRegistry.contains(loopName)) {
            this->bounds = functionLoopRegistry.at(loopName);
        }
    }

    // Create loop nodes recursively for subloops
    for (llvm::Loop *subLoop : loop->getSubLoops()) {
        auto subLoopNode = LoopNode::makeNode(subLoop, function_node, registry, function_node);
        this->Nodes.emplace_back(std::move(subLoopNode));
    }

    // Collect all basicblocks that are contained within our loop
    std::unordered_set<const llvm::BasicBlock *> loopBasicBlocks;
    loopBasicBlocks.reserve(loop->getBlocks().size());

    for (auto *basicBlock : loop->getBlocks()) {
        loopBasicBlocks.insert(basicBlock);
    }

    this->Nodes.reserve(loopBasicBlocks.size());

    // Move nodes from the given function node to this loop node
    for (auto functionNodeIterator = function_node->Nodes.begin();
         functionNodeIterator != function_node->Nodes.end();) {
        GenericNode *genericNode = functionNodeIterator->get();
        llvm::BasicBlock *basicBlock = nullptr;

        // Cast the current node to a normal node and extract the contained basic block
        if (auto *normalNode = dynamic_cast<Node *>(genericNode)) {
            basicBlock = normalNode->block;
        }

        if (basicBlock != nullptr && loopBasicBlocks.count(basicBlock) != 0) {
            this->Nodes.push_back(std::move(*functionNodeIterator));
            functionNodeIterator = function_node->Nodes.erase(functionNodeIterator);
        } else {
            ++functionNodeIterator;
        }
    }

    // Build pointer set of nodes now owned by the loop
    std::unordered_set<GenericNode *> nodesInLoop;
    nodesInLoop.reserve(this->Nodes.size());

    for (auto &nodeUniquePointer : this->Nodes) {
        nodesInLoop.insert(nodeUniquePointer.get());
    }

    // Move all edges contained entirely inside the loop into the loop node
    for (auto edgeIterator = function_node->Edges.begin(); edgeIterator != function_node->Edges.end();) {
        Edge *edge = edgeIterator->get();

        if (edge == nullptr) {
            ++edgeIterator;
            continue;
        }

        const bool sourceIsInLoop = nodesInLoop.count(edge->soure) != 0;
        const bool destinationIsInLoop = nodesInLoop.count(edge->destination) != 0;

        if (sourceIsInLoop && destinationIsInLoop) {
            this->Edges.push_back(std::move(*edgeIterator));
            edgeIterator = function_node->Edges.erase(edgeIterator);
        } else {
            ++edgeIterator;
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

    // Build set of nodes directly contained in this loop scope.
    std::unordered_set<GenericNode *> nodesInLoop;
    nodesInLoop.reserve(this->Nodes.size());

    int headerIndex = -1;
    std::vector<int> exitingBlockIndices;

    llvm::BasicBlock *headerBlock = this->loop != nullptr ? this->loop->getHeader() : nullptr;

    for (int nodeIndex = 0; nodeIndex < static_cast<int>(this->Nodes.size()); nodeIndex++) {
        auto &nodeUniquePointer = this->Nodes[nodeIndex];
        GenericNode *genericNode = nodeUniquePointer.get();

        nodesInLoop.insert(genericNode);

        auto *normalNode = dynamic_cast<Node *>(genericNode);
        if (normalNode == nullptr || normalNode->block == nullptr) {
            continue;
        }

        if (normalNode->block == headerBlock) {
            headerIndex = nodeIndex;
        }

        if (this->loop->isLoopExiting(normalNode->block)) {
            exitingBlockIndices.push_back(nodeIndex);
        }
    }

    if (headerIndex < 0) {
        Logger::getInstance().log(
            "Loop collapse failed: header node not found for loop " + this->getDotName(),
            LOGLEVEL::ERROR);
        return;
    }

    if (exitingBlockIndices.empty()) {
        Logger::getInstance().log(
            "Loop collapse warning: no exiting block found for loop " + this->getDotName(),
            LOGLEVEL::ERROR);
    }

    auto entryNode = VirtualNode::makeVirtualPoint(true, false, this);
    auto exitNode = VirtualNode::makeVirtualPoint(false, true, this);

    this->Nodes.push_back(std::move(entryNode));
    const int virtualEntryIndex = static_cast<int>(this->Nodes.size()) - 1;

    this->Nodes.push_back(std::move(exitNode));
    const int virtualExitIndex = static_cast<int>(this->Nodes.size()) - 1;

    GenericNode *virtualEntryNode = this->Nodes[virtualEntryIndex].get();
    GenericNode *virtualExitNode = this->Nodes[virtualExitIndex].get();

    auto entryEdge = std::make_unique<Edge>(
        Edge(virtualEntryNode, this->Nodes[headerIndex].get()));

    this->Edges.push_back(std::move(entryEdge));

    for (int exitingBlockIndex : exitingBlockIndices) {
        auto exitEdge = std::make_unique<Edge>(
            Edge(this->Nodes[exitingBlockIndex].get(), virtualExitNode));

        this->Edges.push_back(std::move(exitEdge));
    }

    // Collapse this loop:
    //    - move edges fully inside this loop into this->Edges
    //    - redirect boundary edges to use this loop node as collapsed endpoint
    for (auto edgeIterator = edgeList.begin(); edgeIterator != edgeList.end();) {
        Edge *edge = edgeIterator->get();

        if (edge == nullptr) {
            ++edgeIterator;
            continue;
        }

        const bool sourceIsInLoop = nodesInLoop.count(edge->soure) != 0;
        const bool destinationIsInLoop = nodesInLoop.count(edge->destination) != 0;

        // Edge completely inside this loop: move it into this->Edges
        if (sourceIsInLoop && destinationIsInLoop) {
            this->Edges.push_back(std::move(*edgeIterator));
            edgeIterator = edgeList.erase(edgeIterator);
            continue;
        }

        // Loop entry edge: outside -> loop-internal becomes outside -> LoopNode
        if (!sourceIsInLoop && destinationIsInLoop) {
            edge->destination = this;
            ++edgeIterator;
            continue;
        }

        // Loop exit edge: loop-internal -> outside becomes LoopNode -> outside.
        // The internal exit decision is represented inside the loop by edges into virtualExitNode.
        if (sourceIsInLoop && !destinationIsInLoop) {
            edge->soure = this;
            ++edgeIterator;
            continue;
        }

        ++edgeIterator;
    }

    this->refreshBackEdges();
}

void LoopNode::debugDumpEdges() const {
    llvm::BasicBlock *headerBlock = this->loop->getHeader();

    llvm::SmallVector<llvm::BasicBlock *, 8> latchBlocks;
    this->loop->getLoopLatches(latchBlocks);

    Logger::getInstance().log("Loop dump for function: " + this->parentFunction->name, LOGLEVEL::INFO);
    Logger::getInstance().log("Header: " + basicBlockToString(headerBlock), LOGLEVEL::INFO);

    for (llvm::BasicBlock *latchBlock : latchBlocks) {
        Logger::getInstance().log("Latch: " + basicBlockToString(latchBlock), LOGLEVEL::INFO);
    }

    for (const auto &edgeUniquePointer : this->Edges) {
        const Edge *edge = edgeUniquePointer.get();

        if (edge == nullptr) {
            continue;
        }

        std::string sourceDescription = genericNodeTypeToString(edge->soure);
        std::string destinationDescription = genericNodeTypeToString(edge->destination);

        if (auto *sourceNode = dynamic_cast<HLAC::Node *>(edge->soure)) {
            sourceDescription += " " + basicBlockToString(sourceNode->block);
        }

        if (auto *destinationNode = dynamic_cast<HLAC::Node *>(edge->destination)) {
            destinationDescription += " " + basicBlockToString(destinationNode->block);
        }

        Logger::getInstance().log(
            "Edge: " + sourceDescription + " -> " + destinationDescription,
            LOGLEVEL::INFO);
    }
}

void LoopNode::refreshBackEdges() {
    this->backEdges.clear();

    llvm::BasicBlock *loopHeader = this->loop->getHeader();
    if (loopHeader == nullptr) {
        return;
    }

    for (auto &edgeUniquePointer : this->Edges) {
        Edge *edge = edgeUniquePointer.get();

        if (edge == nullptr) {
            continue;
        }

        auto *destinationNode = dynamic_cast<Node *>(edge->destination);
        if (destinationNode == nullptr) {
            continue;
        }

        if (destinationNode->block != loopHeader) {
            continue;
        }

        // Ignore artificial loop entry edge
        if (dynamic_cast<VirtualNode *>(edge->soure) != nullptr) {
            continue;
        }

        this->backEdges.push_back(edge);
    }
}

void LoopNode::constructCallNodes(bool considerDebugFunctions) {
    // Snapshot current nodes
    std::vector<GenericNode *> work;
    work.reserve(this->Nodes.size());

    for (auto &nodeUniquePointer : this->Nodes) {
        work.push_back(nodeUniquePointer.get());
    }

    for (GenericNode *genericNode : work) {
        if (genericNode == nullptr) {
            continue;
        }

        if (auto *normalNode = dynamic_cast<Node *>(genericNode)) {
            // Collect calls first
            std::vector<llvm::CallBase *> calls;

            for (auto instructionIterator = normalNode->block->begin();
                 instructionIterator != normalNode->block->end();
                 ++instructionIterator) {
                if (auto *callBase = llvm::dyn_cast<llvm::CallBase>(&*instructionIterator)) {
                    calls.push_back(callBase);
                }
            }

            for (llvm::CallBase *callBase : calls) {
                // callbase might have been erased by a previous transformation
                if (callBase == nullptr || callBase->getParent() == nullptr) {
                    continue;
                }

                llvm::Function *calledFunction = callBase->getCalledFunction();
                if (calledFunction == nullptr) {
                    continue;
                }

                auto callNodeUniquePointer = CallNode::makeNode(calledFunction, callBase, this->parentFunction);
                CallNode *callNode = callNodeUniquePointer.get();

                if (!callNode->isDebugFunction || !considerDebugFunctions) {
                    this->Nodes.emplace_back(std::move(callNodeUniquePointer));
                    callNode->collapseCalls(normalNode, this->Nodes, this->Edges);
                }
            }
        } else if (auto *loopNode = dynamic_cast<LoopNode *>(genericNode)) {
            loopNode->constructCallNodes(considerDebugFunctions);
        }
    }

    // Call construction may split, replace or remove edges.
    // Therefore the previously cached backedge pointer may be stale now.
    this->refreshBackEdges();
}

std::unique_ptr<LoopNode> LoopNode::makeNode(llvm::Loop *loop, FunctionNode *function_node, ResultRegistry registry,
                                             FunctionNode *parentFunctionNode) {
    auto loopNode = std::make_unique<LoopNode>(loop, function_node, registry, parentFunctionNode);
    return loopNode;
}

void LoopNode::printDotRepresentation(std::ostream &outputStream) {
    outputStream << "subgraph \"" << this->getDotName() << "\" {\n";
    outputStream << "style=filled;";
    outputStream << "fillcolor=\"#FFFFFF\";";
    outputStream << "color=\"#2B2B2B\";";
    outputStream << "penwidth=2;";
    outputStream << "style=\"rounded,filled\";";
    outputStream << "fontname=\"Courier\";";
    outputStream << "tooltip=" << "\"" << "METDADATA" << "\";";
    outputStream << "  labelloc=\"t\";\n";
    outputStream << "  label=\"" << this->getDotName() << "(" << this->bounds.getLowerBound() << ", "
                 << this->bounds.getUpperBound() << ")" << "\r\";\n";
    outputStream << "  " << this->getAnchorDotName() << " [shape=point, width=0.01, label=\"\", style=invis];\n";

    for (auto &node : this->Nodes) {
        node->printDotRepresentation(outputStream);
    }

    for (auto &edge : this->Edges) {
        edge->printDotRepresentation(outputStream);
    }

    outputStream << "}\n";
}

void LoopNode::printDotRepresentationWithSolution(std::ostream &outputStream, std::vector<double> solution) {
    outputStream << "subgraph \"" << this->getDotName() << "\" {\n";
    outputStream << "style=filled;";
    outputStream << "fillcolor=\"#FFFFFF\";";
    outputStream << "color=\"#2B2B2B\";";
    outputStream << "penwidth=2;";
    outputStream << "style=\"rounded,filled\";";
    outputStream << "fontname=\"Courier\";";
    outputStream << "tooltip=" << "\"" << "METDADATA" << "\";";
    outputStream << "  labelloc=\"t\";\n";
    outputStream << "  label=\"" << this->getDotName() << "(" << this->bounds.getLowerBound() << ", "
                 << this->bounds.getUpperBound() << ")" << "\r\";\n";
    outputStream << "  " << this->getAnchorDotName() << " [shape=point, width=0.01, label=\"\", style=invis];\n";

    for (auto &node : this->Nodes) {
        node->printDotRepresentationWithSolution(outputStream, solution);
    }

    for (auto &edge : this->Edges) {
        edge->printDotRepresentationWithSolution(outputStream, solution);
    }

    outputStream << "}\n";
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
