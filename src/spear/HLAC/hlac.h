//
// Created by max on 1/16/26.
//

#ifndef SPEAR_HLAC_H
#define SPEAR_HLAC_H
#include <vector>
#include <llvm/IR/BasicBlock.h>
#include <llvm/Analysis/LoopInfo.h>
#include <unordered_map>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/CFG.h>

namespace HLAC {
    class FunctionNode;

    class GenericNode;

enum FEASIBILITY {
    TOP,
    BOT,
    UNKNOWN
};

class Edge {
public:
    GenericNode* soure = nullptr;
    GenericNode* destination = nullptr;

    FEASIBILITY feasibility = UNKNOWN;

    Edge(GenericNode* soure, GenericNode* destination) : soure(soure), destination(destination) {}
};

class GenericNode {
public:
    std::vector<Edge *> incomingEdges;
    std::vector<GenericNode *> outgoingEdges;
    std::string name;

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
};

// Call Nodes
class CallNode : public GenericNode {
    llvm::Function *function = nullptr;
    bool isVirtualCall = false;
};

// Complete Graph
class hlac {
public:
    std::vector<std::unique_ptr<FunctionNode>> functions;

    void makeFunction(llvm::Function *function, llvm::FunctionAnalysisManager *fam);
};
}



#endif //SPEAR_HLAC_H