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
#include "Modelchecker.h"
#include "analyses/feasibility/util.h"
#include "profilers/CPUProfiler.h"
#include "profilers/MetaProfiler.h"

#include "llvm/Transforms/Scalar/IndVarSimplify.h"
#include "src/spear/PhasarResultRegistry.h"


void runProfileRoutine(CLIOptions opts) {
    auto proflingConfig = ConfigParser::getProfilingConfiguration();

    // Get the parameters from the arguments
    int rep = proflingConfig.iterations;
    std::string compiledPath = opts.codePath;

    CPUProfiler cpuprofiler = CPUProfiler(rep, compiledPath);
    MetaProfiler metaprofiler = MetaProfiler(rep);

    json metaResult = metaprofiler.profile();

    metaResult["start"] = metaprofiler.startTime();
    // Launch the benchmarking
    try {
        json cpuResult = cpuprofiler.profile();
        metaResult["end"] = metaprofiler.stopTime();

        char *outputpath = new char[255];
        snprintf(
            outputpath,
            255,
            "%s/profile.json",
            opts.saveLocation.c_str());
        std::cout << "Writing " << outputpath << "\n";

        ProfileHandler phandler;
        phandler.setOrCreate("meta", metaResult);
        phandler.setOrCreate("cpu", cpuResult);
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
    llvm::PassBuilder passBuilder;
    llvm::LoopAnalysisManager loopAnalysisManager;
    llvm::FunctionAnalysisManager functionAnalysisManager;
    llvm::CGSCCAnalysisManager cGSCCAnalysisManager;
    llvm::ModuleAnalysisManager moduleAnalysisManager;
    llvm::ModulePassManager modulePassManager;

    auto module_up = llvm::parseIRFile(opts.programPath, error, context).release();
    if (!module_up) {
        llvm::errs() << "Failed to parse IR file: " << opts.programPath << "\n";
        return;
    }

    passBuilder.registerModuleAnalyses(moduleAnalysisManager);
    passBuilder.registerCGSCCAnalyses(cGSCCAnalysisManager);
    passBuilder.registerFunctionAnalyses(functionAnalysisManager);
    passBuilder.registerLoopAnalyses(loopAnalysisManager);
    passBuilder.crossRegisterProxies(
            loopAnalysisManager, functionAnalysisManager, cGSCCAnalysisManager,
            moduleAnalysisManager);

    // Build function pipeline once, then move it exactly once.
    llvm::FunctionPassManager functionPassManager;
    functionPassManager.addPass(llvm::InstructionNamerPass());
    functionPassManager.addPass(llvm::PromotePass());
    functionPassManager.addPass(llvm::LoopSimplifyPass());
    functionPassManager.addPass(llvm::LCSSAPass());
    functionPassManager.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::LoopRotatePass()));
    functionPassManager.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::IndVarSimplifyPass()));

    modulePassManager.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(functionPassManager)));

    // IMPORTANT: actually run the NewPM pipeline to materialize proxies/analysis state
    // before any external code queries analyses from a FAM.
    modulePassManager.run(*module_up, moduleAnalysisManager);

    PhasarHandlerPass PH;
    PH.runOnModule(*module_up);

    /*Modelchecker McheckerInstance;
    auto mcheckercontext = McheckerInstance.getContext();
    auto x = mcheckercontext->int_const("x");

    McheckerInstance.addExpression(x < 5 && x > 5);
    auto checkres = McheckerInstance.check();

    std::cout << "Modelchecker result: " << checkres << "\n";*/

    // Store results for later use
    auto MainFn = module_up->getFunction("main");
    auto loopboundResults = PH.queryBoundVars(MainFn);
    auto start = std::chrono::high_resolution_clock::now();
    auto feasibilityResults = PH.queryFeasibilty();
    auto end = std::chrono::high_resolution_clock::now();


    for (const auto &functionEntry : feasibilityResults) {
        llvm::outs() << "Feasibility information for function: " << functionEntry.first << "\n";
        for (const auto &blockEntry : functionEntry.second) {
            std::string feasStr = blockEntry.second.Feasible? "REACHABLE": "UNREACHABLE";

            llvm::outs() << "\t Block: " << blockEntry.first << " => " << feasStr << "\n";
        }
        llvm::outs() << "\n";
    }

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Runtime: " << duration.count() << " ms\n";

    PhasarResultRegistry::get().store(loopboundResults);

    // modulePassManager already ran above (don't run twice unless you intend to).
    // modulePassManager.addPass(Energy(opts.profilePath));
    // modulePassManager.run(*module_up, moduleAnalysisManager);
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
