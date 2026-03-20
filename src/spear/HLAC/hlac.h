/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_HLAC_HLAC_H_
#define SRC_SPEAR_HLAC_HLAC_H_

#include <CoinPackedMatrix.hpp>
#include "OsiClpSolverInterface.hpp"
#include "CbcModel.hpp"
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/BasicBlock.h>

#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "analyses/ResultRegistry.h"

namespace HLAC {


/**
 * Forward declarations of Function and Generic nodes
 */
class hlac;
class FunctionNode;
class GenericNode;
class LoopNode;

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
     * Base dot representation printing
     * @param os
     */
    virtual void printDotRepresentation(std::ostream &os) { os << "NOT IMPLEMENTED" << std::endl; }

    /**
     * Print the original dot representation, but color edges that will be considered in the optimal solution
     * @param os
     * @param result
     */
    virtual void printDotRepresentationWithSolution(std::ostream &os, std::vector<double> result) {
        os << "NOT IMPLEMENTED" << std::endl;
    }

    /**
     * Constructs the dot name of the node.
     * @return Debug Value if function is not overriden
     */
    virtual std::string getDotName() { return "NOT IMPLEMENTED"; }

    /**
     * Calculate the energy of the node.
     * @return Energy of the node, 0.0 if function is not overridden
     */
    virtual double getEnergy() { return 0.0; }

    /**
     * Calculate the address of this node and return it as string
     * @return String representation of the nodes address
     */
    std::string getAddress() {
        auto addr = reinterpret_cast<std::uintptr_t>(this);
        return std::to_string(addr);
    }

    /**
     * Generic destructor
     */
    virtual ~GenericNode() = default;
};

class VirtualNode : public GenericNode {
 public:

    bool isEntry = false;
    bool isExit = false;

    GenericNode *parent;

    static std::unique_ptr<VirtualNode> makeVirtualPoint(bool isEntry, bool isExit, GenericNode *givparent);

    /**
     * Print this NormalNode as dot representation
     * Prints the nodes name and the contained llvm IR statements
     * @param os Outputstream to print to
     */
    void printDotRepresentation(std::ostream &os) override;

    void printDotRepresentationWithSolution(std::ostream &os, std::vector<double> result) override;

    /**
     * Get the name of the NormalNode escaped for dot
     * @return Name of the node as escaped dot string
     */
    std::string getDotName() override;
};

/**
 * Edge class that represents connections inside the HLAC
 */
class Edge {
public:

    int ilpIndex = -1;

    std::string id;

    /**
     * Source node of the edge
     */
    GenericNode *soure = nullptr;
    /**
     * Destination node of the edge
     */
    GenericNode *destination = nullptr;

    /**
     * Feasibility state of the edge
     */
    bool feasibility = true;

    /**
     * Constructs a new edge between the two given nodes
     * @param soure Source node of the edge
     * @param destination Destination node of the edge
     */
    Edge(GenericNode *soure, GenericNode *destination) : soure(soure), destination(destination) {
        id = soure->getDotName() + "->" + destination->getDotName();
    }

    /**
     * Print the HLAC graph as dot representation
     * @param os Outputstream to write to
     */
    void printDotRepresentation(std::ostream &os);

    void printDotRepresentationWithSolution(std::ostream &os, std::vector<double> result);

private:
    /**
     * Searches for the first occasion of a non-LoopNode in the given LoopNode so we can draw an edge to this Node
     * @param loopNode LoopNode to search in
     * @param pickBack If true searches for the last node in the LoopNode, if false for the first Node
     * @return Found Node, nullptr if no Node can be found
     */
    static GenericNode *pickNonLoopNode(HLAC::LoopNode *loopNode, bool pickBack);
};

/**
 * NormalNode that represents basic blocks from the original CFG
 */
class Node : public GenericNode {
 public:
    /**
     * Block stored in the node
     */
    llvm::BasicBlock *block = nullptr;

    /**
     * Construct a new NormalNode with the given basic block and return it
     * @return Unique pointer to the constructed NOde
     */
    static std::unique_ptr<Node> makeNode(llvm::BasicBlock *basic_block);

    /**
     * Print this NormalNode as dot representation
     * Prints the nodes name and the contained llvm IR statements
     * @param os Outputstream to print to
     */
    void printDotRepresentation(std::ostream &os) override;

    void printDotRepresentationWithSolution(std::ostream &os, std::vector<double> result) override;

    /**
     * Get the name of the NormalNode escaped for dot
     * @return Name of the node as escaped dot string
     */
    std::string getDotName() override;

    /**
     * Return the sum of the energy of all instructions contained in the basic block represented by this Node
     * @return Energy of the Node
     */
    double getEnergy() override;
};

/**
 * LoopNode that represents loops inside our HLAC graph
 * Can contain other Nodes and Edges
 */
class LoopNode : public GenericNode {
 public:
    ResultRegistry registry;

    HLAC::Edge *backEdge = nullptr;

    /**
     * List of contained Nodes
     */
    std::vector<std::unique_ptr<GenericNode>> Nodes;

