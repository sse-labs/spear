#include "ProgramGraph.h"
#include "DeMangler.h"

//Create a Node by setting the parent property with the given ProgramGraph
Node::Node(ProgramGraph *parent, AnalysisStrategy::Strategy strategy) {
    this->parent = parent;
    this->strategy = strategy;
}

std::string Node::toString() {
    //Init the output-string
    std::string output;

    output.append(block->getName().str());

    //Return the string
    return output;
}

//Calculate the energy of this Node. Is capable of dealing with if-conditions
double Node::getNodeEnergy(LLVMHandler *handler) {
    //Init the result of the calculation
    double sum = 0.0;

    //Calculate the adjacent nodes of this node
    auto adjacentNodes = this->getAdjacentNodes();

    //If there are adjacent nodes...
    if(!adjacentNodes.empty()){
        //Find the smallest energy-value-path of all the adjacent nodes
        //Init the minimal pathvalue
        auto compare = 0.00;

        switch (this->strategy) {
            case AnalysisStrategy::WORSTCASE :
                 compare = DBL_MIN;

                //Iterate over the adjacent nodes
                for(auto node : adjacentNodes){
                    //Calculate the sum of the node
                    if(!node->isExceptionFollowUp()){
                        double locsum = node->getNodeEnergy(handler);

                        //Set the minimal energy value if the calculated energy is smaller than the current minimum
                        if (locsum > compare){
                            compare = locsum;
                        }
                    }
                }

                sum += compare;
                break;
            case AnalysisStrategy::BESTCASE :
                compare = DBL_MAX;

                //Iterate over the adjacent nodes
                for(auto node : adjacentNodes){
                    //Calculate the sum of the node
                    double locsum = node->getNodeEnergy(handler);

                    //Set the minimal energy value if the calculated energy is smaller than the current minimum
                    if(!node->isExceptionFollowUp()){
                        if (locsum < compare){
                            compare = locsum;
                        }
                    }
                }

                sum += compare;
                break;
            case AnalysisStrategy::AVERAGECASE :
                double locsum = 0.00;

                if(adjacentNodes.size() > 1){
                    double leftSum = adjacentNodes[0]->getNodeEnergy(handler);
                    double rightSum = adjacentNodes[1]->getNodeEnergy(handler);

                    if(handler->inefficient <= handler->efficient){
                        if(adjacentNodes[0]->isExceptionFollowUp()){
                            locsum += rightSum;
                        }else if(adjacentNodes[1]->isExceptionFollowUp()){
                            locsum += leftSum;
                        }else{
                            locsum += std::max(leftSum, rightSum);
                            handler->inefficient++;
                        }

                    }else{
                        if(adjacentNodes[0]->isExceptionFollowUp()){
                            locsum += rightSum;
                        }else if(adjacentNodes[1]->isExceptionFollowUp()){
                            locsum += leftSum;
                        }else{
                            locsum += std::min(leftSum, rightSum);
                            handler->efficient++;
                        }
                    }
                }else{
                    locsum = adjacentNodes[0]->getNodeEnergy(handler);
                }

/*                srand(time(nullptr));
                int randomIndex = rand() % adjacentNodes.size();
                double locsum = adjacentNodes[randomIndex]->getNodeEnergy(handler);
                compare = locsum;
                sum += compare;*/
                sum += locsum;

                break;
        }
    }

    auto nodename = this->block->getName();

    //Calculate the energy-cost of this node's basic blocks and add it to the sum
    double localEnergy = handler->getNodeSum(this);
    sum = sum + localEnergy;

    this->energy = localEnergy;

    //Return the calculated energy
    return sum;
}

//Calculate the adjacent Nodes
std::vector<Node *> Node::getAdjacentNodes() {
    //Init the vector
    std::vector<Node *> adjacent;

    //Get the edgdes starting at this node from the parent ProgramGraph
    for(auto edge : this->parent->findEdgesStartingAtNode(this)){
        //Add the end of the edge to the adjacent vector
        adjacent.push_back(edge->end);
    }

    //Return the adjacent nodes vector
    return adjacent;
}

