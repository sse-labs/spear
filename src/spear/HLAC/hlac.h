/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_HLAC_HLAC_H_
#define SRC_SPEAR_HLAC_HLAC_H_

#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/BasicBlock.h>

#include <catch2/internal/catch_unique_ptr.hpp>
#include <llvm/Analysis/LazyCallGraph.h>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ILP/ILPBuilder.h"
#include "ILP/ILPTypes.h"
#include "analyses/ResultRegistry.h"

namespace HLAC {


/**
 * Forward declarations of Function and Generic nodes
 */
class hlac;
class FunctionNode;
class GenericNode;
class LoopNode;


enum class NodeType {
    UNDEFINED = 999,
    NODE = 0,
    LOOPNODE = 1,
    CALLNODE = 2,
    FUNCTIONNODE = 3,
    VIRTUALNODE = 4,
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

    std::string hash;

    NodeType nodeType = NodeType::UNDEFINED;

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

    virtual std::string calculateHash() { return "NOT IMPLEMENTED"; }

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

    /**
     * Print the virtual node with information about the solution
     * @param os Outputstream to print to
     * @param result Result to consider during printing
     */
    void printDotRepresentationWithSolution(std::ostream &os, std::vector<double> result) override;

    /**
     * Get the name of the NormalNode escaped for dot
     * @return Name of the node as escaped dot string
     */
    std::string getDotName() override;

    std::string calculateHash() override;
};

/**
 * Edge class that represents connections inside the HLAC
 */
class Edge {
 public:
    /**
     * Global unique identifier of this edge
     */
    int ilpIndex = -1;

    /**
     * Local string identifier used for dot printing
     */
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

    /**
     * Print the edge with information about the solution
     * @param os Outputstream to print to
     * @param result Result to consider during printing
     */
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

    /**
     * Print the node with information about the solution
     * @param os Outputstream to print to
     * @param result Result to consider during printing
     */
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

    std::string calculateHash() override;
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

    /**
     * Parent FunctionNode this LoopNode belongs to
     */
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

    /**
     * Print the loopnode with information about the solution
     * @param os Outputstream to print to
     * @param result Result to consider during printing
     */
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

    std::string calculateHash() override;
};

/**
 * FunctionNode that represents functions referenced by the program under analysis
 */
class FunctionNode : public GenericNode {
 public:
    /**
     * Phasar result registry
     */
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
     * Represents the nodes of this FunctioNode as vector in topological order
     */
    std::vector<GenericNode *> topologicalSortedRepresentationOfNodes;

    /**
     * Representation of the function node as mapping of node index to list of adjacent edges
     */
    std::vector<std::vector<HLAC::Edge *>> adjacencyRepresentation;

    /**
     * Internal representation to map node index to precalculated energy values
     */
    std::vector<double> nodeEnergy;

    /**
     * Internal mapping that maps a GenericNode object to the respective index in the Nodes vector
     */
    std::unordered_map<GenericNode*, std::size_t> nodeLookup;

    /**
     * Function represented by the FunctionNode
     */
    llvm::Function *function = nullptr;

    /**
     * Reference to the HLAC this FunctionNode belongs to
     */
    HLAC::hlac *parentGraph = nullptr;

    /**
     * Index of the node representing the entry of the function
     */
    int entryIndex = 0;

    /**
     * Index of the node representing the exit of the function
     */
    int exitIndex = 0;

    /**
     * Meta flags to distinguish different kinds of function
     */
    bool isMainFunction = false;
    bool isDebugFunction = false;
    bool isLinkerFunction = false;

    bool isRecursive = false;

    struct CallNodeBinding {
        std::size_t nodeIndex;
        std::string calleeName;
    };

    std::vector<double> baseNodeEnergy;
    std::vector<CallNodeBinding> callNodeBindings;
    bool baseNodeEnergyInitialized = false;

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

    /**
     * Print the function node with information about the solution
     * @param os Outputstream to print to
     * @param result Result to consider during printing
     */
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

    std::string calculateHash() override;

    bool isFunctionRecursive(llvm::LazyCallGraph &lazyCallGraph);

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

    /**
     * Create a vector of the contained nodes in topological order
     * @return Vector representing the topological order of the contained nodes
     */
    std::vector<GenericNode *> getTopologicalOrdering();
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

