/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <string>
#include <utility>
#include <cstdio>
#include <iostream>
#include <stdexcept>

#include "src/spear/profilers/Profiler.h"
#include "LLVMHandler.h"
#include "ProfileHandler.h"
#include "energy.cpp"

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Utils/InstructionNamer.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/Scalar/LoopRotation.h"
#include "llvm/IRReader/IRReader.h"

#include "CLIHandler.h"
#include "ConfigParser.h"
#include "analyses/ResultRegistry.h"
#include "profilers/CPUProfiler.h"
#include "profilers/MetaProfiler.h"

#include "llvm/Transforms/Scalar/IndVarSimplify.h"
#include "profilers/SyscallProfiler.h"

#define SKIP_CPU_PROFILING false


void runProfileRoutine(CLIOptions opts) {
    auto proflingConfig = ConfigParser::getProfilingConfiguration();

    // Get the parameters from the arguments
    std::string compiledPath = opts.codePath;

    CPUProfiler cpuprofiler = CPUProfiler(compiledPath);
    MetaProfiler metaprofiler = MetaProfiler();
    SyscallProfiler syscallProfiler = SyscallProfiler();

    json metaResult = metaprofiler.profile();

    metaResult["start"] = metaprofiler.startTime();
    // Launch the benchmarking
    try {
        json cpuResult;
        if constexpr (!SKIP_CPU_PROFILING) {
            cpuResult = cpuprofiler.profile();
        }

        json syscallResults = syscallProfiler.profile();

        metaResult["end"] = metaprofiler.stopTime();

        char *outputpath = new char[255];
        snprintf(
            outputpath,
            255,
            "%s/profile.json",
            opts.saveLocation.c_str());
        std::cout << "Writing " << outputpath << "\n";

        ProfileHandler &phandler = ProfileHandler::get_instance();
        phandler.setOrCreate("meta", metaResult);

        if constexpr (!SKIP_CPU_PROFILING) {
            phandler.setOrCreate("cpu", cpuResult);
        }

        phandler.setOrCreate("syscalls", syscallResults);
        phandler.write(outputpath);

        std::cout << "Profiling finished!" << std::endl;
        delete outputpath;
    } catch(std::invalid_argument &ia) {
        std::cerr << "Execution of profile code failed..." << "\n";
    }
}

void runAnalysisRoutine(CLIOptions opts) {
    llvm::LLVMContext context;
    llvm::SMDiagnostic error;
    ResultRegistry resultRegistry;

    auto moduleOriginal = llvm::parseIRFile(opts.programPath, error, context);
    if (!moduleOriginal) {
        llvm::errs() << "Failed to parse IR file: " << opts.programPath << "\n";
        return;
    }

    // Separate copy for the optimized/canonicalized pipeline.
    auto moduleOptimized = llvm::CloneModule(*moduleOriginal);

    // Run lobbound on the original module as we need load/store
    auto startLB = std::chrono::high_resolution_clock::now();
    PhasarHandlerPass loopBoundPhasarHandler(true, false);
    loopBoundPhasarHandler.runOnModule(*moduleOriginal);
    auto loopboundResults = loopBoundPhasarHandler.queryLoopBounds();
    resultRegistry.storeLoopBoundResults(loopboundResults);
    auto endLB = std::chrono::high_resolution_clock::now();

    auto durationLB = std::chrono::duration_cast<std::chrono::microseconds>(endLB - startLB);
    std::cout << "Loopbound took: " << durationLB.count() << " µs\n";

    {
        llvm::PassBuilder passBuilder;
        llvm::LoopAnalysisManager loopAnalysisManager;
        llvm::FunctionAnalysisManager functionAnalysisManager;
        llvm::CGSCCAnalysisManager cGSCCAnalysisManager;
        llvm::ModuleAnalysisManager moduleAnalysisManager;

        passBuilder.registerModuleAnalyses(moduleAnalysisManager);
        passBuilder.registerCGSCCAnalyses(cGSCCAnalysisManager);
        passBuilder.registerFunctionAnalyses(functionAnalysisManager);
        passBuilder.registerLoopAnalyses(loopAnalysisManager);
        passBuilder.crossRegisterProxies(loopAnalysisManager,
                                         functionAnalysisManager,
                                         cGSCCAnalysisManager,
                                         moduleAnalysisManager);

        llvm::FunctionPassManager functionPassManager;
        functionPassManager.addPass(llvm::InstructionNamerPass());
        functionPassManager.addPass(llvm::PromotePass());
        functionPassManager.addPass(llvm::LoopSimplifyPass());
        functionPassManager.addPass(llvm::LCSSAPass());
        functionPassManager.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::LoopRotatePass()));
        functionPassManager.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::IndVarSimplifyPass()));

        llvm::ModulePassManager optimizeMPM;
        optimizeMPM.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(functionPassManager)));

        optimizeMPM.run(*moduleOptimized, moduleAnalysisManager);
    }

    // Run feasibility on the optimized module
    auto startFeas = std::chrono::high_resolution_clock::now();
    PhasarHandlerPass feasibilityPhasarHandler(false, true);
    feasibilityPhasarHandler.runOnModule(*moduleOptimized);
    auto feasibilityResults = feasibilityPhasarHandler.queryFeasibilty();
    resultRegistry.storeFeasibilityResults(feasibilityResults);
    auto endFeas = std::chrono::high_resolution_clock::now();

    auto durationFeas = std::chrono::duration_cast<std::chrono::microseconds>(endFeas - startFeas);
    std::cout << "Feasibility took: " << durationFeas.count() << " µs\n";

    // Run energy on the original module
    {
        llvm::PassBuilder passBuilder;
        llvm::LoopAnalysisManager loopAnalysisManager;
        llvm::FunctionAnalysisManager functionAnalysisManager;
        llvm::CGSCCAnalysisManager cGSCCAnalysisManager;
        llvm::ModuleAnalysisManager moduleAnalysisManager;

        passBuilder.registerModuleAnalyses(moduleAnalysisManager);
        passBuilder.registerCGSCCAnalyses(cGSCCAnalysisManager);
        passBuilder.registerFunctionAnalyses(functionAnalysisManager);
        passBuilder.registerLoopAnalyses(loopAnalysisManager);
        passBuilder.crossRegisterProxies(loopAnalysisManager,
                                         functionAnalysisManager,
                                         cGSCCAnalysisManager,
                                         moduleAnalysisManager);

        llvm::ModulePassManager energyMPM;
        energyMPM.addPass(Energy(opts.profilePath, resultRegistry));
        energyMPM.run(*moduleOriginal, moduleAnalysisManager);
    }
}