    /**
     * List of contained Edges
     */
    std::vector<std::unique_ptr<Edge>> Edges;

    /**
     * Loop represented by the HLAC
     */
    llvm::Loop *loop = nullptr;

    /**
     * Loop bound representing (min, max) iteration count
     */
    LoopBound::DeltaInterval bounds;

    FunctionNode *parentFunction = nullptr;

    /**
     * Flag to store if the contained loop has subloops that find representation as further LoopNodes
     */
    bool hasSubLoops = false;

    /**
     * Constructor to create a new LoopNode
     * @param loop loop that should be represented by the LoopNOde
     * @param function_node FunctionNode, the LoopNode is contained in
     */
    LoopNode(llvm::Loop *loop, FunctionNode *function_node, ResultRegistry registry, FunctionNode *parentFunctionNode);

    /**
     * Creates a new LoopNode and returns it
     * @param loop Loop that should be converted to a LoopNOde
     * @param function_node FunctionNode the LoopNode should be contained in
     * @return Returns unique pointer to the constructed LoopNode
     */
    static std::unique_ptr<LoopNode> makeNode(llvm::Loop *loop, FunctionNode *function_node, ResultRegistry registry,
                                              FunctionNode *parentFunctionNode);

    /**
     * Takes the given list of edges and rewrites all entities that interact with loops inside this loop node
     * Perform recursivly
     * @param edgeList List of edges from the node, this loop node is contained in
     */
    void collapseLoop(std::vector<std::unique_ptr<Edge>> &edgeList);

    /**
     * Construct CallNodes from calls contained within this LoopNode
     * We need this function here to perform recurive CallNode construction
     * @param considerDebugFunctions
     */
    void constructCallNodes(bool considerDebugFunctions = false);

    /**
     * Print the LoopNode as dot representation
     * LoopNodes are Subgraph/Clusters in dot
     * @param os Outputstream to write to
     */
    void printDotRepresentation(std::ostream &os) override;

    void printDotRepresentationWithSolution(std::ostream &os, std::vector<double> result) override;

    /**
     * Get the name of the LoopNode escaped for dot
     * @return Name of the node as escaped dot string
     */
    std::string getDotName() override;

    /**
     * Get the name of the anchor node used to connect normal nodes with the cluster
     * representation of the LoopNode
     * @return Name of the anchor of the LoopNode
     */
    std::string getAnchorDotName();

    /**
     * Return the energy of the node
     * @return Energy as double value
     */
    double getEnergy() override;
};

/**
 * FunctionNode that represents functions referenced by the program under analysis
 */
class FunctionNode : public GenericNode {
 public:
    ResultRegistry registry;

    /**
     * Mapping of function energy
     * worst and best case
     */
    std::map<std::string, double> functionEnergy;

    /**
     * List of contained Nodes
     */
    std::vector<std::unique_ptr<GenericNode>> Nodes;

    /**
     * List of contained Edges
     */
    std::vector<std::unique_ptr<Edge>> Edges;

    /**
     * Function represented by the FunctionNode
     */
    llvm::Function *function = nullptr;

    HLAC::hlac *parentGraph = nullptr;

    /**
     * Meta flags to distinguish different kinds of function
     */
    bool isMainFunction = false;
    bool isDebugFunction = false;
    bool isLinkerFunction = false;

    /**
     * Create a new FunctionNode and return it
     * @param func Function that will be reprented by the FunctioNode
     * @param fam FunctionAnalysisManager that is used to construct the analysis
     * @return Returns constructed FunctionNode
     */
    static std::unique_ptr<FunctionNode> makeNode(llvm::Function *func, llvm::FunctionAnalysisManager *fam,
                                                  ResultRegistry registry, hlac *parentGraph);

    /**
     * Create a new Edge in the HLAC
     * @param source Source of the Edge
     * @param detination Destinaion of the Edge
     * @return Returns the constructed Edge
     */
    static std::unique_ptr<Edge> makeEdge(GenericNode *source, GenericNode *detination);

    /**
     * Create a new FunctionNode
     * @param function Function that is represented by the FunctioNode
     * @param fam FunctionAnalysisManager that is used to drive the analysis
     */
    FunctionNode(llvm::Function *function, llvm::FunctionAnalysisManager *fam, const ResultRegistry &registry,
                 hlac *parentGraph);

    /**
     * Print dot representation of the FunctionNode
     * @param os
     */
    void printDotRepresentation(std::ostream &os) override;

    void printDotRepresentationWithSolution(std::ostream &os, std::vector<double> result) override;

    /**
     * Construct the dot escaped name of the FunctionNode
     * @return
     */
    std::string getDotName() override;

    /**
     * Return the energy of the node
     * @return Energy as double value
     */
    double getEnergy() override;

 private:
    /**
     * Iterates over the contained nodes and constructs LoopNodes recursively for all contained loops
     * @param loops All llvm loops contained in this function that we should construct a LoopNode for
     */
    void constructLoopNodes(std::vector<llvm::Loop *> &loops);

