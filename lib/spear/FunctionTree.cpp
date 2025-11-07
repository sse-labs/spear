
#include <llvm/IR/Instructions.h>
#include "FunctionTree.h"

//Constructor
FunctionTree::FunctionTree(llvm::Function *func) {
    //Set the properties
    this->func = func;
    this->name = func->getName().str();
}

//Construct a Functiontree from a given function
FunctionTree* FunctionTree::construct(llvm::Function *func) {
    //Init the tree
    auto *functionTree = new FunctionTree(func);
    //Get the called functions, so we can generate the subgraphs
    auto calls = functionTree->getCalledFunctions();

    //If we call functions, we are not in a leaf...
    if(!calls.empty()){
        //Iterate over the calls
        for (auto &function : calls) {
            //If we have a function that isn't the function we started at...
            if(function != functionTree->func){
                if(function != nullptr){
                    //Call construct recursively, as there might be further calls in the called functions
                    auto subFunctionTree = construct(function);
                    //Add the constructed tree to the subgraphs list
                    functionTree->subtrees.push_back(subFunctionTree);
                }
            }
        }
    }

    //Return the constructed FunctionTree
    return functionTree;
}

//Calculate the functions called from the Function saved in this FunctionTree
std::vector<llvm::Function *> FunctionTree::getCalledFunctions() const {
    //Init the vector
    std::vector<llvm::Function *> functions;

    //Iterate over the BasicBlocks of the function
    for (auto &basicBlock : *this->func) {
        //Iterate over the instructions of the function
        for( auto &instruction : basicBlock){
            //If we find an instruction, that is a call-instruction
            if(llvm::isa<llvm::CallInst>( instruction ) || llvm::isa<llvm::CallBrInst>( instruction )){
                auto calleeInst = llvm::cast<llvm::CallInst>(&instruction);
                //Get the called function
                auto *calledFunction = calleeInst->getCalledFunction();

                if(calledFunction != nullptr){
                    //Add the function to the list
                    functions.push_back(calledFunction);
                }else{
                    // If the called functions a zero, the function musst be indirect...

                    /*auto operand = calleeInst->getCalledOperand();
                    auto val = operand->stripPointerCasts();
                    auto ref = val->getName();
                    auto refname = ref.str();*/
                    //TODO Here, of course, it would be necessary to analyze the indirect function. However, there is currently no proper way of reliably accessing the object here...
                }

            }else if(llvm::isa<llvm::InvokeInst>( instruction )){
                auto calleeInst = llvm::cast<llvm::InvokeInst>(&instruction);
                //Get the called function
                auto *calledFunction = calleeInst->getCalledFunction();
                //Add the function to the list
                if(calledFunction != nullptr){
                    //Add the function to the list
                    functions.push_back(calledFunction);
                }else{
                    // If the called functions a zero, the function musst be indirect...

                    /*auto operand = calleeInst->getCalledOperand();
                    auto val = operand->stripPointerCasts();
                    auto ref = val->getName();
                    auto refname = ref.str();*/

                    //TODO Here, of course, it would be necessary to analyze the indirect function. However, there is currently no proper way of reliably accessing the object here...
                }
            }
        }
    }

    //Return the calculated list
    return functions;
}

//Print the tree in pre-order
void FunctionTree::printPreorder() {
    if(this->subtrees.empty()){
        llvm::outs() << "------------Leaf-----------\n";
        llvm::outs() << "Node " << this->func->getName() << "\n";
    }else{
        llvm::outs() << "============================================\n";
        for (auto subFunctionTree : this->subtrees) {
            subFunctionTree->printPreorder();
        }
        llvm::outs() << "------------Node-----------\n";
        llvm::outs() << "Node " << this->func->getName() << "\n";
        llvm::outs() << "============================================\n\n\n\n";
    }

}

//Get the tree as list in pre-order
std::vector<llvm::Function *> FunctionTree::getPreOrderVector() {
    //init the functionList
    std::vector<llvm::Function *> functionList;

    //Test if we are in a leaf
    if(this->subtrees.empty()){
        //If we are in a leaf, add this function to the functionList. No further recursion will be taken
        functionList.push_back(this->func);
    }else{
        //If we have subTrees, iterate over them
        for (auto subFunctionTree : this->subtrees) {
            //Recursivly call this method on the current subtree
            auto subFunctionTreePreOrder = subFunctionTree->getPreOrderVector();

            //Check if the function is already in the functionList, if so don't add it to the functionList
            for(auto function : subFunctionTreePreOrder){
                if(std::find(functionList.begin(), functionList.end(), function) == functionList.end()){
                    functionList.push_back(function);
                }
            }
        }
        //Add the current function to the functionList
        functionList.push_back(this->func);
    }

    //Return the functionList
    return functionList;
}


