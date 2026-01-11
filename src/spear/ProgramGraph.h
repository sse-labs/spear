/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_PROGRAMGRAPH_H_
#define SRC_SPEAR_PROGRAMGRAPH_H_


#include <vector>
#include <string>
#include <utility>
#include <map>
#include "LoopTree.h"
#include "AnalysisStrategy.h"
#include "llvm/IR/BasicBlock.h"


using DomainVal = psr::IDELinearConstantAnalysisDomain::l_t;

using BoundVarMap =
    std::map<std::string, std::pair<const llvm::Value *, DomainVal>>;


// Pre-declaration of the ProgramGraph Class
class ProgramGraph;


/**
 * Defines a instruction with attached energy value
 * 
 */
class InstructionElement {
 public:
    double energy;
    llvm::Instruction* inst;

    /**
     * Construct a new Instruction Element object
     * 
     * @param instruction Instruction the element belongs to
     */
    explicit InstructionElement(llvm::Instruction* instruction);
};

/**
 * Enum to distinguish node types 
 * 
 */
enum NodeType {
    UNDEFINED,
    NODE,
    LOOPNODE
};

/**
 * Node - A Node in the Programtree. Represents a set of BasicBlocks, which may contain if-statements but is loop-free
 */
class Node {
 public:
    /**
     * Reference to the Tree this node is contained in
     */
    ProgramGraph *parent;

    /**
     * A vector savint references to the blocks in this node
     */
    llvm::BasicBlock * block;

    double energy;

    /**
     * The strategy the analysis should follow
     */
    AnalysisStrategy::Strategy strategy;

    std::vector<InstructionElement> instructions;

    /**
     * Constructor taking the surrounding ProgramGraph
     * @param parent
     */
    Node(ProgramGraph *parent, AnalysisStrategy::Strategy strategy);

    /**
     * Method for converting this node to string for debug output
     * Made virtual so further node specializations can be made
     * @return Returns this node a string
     */
    virtual std::string toString();

    /**
     * Method for calculating this Nodes energy consumption based on the given LLVMHandler
     * Made virtual, as calculation can differ for the specific node-type
     * @param handler A Reference to a LLVMHandler for calculating the energy usage
     * @return Returns the esitmated energy for this Node as double
     */
    virtual double getNodeEnergy(LLVMHandler *handler);

    virtual double getMaxEnergy();

    virtual bool isExceptionFollowUp();

    virtual json getJsonRepresentation();

    std::string getSourceVarName(llvm::Value *V, llvm::Instruction *Ctx);

    bool evalICMP(llvm::ICmpInst *ICmp, llvm::ConstantInt *left, llvm::ConstantInt *right);

    llvm::BasicBlock* getPathName(const llvm::BranchInst *br, bool conditionalresult);

    const DomainVal* findDeducedValue(BoundVarMap *resultsAtBlock, std::string varname);

 protected:
    /**
     * Calculates the adjacent Nodes extending through vertices in the parent ProgramGraph from this Node outwards
     * @return Returns a vector of references to the adjacent nodes
     */
    std::vector<Node *> getAdjacentNodes();
};



/**
 * Edge - Class to represent the connection between two nodes. The connection is not directional by definition.
 */
class Edge {
 public:
    /**
     * Reference to the starting node of the edge
     */
    Node * start;

    /**
     * Reference to the ending node of the edge
     */
    Node * end;

    /**
     * Constructor taking a start-node and an end-node
     * @param start Node, the edges starts on
     * @param end Node, the edge ends on
     */
    Edge(Node *start, Node *end);

    /**
     * Method for converting the edge to a string representation
     * @return
     */
    [[nodiscard]] std::string toString() const;
};



/**
 * LoopNode - A specialization of the Node to represent loops extracted from the program. LoopNodes may contain further
 * ProgramTrees representing a recursive structure
 */
class LoopNode : public Node {
 public:
    /**
     * Reference to a LoopTree object storing the information of the loop represented by this LoopNode
     */
    LoopTree *loopTree;

    /**
     * Vector storing the references to the subgraphs encapsulated by this LoopNode
     */
    std::vector<ProgramGraph *> subgraphs;

    /**
     * Constructor. Creates a LoopNode with the given LoopTree and the surrounding ProgramGraph
     * @param loopTree Reference to the LoopTree for the loop to represent
     * @param parent The parent ProgramGraph, this LoopNode is contained in
     */
    LoopNode(LoopTree *loopTree, ProgramGraph *parent, AnalysisStrategy::Strategy strategy);

    /**
     * Static Method for constructing a LoopNode recursivly trough the given LoopTree and the parent ProgramGraph
     * @param loopTree LoopTree to construct the LoopNode for
     * @param parent ProgramGraph the LoopNode should be contained in
     * @return Returns a reference to the constructed LoopNode
     */
    static LoopNode* construct(LoopTree *loopTree, ProgramGraph *parent, AnalysisStrategy::Strategy strategy);

