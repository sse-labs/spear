#include "ProgramGraph.h"

//Creates a LoopNode with the given LoopTree and ProgramGraph
LoopNode::LoopNode(LoopTree *loopTree, ProgramGraph *parent, AnalysisStrategy::Strategy strategy) : Node(parent, strategy) {
    this->loopTree = loopTree;
}

//Get the string representation of this LoopNode
std::string LoopNode::toString() {
    //Init the output string
    std::string output;

    //Get the Address of this LoopNode
    const void* addr = static_cast<const void*>(this);
    std::stringstream convertStream;
    convertStream << addr;

    //Add the address to the output
    output.append("LOOPNODE ");
    output.append(convertStream.str());

    //Return output string
    return output;
}

//Test if this LoopNde has subgraphs
bool LoopNode::isLeafNode() const {
    return this->loopTree->subTrees.empty();
}

//Construct a LoopTree by recursively calling this method until we reach a leaf
LoopNode* LoopNode::construct(LoopTree *loopTree, ProgramGraph *parent, AnalysisStrategy::Strategy strategy) {
    //Create a Toplevel LoopNode
    auto *loopNode = new LoopNode(loopTree, parent, strategy);
    ProgramGraph *programGraph = new ProgramGraph();

    //End-condition
    if(loopNode->isLeafNode()){
        //Create a ProgramGraph from this LoopTrees mainloop
        ProgramGraph::construct(programGraph, loopNode->loopTree->mainloop->getBlocksVector(), strategy);

        //Add the ProgramGraph to the list of subgraphs
        loopNode->subgraphs.push_back(programGraph);
    }else{
        //Further recursion

        //Create a ProgramGraph from this LoopTrees mainloop
        ProgramGraph::construct(programGraph, loopNode->loopTree->mainloop->getBlocksVector(), strategy);
        //Add the ProgramGraph to the list of subgraphs
        loopNode->subgraphs.push_back(programGraph);

        //Iterate over the subloops of this LoopNodes LoopTree
        for(auto *subTree : loopNode->loopTree->subTrees){
            //Construct a LoopNode for the current Sub-LoopTree
            LoopNode *subLoopNode = LoopNode::construct(subTree, programGraph, strategy);

            //ProgramGraph *SPT = ProgramGraph::construct(subTree->mainloop->getBlocksVector());
            //subLoopNode->subgraphs.push_back(SPT);

            //Init the list of blocks contained in the subloop
            std::vector<std::string> allblocks;

            //Add the subloops blocks to the list of contained blocks
            for(auto subBlock : subTree->mainloop->getBlocksVector()){
                allblocks.push_back(subBlock->getName().str());
            }

            //LoopNode *subLN = LoopNode::construct(&subTree);

            //Replace the nodes in the sub-ProgramGraph
            programGraph->replaceNodesWithLoopNode(subTree->mainloop->getBlocksVector(), subLoopNode);
        }
    }

    //Remove the loop-edges in this LoopNode, so we won't create infinite recursions
    loopNode->removeLoopEdgesFromSubGraphs();

    //Return the LoopNode
    return loopNode;
}

//Get the LoopNodes energy
double LoopNode::getNodeEnergy(LLVMHandler *handler) {
    //Init the calculation result
    double sum = 0.0;
    //Calculate the adjacent nodes
    auto adjacentNodes = this->getAdjacentNodes();

    //Get the energy from all contained subgraphs
    for(auto subTrees : this->subgraphs){
        sum += subTrees->getEnergy(handler);
    }

    //Multiply the calculated energy from the subgraphs by the iterations of this LoopNode's loop

    switch (this->strategy) {
        case AnalysisStrategy::WORSTCASE :
                sum = (double) this->loopTree->iterations * sum;
            break;
        case AnalysisStrategy::BESTCASE :
                sum = (double) this->loopTree->iterations * sum;
            break;
        case AnalysisStrategy::AVERAGECASE :
                sum = (double) this->loopTree->iterations * sum;
            break;
    }

    //Handle if-conditions contained in this LoopNode, if we're dealing with a leaf-Node
    if(!adjacentNodes.empty()){


        //Iterate over the adjacent nodes
        for(auto node : adjacentNodes){
            //Calculate the sum of the node
            auto nodename = node->block->getName();

            sum += node->getNodeEnergy(handler);

        }
    }


    //Return the calculation result
    return sum;
}


//Remove the loop-edges from the LoopNode
void LoopNode::removeLoopEdgesFromSubGraphs(){
    //Iterate over the ProgramTrees contained in this LoopNode
    for(auto subgraph : this->subgraphs){
        //Get the BasicBlock used as latch in this ProgramTrees LoopTree
        auto *latchblock = this->loopTree->mainloop->getLoopLatch();
        //Get the Node the latchblock is contained in
        auto *latchnode = subgraph->findBlock(latchblock);
        auto lnname = latchblock->getName();
        //Init the list of edges we want to keep
        std::vector<Edge *> tempedges;

        //Iterate over the edges contained in the Sub-ProgramGraph
        for(auto edge : subgraph->edges){
            //If the start node is not the latch of the loop, add it to the list of edges we want to keep
            if(edge->start != latchnode){
                tempedges.push_back(edge);
            }
        }

        //Set the edges-list of the Sub-ProgramGraph to the calculated list
        subgraph->edges = tempedges;

        //If we have further LoopNodes contained in this LoopNode, remove their loopedges too
        if(subgraph->containsLoopNodes()){
            auto subLoopNodes = subgraph->getLoopNodes();
            for(auto subLoopNode : subLoopNodes){
                subLoopNode->removeLoopEdgesFromSubGraphs();
            }
        }
    }
}

LoopNode::~LoopNode() {
    for (auto subtree : this->subgraphs) {
        delete subtree;
    }
}

bool LoopNode::isExceptionFollowUp(){
    return false;
}

double LoopNode::getMaxEnergy() {
    double maxEnergy = 0.0;
    auto adjacentNodes = this->getAdjacentNodes();

    //Get the energy from all contained subgraphs
    for(auto subTrees : this->subgraphs){
        double maxEnergyOfSubtree = subTrees->findMaxEnergy();
        if(maxEnergyOfSubtree > maxEnergy){
            maxEnergy = maxEnergyOfSubtree;
        }
    }

    //Multiply the calculated energy from the subgraphs by the iterations of this LoopNode's loop

    maxEnergy = (double) this->loopTree->iterations * maxEnergy;

    //Handle if-conditions contained in this LoopNode, if we're dealing with a leaf-Node
    if(!adjacentNodes.empty()){

        //Iterate over the adjacent nodes
        for(auto node : adjacentNodes){
            //Calculate the sum of the node
            double maxEnergyOfAdjNode = node->getMaxEnergy();
            if(maxEnergyOfAdjNode > maxEnergy){
                maxEnergy = maxEnergyOfAdjNode;
            }

        }
    }


    //Return the calculation result
    return maxEnergy;
}


json LoopNode::getJsonRepresentation() {
    json nodeObject;

    nodeObject["type"] = NodeType::LOOPNODE;
    nodeObject["name"] = "";
    nodeObject["repetitions"] = loopTree->iterations;
    nodeObject["subgraphs"] = json::array();

    for(int i = 0; i < this->subgraphs.size(); i++){
        json subGraphObject;
        subGraphObject = this->subgraphs[i]->populateJsonRepresentation(subGraphObject);

        nodeObject["subgraphs"][i] = subGraphObject;
    }

    return nodeObject;
}