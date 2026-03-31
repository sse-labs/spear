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
#include "ProfileHandler.h"

nlohmann::json LegacyAnalysis::run(
    llvm::FunctionAnalysisManager &FAM,
    FunctionTree *functionTree, bool showTimings) {
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
    auto *domtree = new llvm::DominatorTree();
    llvm::Function *function = energyFunc->func;
    domtree->recalculate(*function);

    // Always create a local LoopInfo from the freshly computed DomTree.
    // This avoids using stale Loop* pointers across IR changes / analysis
    // invalidation.
    llvm::LoopInfo localLI(*domtree);

    // If you still want SCEV, keep it optional; LoopInfo should be local.
    llvm::ScalarEvolution *scevPtr = nullptr;
    if (FAM) {
        // SCEV may assert if not registered; keep optional.
        // If this asserts in your setup, set scevPtr = nullptr unconditionally.
        scevPtr = &FAM->getResult<llvm::ScalarEvolutionAnalysis>(*function);
    }

    // Init a vector of references to BasicBlocks for all BBs in the function
    std::vector<llvm::BasicBlock *> functionBlocks;
    for (auto &blocks : *function) {
        functionBlocks.push_back(&blocks);
    }

    // Create the ProgramGraph for the BBs present in the current function
    ProgramGraph::construct(pGraph, functionBlocks, analysisStrategy);

    // Get the vector of Top-Level loops present in the program (LOCAL)
    auto loops = localLI.getTopLevelLoops();

    // We need to distinguish if the function contains loops
    if (!loops.empty()) {
        for (auto liiter = loops.begin(); liiter < loops.end(); ++liiter) {
            llvm::Loop *topLoop = *liiter;

            // Hard guards against bad/dangling loops (prevents EXC_BAD_ACCESS in
            // getExitingBlocks etc.)
            if (!topLoop) {
                continue;
            }
            llvm::BasicBlock *H = topLoop->getHeader();

            if (!H) {
                continue;
            }

            llvm::Function *PF = H->getParent();
            if (!PF || PF != function) {
                continue;
            }

            // Optional: trigger a safe query to ensure loop is well-formed before
            // deeper usage. (getBlocksVector is usually safe if loop is valid)
            auto Blocks = topLoop->getBlocksVector();
            if (Blocks.empty()) {
                continue;
            }

            // Construct the LoopTree from the Information of the current top-level
            // loop
            LoopTree *LT = new LoopTree(topLoop, topLoop->getSubLoops(), handler, scevPtr);

            // Construct a LoopNode for the current loop
            LoopNode *loopNode = LoopNode::construct(LT, pGraph, analysisStrategy);
            // Replace the blocks used by loop in the previous created ProgramGraph
            pGraph->replaceNodesWithLoopNode(topLoop->getBlocksVector(), loopNode);
        }

        // energyCalculation(pGraph, handler, function);
        energyFunc->energy = pGraph->getEnergy(handler);

    } else {
        // energyCalculation(pGraph, handler, function);
        energyFunc->energy = pGraph->getEnergy(handler);
    }
    delete domtree;
}