    /**
     * Print the call node with information about the solution
     * @param os Outputstream to print to
     * @param result Result to consider during printing
     */
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

    std::string calculateHash() override;
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

    llvm::LazyCallGraph lazyCallGraph;

    /**
     * Simple cache to store the energy of functions that we have already calculated to avoid redundant calculations
     */
    std::map<std::string, double> FunctionEnergyCache;

    /**
     * Create a new HLAC graph with the given ResultRegistry
     * @param registry Registry containing the results of the analyses we want to consider for
     * the construction of the HLAC
     */
    explicit hlac(ResultRegistry registry, llvm::LazyCallGraph &lcg) : registry(std::move(registry)), lazyCallGraph(std::move(lcg)) {}

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
     * Print the DOT representation of the HLAC graph.
     *
     * This function generates a Graphviz DOT description of the current HLAC,
     * including all nodes and edges, without any analysis or optimization results.
     * It can be used to visualize the raw structure of the graph.
     */
    void printDotRepresentation();

    /**
     * Print the DOT representation of the HLAC graph annotated with an ILP solution.
     *
     * This variant expects a vector of doubles representing the ILP solution,
     * where each entry corresponds to an edge variable (e.g., execution count).
     * Edges that are part of the solution (e.g., with value > 0) are highlighted
     * in the generated DOT output.
     *
     * @param FN The function node whose HLAC graph should be printed
     * @param result ILP solution vector (edge execution counts)
     * @param appendName Optional suffix added to the output file name
     */
    void printDotRepresentationWithSolution(FunctionNode *FN, std::vector<double> result, std::string appendName);

    /**
     * Print the DOT representation of the HLAC graph annotated with a selected edge set.
     *
     * This variant expects a list of edges that are part of the final solution
     * (e.g., the extracted longest path or selected ILP edges). Only these edges
     * are highlighted in the visualization.
     *
     * @param FN The function node whose HLAC graph should be printed
     * @param result Vector of edges that should be emphasized in the output
     * @param appendName Optional suffix added to the output file name
     */
    void printDotRepresentationWithSolution(FunctionNode *FN, std::vector<Edge *> result, std::string appendName);

    /**
     * Return the energy of a given function
     * @param functionName Name of the function to analyze
     * @param isRecursive Flag if we are calling recursively
     * @return Energy of the function under analysis
     */
    double getEnergyPerFunction(std::string functionName, bool isRecursive);

    /**
     * Return the energy of the contained functions
     * @return Mapping between function name and energy as double value
     */
    std::map<std::string, double> getEnergy();

    /**
     * Query a pointer to a FunctionNode contained in the graph by name
     * @param name Name of the function to look for
     * @return Pointer to the found FunctionNode, nullptr otherwise
     */
    FunctionNode *getFunctionByName(std::string name);

    /**
     * Calculate the longest paths of each function contained inside the HLAC
     * @param clusteredResult Mapping of functions to -> (LoopNode -> Clustered ILP Result)
     * @return Mapping of function names to their respective solution of the DAG longest path search
     */
    std::optional<DAGLongestPathSolution> DAGLongestPath(FunctionNode *functionNode, std::unordered_map<LoopNode *, ILPResult> clusteredResult);

    /**
     * Build an ILP representation for the contained functions
     * @return Returns mapping between function name and the constructed CoinPackedMatrix representing
     * the ILP for the function
     */
    std::optional<ILPModel> buildMonolithicILP(FunctionNode *functionNode);

    /**
     * Build clustered ILP representations for the contained functions
     * @return Returns mapping of functionname to
     */
    std::optional<ClusteredILPModel> buildClusteredILPS(FunctionNode *functionNode);

    /**
     * Solve the monolithic models of the contained functions
     * @param modelMapping Mapping function name to constructed monolithic ILPModel
     * @return Mapping of function name to monolithic ILP result
     */
    std::optional<ILPResult> solveMonolithicIlp(ILPModel &model);

    /**
     * Solve the clustered models of the contained functions
     * @param modelMapping Mapping function name to constructed clustered ILPModel
     * @return Mapping of function name to clustered ILP result
     */
    ILPClusteredLoopResult solveClusteredIlps(ILPLoopModelMapping loopModelMapping);
};
}  // namespace HLAC


#endif  // SRC_SPEAR_HLAC_HLAC_H_
