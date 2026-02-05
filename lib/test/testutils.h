/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#pragma once

#include <iostream>
#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"

#include "PhasarHandler.h"

struct SpearRun {
    std::unique_ptr<llvm::LLVMContext> Ctx;
    std::unique_ptr<llvm::Module> M;
    PhasarHandlerPass phasarHandler;

    llvm::Module &module() { return *M; }
    const llvm::Module &module() const { return *M; }
};

inline std::unique_ptr<SpearRun> runSpearOnFile(std::filesystem::path testroot, std::string strPath) {
    auto R = std::make_unique<SpearRun>();
    R->Ctx = std::make_unique<llvm::LLVMContext>();

    std::cout << "Root: " << testroot.string() << std::endl;
    std::cout << "Path: " << strPath << std::endl;

    auto combined = testroot / strPath;
    std::string combinedAsStr = combined.string();

    llvm::SMDiagnostic Err;
    R->M = llvm::parseIRFile(combinedAsStr, Err, *R->Ctx);

    INFO("Failed to parse IR file: " << strPath);
    REQUIRE(R->M != nullptr);

    R->phasarHandler.runOnModule(*R->M);
    return R;
}
