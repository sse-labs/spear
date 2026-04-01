/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "LegacyAnalysis.h"

#include <llvm/IR/Dominators.h>

#include "ConfigParser.h"
#include "DeMangler.h"
#include "EnergyFunction.h"
#include "LLVMHandler.h"
#include "Logger.h"
#include "PassUtil.h"
#include "ProfileHandler.h"

nlohmann::json LegacyAnalysis::run(
    llvm::FunctionAnalysisManager &FAM,
    FunctionTree *functionTree, bool showTimings) {
    Logger::getInstance().log("Running Legacy Analysis for Energy", LOGLEVEL::INFO);

    if (functionTree != nullptr) {
        std::vector<llvm::StringRef> names;
        for (auto function : functionTree->getPreOrderVector()) {
            names.push_back(function->getName());
        }

        const auto &preOrder = functionTree->getPreOrderVector();
        std::vector<EnergyFunction> funcPool(preOrder.size());

        for (int i = 0; i < functionTree->getPreOrderVector().size(); i++) {
            // Construct a new EnergyFunction to the current function
            // auto newFuntion = new EnergyFunction(function);
            llvm::Function *function = functionTree->getPreOrderVector()[i];

            // Add the EnergyFunction to the queue
            // handler.funcqueue.push_back(newFuntion);
            // auto energyFunction =
            // handler.funcmap.at(function->getName().str());

            funcPool[i].func = function;
            funcPool[i].name = DeMangler::demangle(function->getName().str());
        }

        // Init the LLVMHandler with the given model and the upper bound for
        // unbounded loops

        bool deepCallsEnabled = ConfigParser::getAnalysisConfiguration().legacyconfig.deepcalls;

        LLVMHandler handler = LLVMHandler(ProfileHandler::get_instance().getProfile(), deepCallsEnabled, funcPool.data(),
                                          functionTree->getPreOrderVector().size());

        for (int i = 0; i < functionTree->getPreOrderVector().size(); i++) {
            llvm::Function *function = functionTree->getPreOrderVector()[i];

            // Check if the current function is external. Analysis of external
            // functions, that only were declared, will result in an infinite loop
            if (!function->isDeclarationForLinker()) {
                // Calculate the energy
                constructProgramRepresentation(funcPool[i].programGraph, &funcPool[i], &handler,
                                               &FAM, AnalysisStrategy::WORSTCASE);
                //  Calculate the maximal amount of energy of the programgraph
            } else {
                funcPool[i].programGraph = nullptr;
            }
        }

        // this->stopwatch_end = std::chrono::steady_clock::now();
        // std::chrono::duration<double, std::milli> ms_double = this->stopwatch_end - this->stopwatch_start;

        //double duration = ms_double.count() / 1000;

        return json::object();
    } else {
        Logger::getInstance().log("No function tree provided for legacy analysis!", LOGLEVEL::ERROR);
        return json::object();
    }
}

/**
 * Calculates ProgramGraph-representation of a function
 * @param energyFunc Function to construct the graph for
 * @param handler A LLVMHandler containing the energy-Model
 * @param FAM A llvm::FunctionAnalysisManager
 * @param analysisStrategy The strategy to analyze the function with
 * @return Returns the calculated ProgramGraph
 */
void LegacyAnalysis::constructProgramRepresentation(ProgramGraph *pGraph, EnergyFunction *energyFunc, LLVMHandler *handler,
                                           llvm::FunctionAnalysisManager *FAM,
                                           AnalysisStrategy::Strategy analysisStrategy) {
    auto* domtree = new llvm::DominatorTree();
    llvm::Function* function = energyFunc->func;

    domtree->recalculate(*function);

    auto &loopAnalysis = FAM->getResult<llvm::LoopAnalysis>(*function);
    auto &scalarEvolution = FAM->getResult<llvm::ScalarEvolutionAnalysis>(*function);


    //Init a vector of references to BasicBlocks for all BBs in the function
    std::vector<llvm::BasicBlock *> functionBlocks;
    for(auto &blocks : *function){
        functionBlocks.push_back(&blocks);
    }

    //Create the ProgramGraph for the BBs present in the current function
    ProgramGraph::construct(pGraph, functionBlocks, analysisStrategy);

    //Get the vector of Top-Level loops present in the program
    auto loops = loopAnalysis.getTopLevelLoops();

    //We need to distinguish if the function contains loops
    if(!loops.empty()){
        //If the function contains loops

        //Iterate over the top-level loops
        for (auto liiter = loops.begin(); liiter < loops.end(); ++liiter) {
            //Get the loop, the iterator points to
            auto topLoop= *liiter;

            //Construct the LoopTree from the Information of the current top-level loop
            LoopTree *LT = new LoopTree(topLoop, topLoop->getSubLoops(), handler, &scalarEvolution);

            //Construct a LoopNode for the current loop
            LoopNode *loopNode = LoopNode::construct(LT, pGraph, analysisStrategy);
            //Replace the blocks used by loop in the previous created ProgramGraph
            pGraph->replaceNodesWithLoopNode(topLoop->getBlocksVector(), loopNode);
        }

        //energyCalculation(pGraph, handler, function);
        //energyFunc->energy = pGraph->getEnergy(handler);
        // Print only the currently available ProgramGraph pgraph to std::cout
        if (pGraph == nullptr) {
            std::cerr << "Error: ProgramGraph is null, nothing to print.\n";
            return;
        }

        std::cout << "digraph SPEARGRAPH {\n";
        std::cout << "compound=true;\n";
        std::cout << "rankdir=\"TB\";\n";
        std::cout << "nodesep=1.5;\n";
        std::cout << "ranksep=1.5;\n";
        std::cout << "linelength=30;\n";
        std::cout << "graph[fontname=Arial]\n";
        std::cout << "node[fontname=Arial, shape=\"rect\"]\n";
        std::cout << "edge[fontname=Arial]\n";

        // Compute energy for labeling
        double maxEnergy = 0.0;

        // Wrap in a single cluster
        std::cout << "subgraph cluster_pgraph {\n";
        std::cout << "rank=\"same\"\n";
        std::cout << "margin=40\n";
        std::cout << "bgcolor=white\n";
        std::cout << "cluster=true\n";
        std::cout << "\tlabel=<<b>ProgramGraph</b><br/>" << maxEnergy << " J>\n";

        // Print the actual graph content
        std::cout << pGraph->printDotRepresentation();

        std::cout << "}\n";

        // Optional: scale legend
        std::cout << "subgraph scale {\n";
        std::cout << "scale_image [label=\"\" shape=none image=\"/usr/share/spear/scale.png\"];\n";
        std::cout << "margin=40\n";
        std::cout << "bgcolor=white\n";
        std::cout << "}\n";

        std::cout << "}\n";

        Logger::getInstance().log(
            "Legacy Energy of " + energyFunc->name + ": " + formatScientific(energyFunc->energy) + " J",
            LOGLEVEL::HIGHLIGHT
        );
    }else{
        //energyCalculation(pGraph, handler, function);
        energyFunc->energy = pGraph->getEnergy(handler);
        Logger::getInstance().log(
            "Legacy Energy of " + energyFunc->name + ": " + formatScientific(energyFunc->energy) + " J",
            LOGLEVEL::HIGHLIGHT
        );
    }
    delete domtree;
}