    /**
     * Iterates over the NormalNodes contained in the FunctionNode and constructs CallNodes for each
     * call that can be found
     * @param considerDebugFunctions Toggle to consider LLVM debug functions e.g. llvm.dbg.value
     */
    void constructCallNodes(bool considerDebugFunctions = false);
};

/**
 * CallNodes that represent calls to other functions
 */
class CallNode : public GenericNode {
 public:
    /**
     * LLVM Function that will be called by the corresponding call instruction
     */
    llvm::Function *calledFunction = nullptr;

    /**
     * Meta flags that are used to distinguish different kinds of functions
     */
    bool isLinkerFunction = false;
    bool isDebugFunction = false;
    bool isSyscall = false;

    FunctionNode *parentFunctionNode = nullptr;

    /**
     * If this call is a syscall, this field contains the corresponding syscall name, otherwise it is std::nullopt
     */
    std::optional<size_t> syscallId = std::nullopt;

    /**
     * Underlying call instruction of the CallNode
     */
    llvm::CallBase *call = nullptr;

    /**
     * Create a new CallNode from the given called function and the corresponding call instruction
     * @param calls Function that will be called
     * @param call Call instruction that will be converted to a CallNode
     */
    CallNode(llvm::Function *calls, llvm::CallBase *call, FunctionNode *parentFunctionNode);

    /**
     * Insert this CallNode to the given Node and rewrites the corresponding edges
     * @param belongingNode Node the underlying call instruction is located in
     * @param nodeList List of nodes this CallNode will be inserted in
     * @param edgeList List of edges this CallNode will be connected in
     */
    void collapseCalls(HLAC::Node *belongingNode, std::vector<std::unique_ptr<GenericNode>> &nodeList,
                       std::vector<std::unique_ptr<Edge>> &edgeList);

    /**
     * Create a new FunctionNode
     * @param function Function that will be called by the CallNode
     * @param instruction Call instruction the CallNode is based on
     * @return Returns new constructed FunctionNode
     */
    static std::unique_ptr<CallNode> makeNode(llvm::Function *function, llvm::CallBase *instruction,
                                              FunctionNode *parent);

    /**
     * Check whether an edge with the given source and destination exists
     * @param edgeList List of edges to search in
     * @param source Source node to search for
     * @param destination Destination node to search for
     * @return Returns true if an edge exists, false otherwise
     */
    static bool edgeExists(const std::vector<std::unique_ptr<Edge>> &edgeList, GenericNode *source,
                           GenericNode *destination);

    /**
     * Prints the FunctionNode as dot representation
     * @param os Outputstream to print to
     */
    void printDotRepresentation(std::ostream &os) override;

    void printDotRepresentationWithSolution(std::ostream &os, std::vector<double> result) override;

    /**
     * Get dot escaped name of the FunctionNode
     * @return String representation of the FunctionNode
     */
    std::string getDotName() override;

    /**
     * Check whether this CallNode represents a syscall by checking the called function
     * for the function "syscall" and for linker functions that are known to be syscall wrappers
     *
     * If the CallNode represents a syscall, the corresponding syscall id will be stored in the field syscallId
     *
     * @return true if this CallNode represents a syscall, false otherwise
     */
    bool checkIfIsSyscall();

    /**
     * Return the energy of the node
     * @return Energy as double value
     */
    double getEnergy() override;
};

/**
 * HLAC graph representation
 */
class hlac {
 public:
    /**
     * Phasar ResultRegistry that contains all results from the analyses we want to consider for the
     * construction of the HLAC
     */
    ResultRegistry registry;

    /**
     * Simple cache to store the energy of functions that we have already calculated to avoid redundant calculations
     */
    std::map<std::string, double> FunctionEnergyCache;

    /**
     * Create a new HLAC graph with the given ResultRegistry
     * @param registry Registry containing the results of the analyses we want to consider for
     * the construction of the HLAC
     */
    explicit hlac(ResultRegistry registry) : registry(std::move(registry)) {}

    /**
     * List of FunctioNodes contained within the HLAC
     */
    std::vector<std::unique_ptr<FunctionNode>> functions;

    /**
     * Create a new FunctioNode from the given parameters
     * @param function Function that we want to represent by the FunctionNode
     * @param fam FunctionAnalysisManager that drives our analysis
     */
    void makeFunction(llvm::Function *function, llvm::FunctionAnalysisManager *fam);

    /**
     * Print dot representation of the HLAC
     */
    void printDotRepresentation();

    void printDotRepresentationWithSolution(std::vector<double> result);

    /**
     * Return the energy of a given function
     * @param functionName Name of the function to analyze
     * @return Energy of the function under analysis
     */
    double getEnergyPerFunction(std::string functionName);

    /**
     * Return the energy of the contained functions
     * @return Mapping between function name and energy as double value
     */
    std::map<std::string, double> getEnergy();

    /**
     * Build an ILP representation for the contained functions
     * @return Returns mapping between function name and the constructed CoinPackedMatrix representing the ILP for the
     * function
     */
    std::map<std::string, CoinPackedMatrix> buildILPS();
};
}  // namespace HLAC


#endif  // SRC_SPEAR_HLAC_HLAC_H_
