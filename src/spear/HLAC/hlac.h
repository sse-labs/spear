/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_HLAC_HLAC_H_
#define SRC_SPEAR_HLAC_HLAC_H_

#include <llvm/IR/BasicBlock.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/Instructions.h>

#include <vector>
#include <string>
#include <memory>

namespace HLAC {

/**
 * Forward declarations of Function and Generic nodes
 */
class FunctionNode;
class GenericNode;

/**
 * Enum to distinguish different types of feasibility that we store inside edges
 */
enum FEASIBILITY {
    TOP,  // The path is feasible
    BOT,  // The path is NOT feasible
    UNKNOWN  // We can not tell if the path is feasible or not. ASSUME THE WORST CASE
};

/**
 * Edge class that represents connections inside the HLAC
 */
class Edge {
 public:
    /**
     * Source node of the edge
     */
    GenericNode* soure = nullptr;
    /**
     * Destination node of the edge
     */
    GenericNode* destination = nullptr;

    /**
     * Feasibility state of the edge
     */
    FEASIBILITY feasibility = UNKNOWN;

    /**
     * Constructs a new edge between the two given nodes
     * @param soure Source node of the edge
     * @param destination Destination node of the edge
     */
    Edge(GenericNode* soure, GenericNode* destination) : soure(soure), destination(destination) {}
};

/**
 * Polymorphic generic node type
 */
class GenericNode {
 public:
    /**
     * Each node has a (unique) name
     */
    std::string name;

    /**
     * Generic destructor
     */
    virtual ~GenericNode() = default;
};

// Normal Nodes
class Node : public GenericNode {
 public:
    llvm::BasicBlock *block = nullptr;

    static std::unique_ptr<Node> makeNode(llvm::BasicBlock *basic_block);
};

// Loop Nodes
class LoopNode : public GenericNode {
 public:
    std::vector<std::unique_ptr<GenericNode>> Nodes;
    std::vector<std::unique_ptr<Edge>> Edges;

    llvm::Loop *loop = nullptr;
    bool hasSubLoops = false;

    LoopNode(llvm::Loop *loop, FunctionNode *function_node);

    static std::unique_ptr<LoopNode> makeNode(llvm::Loop *loop, FunctionNode *function_node);

    /**
     * Takes the given list of edges and rewrites all entities that interact with loops inside this loop node
     * Perform recursivly
     * @param edgeList List of edges from the node, this loop node is contained in
     */
    void collapseLoop(std::vector<std::unique_ptr<Edge>> &edgeList);

    void constructCallNodes();
};

// Function Nodes
class FunctionNode : public GenericNode {
 public:
    std::vector<std::unique_ptr<GenericNode>> Nodes;
    std::vector<std::unique_ptr<Edge>> Edges;
    GenericNode *entry = nullptr;
    GenericNode *exit = nullptr;

    llvm::StringRef name = "<unnamed>";
    llvm::Function *function = nullptr;
    bool isMainFunction = false;
    bool isDebugFunction = false;
    bool isLinkerFunction = false;

    static std::unique_ptr<LoopNode> makeNode(llvm::Loop *loop);
    static std::unique_ptr<FunctionNode> makeNode(llvm::Function *func, llvm::FunctionAnalysisManager *fam);
    static std::unique_ptr<Edge> makeEdge(GenericNode *entry, GenericNode *exit);

    FunctionNode(llvm::Function *function, llvm::FunctionAnalysisManager *fam);

 private:
    void constructLoopNodes(std::vector<llvm::Loop *> &loops);
    void constructCallNodes();
};

// Call Nodes
class CallNode : public GenericNode {
 public:
    llvm::Function *calledFunction = nullptr;
    bool isVirtualCall = false;
    llvm::CallBase *call = nullptr;

    CallNode(llvm::Function *calls, llvm::CallBase *call);

    void collapseCalls(HLAC::Node *belongingNode,
        std::vector<std::unique_ptr<GenericNode>> &nodeList,
        std::vector<std::unique_ptr<Edge>> &edgeList);

    static std::unique_ptr<CallNode> makeNode(llvm::Function *function, llvm::CallBase *instruction);

    static bool edgeExists(const std::vector<std::unique_ptr<Edge>> &edgeList, GenericNode *s, GenericNode *d);
};

// Complete Graph
class hlac {
 public:
    std::vector<std::unique_ptr<FunctionNode>> functions;

    void makeFunction(llvm::Function *function, llvm::FunctionAnalysisManager *fam);
};
}  // namespace HLAC



#endif  // SRC_SPEAR_HLAC_HLAC_H_