int main(int argc, char *argv[]) {
    std::string helpString = R"(Usage: spear <option> <arguments>
    ==================================
    Options:

        profile    Profile the system and generate the estimated energy usage
                   of the device. Used for any further analysis.
                   --config        Configuration file for the analysis (path)
                   --model         Path to the compiled profile programs
                   --savelocation  Path to save the generated profile (path)

        analyze    Analyze a given program. Further parameters are required:
                   --profile       Path to the profile to use for the analysis (path)
                   --program       Path to the program to analyze (path)
                   --config        Configuration file for the analysis (path)

    )";

    if (argc > 1) {
        CLIOptions opts = CLIHandler::parseCLI(argc, argv);

        if (!opts.configPath.empty()) {
            ConfigParser configParser(opts.configPath);
            configParser.parse();

            if (configParser.configValid()) {
                if (opts.operation == Operation::PROFILE) {
                    // Check if the parser returned valid options
                    if (!opts.codePath.empty() && !opts.saveLocation.empty()) {
                        // std::cout << "Options valid" << std::endl;
                        runProfileRoutine(opts);
                        return 0;
                    } else {
                        std::string profileHelpMsg =
                        R"(Usage: spear profile <arguments>
                        ========================================
                        Arguments:
                            Profile the system and generate the estimated energy usage of the device.
                            Used for any further analysis.

                                --config        Configuration file for the analysis (path)
                                --model         Path to the compiled profile programs
                                --savelocation  Path to save the generated profile (path)
                        )";


                        std::cerr << profileHelpMsg << std::endl;
                        return 1;
                    }
                } else if (opts.operation == Operation::ANALYZE) {
                    // Check if the parser returned valid options
                    bool hasProfilePath = !opts.profilePath.empty();
                    bool hasProgramPath = !opts.programPath.empty();

                    if (hasProfilePath && hasProgramPath) {
                        // std::cout << "Options valid" << std::endl;
                        runAnalysisRoutine(opts);
                        return 0;
                    } else {
                        std::string profileHelpMsg =
                        R"(Usage: spear analyze <arguments>
                        =================================
                        Arguments:

                            Analyzes a given program. Further parameters are required:
                                --profile        Path to the profile to use for the analysis (path)
                                --program        Path to the program to analyze (path)
                                --config         Configuration file for the analysis (path)

                        )";


                        std::cerr << profileHelpMsg;

                        if (!hasProfilePath) {
                            std::cerr << "Error: Profile path is missing. Please specify --profile <path>\n";
                        }
                        if (!hasProgramPath) {
                            std::cerr << "Error: Program path is missing. Please specify --program <path>\n";
                        }

                        std::cerr << std::endl;
                        return 1;
                    }
                } else {
                    return 1;
                }
            }
        } else {
            std::cerr << helpString;

            std::cerr << "No config path specifed or config file can not be found! "
                         "Please specify a valid config file with --config <path>"
            << std::endl;
            return 1;
        }


    } else {
        std::cout << helpString;
    }
}