bool Node::isExceptionFollowUp(){
    return this->block->isLandingPad();
}

double Node::getMaxEnergy() {
    double maxEng = 0.0;

    //Calculate the adjacent nodes of this node
    auto adjacentNodes = this->getAdjacentNodes();

    //If there are adjacent nodes...
    if(!adjacentNodes.empty()){
        //Find the smallest energy-value-path of all the adjacent nodes
        //Init the minimal pathvalue

        for(auto node : adjacentNodes){
            //Calculate the sum of the node
            if(!node->isExceptionFollowUp()){
                double locMaxEng = node->getMaxEnergy();

                //Set the minimal energy value if the calculated energy is smaller than the current minimum
                if (locMaxEng > maxEng){
                    maxEng = locMaxEng;
                }
            }
        }
    }

    //Calculate the energy-cost of this node's basic blocks and add it to the sum
    double localEnergy = this->energy;
    if(localEnergy > maxEng){
        maxEng = localEnergy;
    }

    //Return the calculated energy
    return maxEng;
}

json Node::getJsonRepresentation() {
    json nodeObject = json::object();
    if(block != nullptr){
        nodeObject["type"] = NodeType::NODE;
        nodeObject["name"] = block->getName().str();
        nodeObject["energy"] = energy;
        nodeObject["instructions"] = json::array();

        for(int k=0; k < instructions.size(); k++) {
            json instructionObject = json::object();
            InstructionElement Inst = instructions[k];

            instructionObject["opcode"] = Inst.inst->getOpcodeName();
            instructionObject["energy"] = Inst.energy;

            if(llvm::isa<llvm::CallInst>( Inst.inst ) || llvm::isa<llvm::CallBrInst>( Inst.inst )){
                auto calleeInst = llvm::cast<llvm::CallInst>(Inst.inst);
                //Get the called function
                auto *calledFunction = calleeInst->getCalledFunction();
                //Add the function to the list

                if(calledFunction != nullptr){
                    std::string functionName = DeMangler::demangle(calledFunction->getName().str());
                    instructionObject["calledFunction"] = calledFunction->getName().str();
                }else{
                    auto operand = calleeInst->getCalledOperand();
                    auto val = operand->stripPointerCasts();
                    if(val != nullptr){
                        auto ref = val->getName();
                        auto refname = ref.str();
                        instructionObject["calledFunction"] = refname;
                    }
                }

            }else if(llvm::isa<llvm::InvokeInst>( Inst.inst )){
                auto calleeInst = llvm::cast<llvm::InvokeInst>(Inst.inst);
                //Get the called function
                auto *calledFunction = calleeInst->getCalledFunction();
                //Add the function to the list
                if(calledFunction != nullptr){
                    std::string functionName = DeMangler::demangle(calledFunction->getName().str());
                    instructionObject["calledFunction"] = calledFunction->getName().str();
                }else{
                    auto operand = calleeInst->getCalledOperand();
                    auto val = operand->stripPointerCasts();
                    if(val != nullptr){
                        auto ref = val->getName();
                        auto refname = ref.str();
                        instructionObject["calledFunction"] = refname;
                    }
                }
            }

            json locationObj = json::object();
            const llvm::DebugLoc &dbl = Inst.inst->getDebugLoc();
            unsigned int line = -1;
            unsigned int col  = -1;
            std::string filename = "undefined";

            // Check if the debug information is present
            // If the instruction i.e. is inserted by the compiler no debug info is present
            if(dbl){
                line = dbl.getLine();
                col = dbl->getColumn();
                filename = dbl->getFile()->getDirectory().str() + "/" + dbl->getFile()->getFilename().str();
            }

            locationObj["line"] = line;
            locationObj["column"] = col;
            locationObj["file"] = filename;
            instructionObject["location"] = locationObj;
            nodeObject["instructions"][k] = instructionObject;
        }
    }

    return nodeObject;
}