    /**
     * Method for determining if the current LoopNode is a "Leaf".
     * A leaf-LoopNode does not contain any further loops
     * @return Returns true if this LoopNode does not contain any further loops. False if otherwise
     */
    [[nodiscard]] bool isLeafNode() const;

    /**
     * Method for calculating the energy of this LoopNode. Calculates the energy with respect to all contained subloops.
     * Multiplies the contained energy, by the iterations of this nodes loop.
     * Overrides the inherited getNodeEnergy method
     * @param handler A LLVMHandler used for energy calculation
     * @return Returns the calculated energy as double
     */
    double getNodeEnergy(LLVMHandler *handler) override;

    double getMaxEnergy() override;

    /**
     * Method for breaking cycles in the subgraphs of this LoopNode.
     * Prevents infity-calculations while dealing with recursion.
     */
    void removeLoopEdgesFromSubGraphs();

    /**
     * Method for representing the LoopNode as string.
     * Overrides the inherited method for better representing the contained programtrees
     * @return Returns a string representing the LoopNode
     */
    std::string toString() override;

    /**
     * LoopNodes Destructor
     */
    ~LoopNode();

    bool isExceptionFollowUp() override;

    json getJsonRepresentation() override;
};



/**
 * ProgramGraph -  Class to (partly) represent a program as a graph
 */
class ProgramGraph {
 public:
    /**
     * Vector containing references to the nodes contained in the graph
     */
    std::vector<Node *> nodes;

    /**
     * Vector containing references to the edges of the graph
     */
    std::vector<Edge *> edges;

    double maxEnergy;

    /**
     * Static method for creating a ProgramGraph from a given set of BasicBlocks
     * @param blockset Vector with references to a set of basic blocks
     * @return Returns the constructed ProgramGraph
     */
    static void construct(
        ProgramGraph* pGraph,
        const std::vector<llvm::BasicBlock *>& blockset,
        AnalysisStrategy::Strategy strategy);

    /**
     * ProgramGraph destructor
     */
    ~ProgramGraph();

    /**
     * Method for printing the string representations of the contained nodes with their calculated energy
     * @param handler A LLVMHandler used for energy calculation
     */
    void printNodes(LLVMHandler *handler);

    /**
     * Prints the ProgramGraph in the Graphviz dot format recursivly
     * @return Returns the string representation of the graph in the dot format
     */
    std::string printDotRepresentation();

    /**
     * Returns a vector of pointers to the nodes of this ProgramGraph
     * @return Vector containing pointers to the nodes of the graph
     */
    std::vector<Node*> getNodes();

    /**
     * Method for printing the string representations of the graphs edges
     */
    void printEdges();

    /**
     * Method for getting the Node contained in this ProgramGraph, which holds the given BasicBlock
     * @param basicBlock A reference to the BasicBlock to find
     * @return A reference to the Node if it was found. Returns a null pointer otherwise.
     */
    Node *findBlock(llvm::BasicBlock *basicBlock);

    /**
     * Calculates the edges going outwards from the given node.
     * @param sourceNode A reference to the Node the edges are extending from
     * @return Returns a vector of references to the edges
     */
    std::vector<Edge *> findEdgesStartingAtNode(Node *sourceNode);

    /**
     * Removes the given Node from this ProgramTre
     * @param nodeToRemove Reference to the Node to remove
     */
    void removeNode(Node *nodeToRemove);

    /**
     * Method for removing all edges from the graph, which start node or end node got removed
     */
    void removeOrphanedEdges();

    /**
     * Method for replacing the nodes in the ProgramGraph contained by a loopNode.
     * @param blocks Vector of BasicBlocks to replace by the given LoopNode
     * @param loopNode LoopNode used for replacing
     */
    void replaceNodesWithLoopNode(const std::vector<llvm::BasicBlock *>& blocks, LoopNode *loopNode);

    /**
     * Method for calculating the energy of this ProgramGraph. Uses the getNodeEnergy() methods of the contained nodes
     * @param handler LLVMHandler used to the get the values for the basic blocks
     * @return Returns the used energy as double
     */
    double getEnergy(LLVMHandler *handler);

    /**
     * Calculates the LoopNodes contained in this ProgramGraph
     * @return Returns a Vector of references to the contained LoopNodes.
     */
    std::vector<LoopNode *> getLoopNodes();

    /**
     * Calculates if the ProgramGraph contains LoopNodes
     * @return Returns true if the ProgramGraph contains LoopNodes. False if otherwise
     */
    bool containsLoopNodes();

    double findMaxEnergy();

    std::string getNodeColor(Node *node, double maxEng);
    std::string getNodeColor(double nodeEnergy, double maxEng);

    json populateJsonRepresentation(json functionObject);
};


#endif  // SRC_SPEAR_PROGRAMGRAPH_H_
