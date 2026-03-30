/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Dominators.h>

#include <unordered_map>
#include <utility>
#include <memory>
#include <vector>
#include <string>
#include <queue>
#include <map>

#include "HLAC/HLACHashing.h"
#include "HLAC/hlac.h"
#include "HLAC/util.h"
#include "LLVMHandler.h"

#include <ClpEventHandler.hpp>

#define SPR_IGNORE_DEBUG_FUNCTIONS 1

namespace HLAC {

FunctionNode::FunctionNode(llvm::Function *function,
    llvm::FunctionAnalysisManager *function_analysis_manager,
    const ResultRegistry& registry,
    hlac *parentGraph) {
    // Set the parameter of the FunctionNode
    this->registry = registry;
    this->function = function;
    this->name = function->getName();
    this->isLinkerFunction = function->isDeclarationForLinker();
    this->parentGraph = parentGraph;

    // Determine if the function is a LLVM debug function
    if (HLAC::Util::starts_with(this->name, "llvm.")) {
        this->isDebugFunction = true;
    }

    // Check if the function is the main function
    if (HLAC::Util::starts_with(this->name, "main")) {
        this->isMainFunction = true;
    }

    // If we can analyze the function e.g. we do not have a linker function
    if (!this->isLinkerFunction) {
        // Create nodes inside the function node
        std::unordered_map<const llvm::BasicBlock *, GenericNode *> bb2node;
        bb2node.reserve(function->size());

        int localEntryIndex = -1;
        int localExitIndex = -1;

        // create entry once
        auto entryNode = VirtualNode::makeVirtualPoint(true, false, this);
        this->Nodes.push_back(std::move(entryNode));
        localEntryIndex = this->Nodes.size() - 1;

        // create all BB nodes, remember whether an exit BB exists
        bool hasExitBlock = false;
        for (auto &basic_block : *function) {
            llvm::Instruction *term = basic_block.getTerminator();

            auto normal_node = Node::makeNode(&basic_block);
            GenericNode *raw = normal_node.get();
            this->Nodes.push_back(std::move(normal_node));
            bb2node.emplace(&basic_block, raw);

            if (term && term->getNumSuccessors() == 0) {
                hasExitBlock = true;
            }
        }

        if (hasExitBlock) {
            auto exitNode = VirtualNode::makeVirtualPoint(false, true, this);
            this->Nodes.push_back(std::move(exitNode));
            localExitIndex = this->Nodes.size() - 1;
        }

        auto feasResult = registry.getFeasibilityResults();

        if (feasResult.contains(this->name)) {
            Feasibility::BlockFeasibilityMap blockMapping = feasResult.at(this->name);

            // Create all Edges from the basic blocks
            for (auto &basic_block : *function) {
                GenericNode *src = bb2node.at(&basic_block);

                llvm::Instruction *term = basic_block.getTerminator();
                if (!term) continue;

                const unsigned nSucc = term->getNumSuccessors();

                // Handle virtual points
                if (localEntryIndex != -1) {
                    if (basic_block.isEntryBlock()) {
                        auto e = FunctionNode::makeEdge(this->Nodes[localEntryIndex].get(), src);
                        e->feasibility = true;
                        this->Edges.push_back(std::move(e));
                    }
                }

                if (localExitIndex != -1) {
                    if (term->getNumSuccessors() == 0) {
                        auto e = FunctionNode::makeEdge(src, this->Nodes[localExitIndex].get());
                        e->feasibility = true;
                        this->Edges.push_back(std::move(e));
                    }
                }


                for (unsigned i = 0; i < nSucc; ++i) {
                    const llvm::BasicBlock *succBB = term->getSuccessor(i);

                    auto it = bb2node.find(succBB);
                    if (it == bb2node.end()) {
                        continue;
                    }

                    GenericNode *dst = it->second;

                    auto blockName = succBB->getName().str();

                    bool willEdgeBeFeasible = true;

                    if (blockMapping.find(blockName) != blockMapping.end()) {
                        willEdgeBeFeasible = blockMapping.at(succBB->getName().str()).Feasible;
                    }

                    auto e = FunctionNode::makeEdge(src, dst);

                    e->feasibility = willEdgeBeFeasible;

                    this->Edges.push_back(std::move(e));
                }
            }
        }

        // Query LLVM for information about loops
        llvm::DominatorTree domtree{};
        domtree.recalculate(*function);
        auto &loopAnalysis = function_analysis_manager->getResult<llvm::LoopAnalysis>(*function);
        auto &scalarEvolution = function_analysis_manager->getResult<llvm::ScalarEvolutionAnalysis>(*function);

        // Get the vector of Top-Level loops present in the program
        auto loops = loopAnalysis.getTopLevelLoops();

        // Construct all LoopNodes
        constructLoopNodes(loops);

        // Construct all CallNodes
        constructCallNodes(SPR_IGNORE_DEBUG_FUNCTIONS);

        // Set entry and exit indices
        for (int i = 0; i < this->Nodes.size(); ++i) {
            auto &node = this->Nodes[i];;

            if (auto *virtualNode = dynamic_cast<VirtualNode *>(node.get())) {
                if (virtualNode->isEntry) {
                    entryIndex = i;
                }
                if (virtualNode->isExit) {
                    exitIndex = i;
                }
            }
        }

        topologicalSortedRepresentationOfNodes = this->getTopologicalOrdering();

        for (std::size_t i = 0; i < this->Nodes.size(); ++i) {
            nodeLookup[topologicalSortedRepresentationOfNodes[i]] = i;
        }

        // Build adjacency list as vectors of raw edge pointers
        std::vector<std::vector<HLAC::Edge*>> adjacencyList(this->Nodes.size());

        for (const auto &edgeUP : this->Edges) {
            HLAC::Edge *edge = edgeUP.get();
            if (!edge || !edge->soure || !edge->destination) {
                continue;
            }

            auto it = nodeLookup.find(edge->soure);
            if (it != nodeLookup.end()) {
                adjacencyList[it->second].push_back(edge);
            }
        }
        this->adjacencyRepresentation = adjacencyList;
    }

    this->hash = FunctionNode::calculateHash();
}

void FunctionNode::constructLoopNodes(std::vector<llvm::Loop *> &loops) {
    for (auto &loop : loops) {
        auto loopNode = LoopNode::makeNode(loop, this, registry, this);
        loopNode->collapseLoop(this->Edges);

        this->Nodes.push_back(std::move(loopNode));
    }
}

void FunctionNode::constructCallNodes(bool considerDebugFunctions) {
    // Create shadowed worklist of Nodes that need to be considered
    // We shadow the list here to avoid dereferencing nodes while we are still working on them
    std::vector<GenericNode*> work;
    work.reserve(this->Nodes.size());
    for (auto &up : this->Nodes) work.push_back(up.get());

    for (GenericNode *base : work) {
        if (!base) continue;

        // If the currently viewed Node is a NormalNode we need to extract all calls and create new CallNodes for each
        // of them
        if (auto *normalnode = dynamic_cast<Node *>(base)) {
            std::string sourcename = normalnode->block->getName().str();
            // List of calls we need to consider
            std::vector<llvm::CallBase*> calls;
            // Search for all calls inside the NormalNode's basic block
            for (auto it = normalnode->block->begin(); it != normalnode->block->end(); ++it) {
                if (auto *cb = llvm::dyn_cast<llvm::CallBase>(&*it)) {
                    calls.push_back(cb);
                }
            }

            // Check each found call
            for (llvm::CallBase *callbase : calls) {
                // callbase might have been erased by a previous transformation
                if (!callbase || !callbase->getParent()) continue;

                llvm::Function *calledFunction = callbase->getCalledFunction();

                // Construct the CallNode
                auto callNodeUP = CallNode::makeNode(calledFunction, callbase, this);
                CallNode *callNode = callNodeUP.get();

                if (!callNode->isDebugFunction || !considerDebugFunctions) {
                    // Add the CallNode to the list of Nodes and rewrite the edges of this FunctionNode
                    this->Nodes.emplace_back(std::move(callNodeUP));
                    callNode->collapseCalls(normalnode, this->Nodes, this->Edges);
                }
            }

        } else if (auto *loopNode = dynamic_cast<LoopNode *>(base)) {
            // If we encountered a LoopNode we need to construct CallNodes in the contained Nodes
            // Call the constructCallNodes function recursively
            loopNode->constructCallNodes(SPR_IGNORE_DEBUG_FUNCTIONS);
        }
    }
}

std::unique_ptr<FunctionNode> FunctionNode::makeNode(
    llvm::Function* function,
    llvm::FunctionAnalysisManager *fam,
    ResultRegistry registry,
    hlac *parentGraph) {
    auto fn = std::make_unique<FunctionNode>(function, fam, registry, parentGraph);
    return fn;
}

std::unique_ptr<Edge> FunctionNode::makeEdge(GenericNode *src, GenericNode *dst) {
    auto edge = std::make_unique<Edge>(src, dst);
    return edge;
}

void FunctionNode::printDotRepresentation(std::ostream &os) {
    os << "digraph " << "\"" << this->getDotName() << "\"" << " {" << std::endl;
    os << "graph [pad=\".3\", ranksep=\"1.4\", nodesep=\"1.0\"];" << std::endl;
    os << "compound=true;" << std::endl;
    os << "style=\"rounded,filled\";" << std::endl;
    os << "fontname=\"Courier\";" << std::endl;
    os << "  labelloc=\"t\";\n";
    os << "label=" << "\"" << this->getDotName() << "\";" << std::endl;

    for (auto &node : this->Nodes) {
        node->printDotRepresentation(os);
    }

    for (auto &edge : this->Edges) {
        edge->printDotRepresentation(os);
    }

    os << "}" << std::endl;
}

void FunctionNode::printDotRepresentationWithSolution(std::ostream &os, std::vector<double> solution) {
    os << "digraph " << "\"" << this->getDotName() << "\"" << " {" << std::endl;
    os << "graph [pad=\".3\", ranksep=\"1.4\", nodesep=\"1.0\"];" << std::endl;
    os << "compound=true;" << std::endl;
    os << "style=\"rounded,filled\";" << std::endl;
    os << "fontname=\"Courier\";" << std::endl;
    os << "  labelloc=\"t\";\n";
    os << "label=" << "\"" << this->getDotName() << "\";" << std::endl;

    for (auto &node : this->Nodes) {
        node->printDotRepresentationWithSolution(os, solution);
    }

    for (auto &edge : this->Edges) {
        edge->printDotRepresentationWithSolution(os, solution);
    }

    os << "}" << std::endl;
}

std::string FunctionNode::getDotName() {
    return "FunctionNode " + this->name;
}

double FunctionNode::getEnergy() {
    // Here we need to implement the global ILP formulation for the function energy estimation or the
    // DAG search based approach
    double energy = 0.0;

    // DUMMY CALCULATION
    // Just sum up the energy of all contained nodes for now, this is not the actual energy calculation we want to do
    // in the end, but it is sufficient for testing purposes
    for (auto &node : this->Nodes) {
        energy += node->getEnergy();
    }

    // After the energy is calculated store it in the energy cache of the parent graph to avoid redundant calculations
    this->parentGraph->FunctionEnergyCache[this->function->getName().str()] = energy;
    return energy;
}

std::vector<GenericNode *> FunctionNode::getTopologicalOrdering() {
    // Node -> Incoming edge representation to access in degree more easily
    auto incomingMapping = Util::createIncomingList(this->Nodes, this->Edges);

    // Adjacent representation of our graph
    auto G = Util::createAdjacentList(this->Nodes, this->Edges);
    std::vector<GenericNode *> topologicalOrdering;

    // Calculate in-degree
    std::map<HLAC::GenericNode *, int> inDegree;
    for (const auto [node, edgelist] : incomingMapping) {
        inDegree[node] = incomingMapping[node].size();
    }

    // Heap where access the discovered nodes
    std::queue<GenericNode *> H;

    // Add all nodes to the heap that have 0 incoming edges (should only be the entry node)
    for (auto [node, incomingEdges] : inDegree) {
        if (incomingEdges == 0) {
            H.push(node);
        }
    }

    // Iterate over the node heap
    while (!H.empty()) {
        // Get the node at the front of the queue
        auto minVertex = H.front();
        H.pop();

        // Add this element to the ordering. As we assume correct ordering for the element under analysis
        topologicalOrdering.push_back(minVertex);

        // Iterate over the adjacent edges of our element
        for (auto edge : G[minVertex]) {
            // We simulate the removal of the edge from the graph
            // Decrease indegree of destination node
            auto dest = edge->destination;
            inDegree[dest]--;

            // If the destination node is now no longer accesible from any other node (e.g there is no other node
            // that we need to deal with first)
            if (inDegree[dest] == 0) {
                H.push(dest);
            }
        }
    }


    return topologicalOrdering;
}

std::string FunctionNode::calculateHash() {
    return Hasher::getHashForNode(this);
}

}  // namespace HLAC
