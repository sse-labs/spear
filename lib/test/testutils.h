/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#pragma once

#include <iostream>
#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Transforms/Scalar/IndVarSimplify.h>
#include <llvm/Transforms/Scalar/LoopRotation.h>
#include <llvm/Transforms/Utils/InstructionNamer.h>
#include <llvm/Transforms/Utils/Mem2Reg.h>

#include "ConfigParser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"

#include "PhasarHandler.h"


struct TestConfig {
    bool runFeasibilityAnalysis = false;
    bool runLoopBoundAnalysis = false;
};

struct SpearRun {
    std::unique_ptr<llvm::LLVMContext> Ctx;
    std::unique_ptr<llvm::Module> M;
    TestConfig testConfig{};

    PhasarHandlerPass phasarHandler;

    explicit SpearRun(TestConfig config) {
        testConfig = config;

        phasarHandler = PhasarHandlerPass(config.runLoopBoundAnalysis, config.runFeasibilityAnalysis, false);
    }

    llvm::Module &module() { return *M; }

    const llvm::Module &module() const { return *M; }
};

inline std::unique_ptr<SpearRun> runSpearOnFile(std::filesystem::path testroot, std::string strPath, TestConfig config,
                                                bool requireOptimizationPasses) {
    llvm::LLVMContext context;
    llvm::SMDiagnostic error;
    llvm::PassBuilder passBuilder;
    llvm::LoopAnalysisManager loopAnalysisManager;
    llvm::FunctionAnalysisManager functionAnalysisManager;
    llvm::CGSCCAnalysisManager cGSCCAnalysisManager;
    llvm::ModuleAnalysisManager moduleAnalysisManager;
    llvm::ModulePassManager modulePassManager;
    llvm::FunctionPassManager functionPassManager;
    llvm::CGSCCPassManager callgraphPassManager;

    auto run = std::make_unique<SpearRun>(config);
    run->Ctx = std::make_unique<llvm::LLVMContext>();

    std::cout << "Root: " << testroot.string() << std::endl;
    std::cout << "Path: " << strPath << std::endl;

    // Parse the config, otherwise we cannot rely on fallback values
    ConfigParser configParser(testroot / "defaultconfig.json");
    configParser.parse();


    auto combined = testroot / strPath;
    std::string combinedAsStr = combined.string();

    llvm::SMDiagnostic Err;
    run->M = llvm::parseIRFile(combinedAsStr, Err, *run->Ctx);

    INFO("Failed to parse IR file: " << strPath);
    REQUIRE(run->M != nullptr);

    if (requireOptimizationPasses) {
        passBuilder.registerModuleAnalyses(moduleAnalysisManager);
        passBuilder.registerCGSCCAnalyses(cGSCCAnalysisManager);
        passBuilder.registerFunctionAnalyses(functionAnalysisManager);
        passBuilder.registerLoopAnalyses(loopAnalysisManager);
        passBuilder.crossRegisterProxies(loopAnalysisManager, functionAnalysisManager, cGSCCAnalysisManager,
                                         moduleAnalysisManager);

        functionPassManager.addPass(llvm::InstructionNamerPass());
        functionPassManager.addPass(llvm::PromotePass());
        functionPassManager.addPass(llvm::LoopSimplifyPass());
        functionPassManager.addPass(llvm::LCSSAPass());
        functionPassManager.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::LoopRotatePass()));
        functionPassManager.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::IndVarSimplifyPass()));


        modulePassManager.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(functionPassManager)));

        // IMPORTANT: actually run the NewPM pipeline to materialize proxies/analysis state
        // before any external code queries analyses from a FAM.
        modulePassManager.run(*run->M, moduleAnalysisManager);
    }

    run->phasarHandler.runOnModule(*run->M);

    return run;
}


inline void CHECK_INFEASIBLE_BLOCKS_STRICT(std::map<std::string, Feasibility::BlockFeasInfo> *blocks,
                                           const std::vector<std::string> &expectedInfeasible,
                                           const std::vector<std::string> &expectedAllBlocks) {

    const std::unordered_set<std::string> expectedBad(expectedInfeasible.begin(), expectedInfeasible.end());
    const std::unordered_set<std::string> expectedAll(expectedAllBlocks.begin(), expectedAllBlocks.end());

    // Ensure block universe matches
    CHECK(blocks->size() == expectedAll.size());
    for (const auto &name : expectedAll) {
        INFO("Expected block name: " << name);
        CHECK(blocks->contains(name));
    }

    // Now check feasibility property
    for (const auto &name : expectedAll) {
        const auto &info = blocks->at(name);
        const bool shouldBeFeasible = !expectedBad.contains(name);

        CAPTURE(name);
        CAPTURE(shouldBeFeasible);
        CAPTURE(info.Feasible);

        CHECK(info.Feasible == shouldBeFeasible);
    }
}
