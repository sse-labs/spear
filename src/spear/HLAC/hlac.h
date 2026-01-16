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

    void makeNode(LoopNode *loop);
};

// Function Nodes
class FunctionNode : public GenericNode {
public:
    std::vector<std::unique_ptr<GenericNode>> Nodes;
    std::vector<std::unique_ptr<Edge>> Edges;
    GenericNode *entry = nullptr;
    GenericNode *exit = nullptr;

    std::string_view *name = nullptr;
    llvm::Function *function = nullptr;

    static std::unique_ptr<LoopNode> makeNode(llvm::Loop *loop);
    static std::unique_ptr<FunctionNode> makeNode(llvm::Function *func);
    static std::unique_ptr<Edge> makeEdge(GenericNode *entry, GenericNode *exit);

    FunctionNode(llvm::Function *function);
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

    void makeFunction(llvm::Function *function);
};
}



#endif //SPEAR_HLAC_H