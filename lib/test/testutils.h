/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#pragma once

#include <iostream>
#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

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

        phasarHandler = PhasarHandlerPass(
            config.runLoopBoundAnalysis,
            config.runFeasibilityAnalysis,
            false);
    }

    llvm::Module &module() { return *M; }

    const llvm::Module &module() const { return *M; }
};

inline std::unique_ptr<SpearRun> runSpearOnFile(std::filesystem::path testroot, std::string strPath,
                                                TestConfig config) {
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

    run->phasarHandler.runOnModule(*run->M);


    return run;
}
