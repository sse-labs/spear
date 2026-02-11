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
    llvm::FunctionPassManager functionPassManager;
    llvm::CGSCCPassManager callgraphPassManager;


    auto module_up = llvm::parseIRFile(opts.programPath, error, context).release();

    passBuilder.registerModuleAnalyses(moduleAnalysisManager);
    passBuilder.registerCGSCCAnalyses(cGSCCAnalysisManager);
    passBuilder.registerFunctionAnalyses(functionAnalysisManager);
    passBuilder.registerLoopAnalyses(loopAnalysisManager);
    passBuilder.crossRegisterProxies(
            loopAnalysisManager, functionAnalysisManager, cGSCCAnalysisManager,
            moduleAnalysisManager);

    // instname
    functionPassManager.addPass(llvm::InstructionNamerPass());
    // mem2reg

    /**
     * Disabled to allow phasar to infer more variables
    */

    functionPassManager.addPass(llvm::PromotePass());

    // loop-simplify
    functionPassManager.addPass(llvm::LoopSimplifyPass());

    functionPassManager.addPass(llvm::LCSSAPass());

    // loop-rotate
    functionPassManager.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::LoopRotatePass()));
    modulePassManager.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(functionPassManager)));

    functionPassManager.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::IndVarSimplifyPass()));

    PhasarHandlerPass PH;
    PH.runOnModule(*module_up);

    // Store results for later use
    auto MainFn = module_up->getFunction("main");
    auto PhasarResults = PH.queryBoundVars(MainFn);

    PhasarResultRegistry::get().store(PhasarResults);

    modulePassManager.addPass(Energy(opts.profilePath));
    modulePassManager.run(*module_up, moduleAnalysisManager);
}


int main(int argc, char *argv[]) {
    std::string helpString = "Usage: spear option <arguments>\n==============================\nOptions:"
                             "\n\tprofile\t Profile the system and generate the estimated energy usage of the device. "
                             "Used for any further analysis"
                             "\n\tanalyze\t Analyzes a given program. Further parameters are needed:"
                             "\n\t\t\t --mode Type of analysis (program/function)"
                             "\n\t\t\t --format Format of the result to print (plain/json)"
                             "\n\t\t\t --strategy Type of analysis-strategy (worst/best/average)"
                             "\n\t\t\t --loopbound Value with with which loops get approximed if their upper bound "
                             "can't be calculated (0 - INT_MAX)"
                             "\n\n";

    if (argc > 1) {
        CLIOptions opts = CLIHandler::parseCLI(argc, argv);
        ConfigParser configParser(opts.configPath);
        configParser.parse();

        if (!opts.configPath.empty()) {
            if (configParser.configValid()) {
                if (opts.operation == Operation::PROFILE) {
                    // Check if the parser returned valid options
                    if (!opts.codePath.empty() && !opts.saveLocation.empty()) {
                        // std::cout << "Options valid" << std::endl;
                        runProfileRoutine(opts);
                        return 0;
                    } else {
                        std::string profileHelpMsg =  "Usage: spear profile <arguments>\n===================="
                                                      "==========\nArguments:"
                                                      "\n\t Profile the system and generate the estimated energy usage"
                                                      " of the device. Used for any further analysis"
                                                      "\n\t\t --iterations Amount of measurement repetitions (int)"
                                                      "\n\t\t --model Path to the compiled profile programs"
                                                      "\n\t\t --savelocation Path the calculated profile will be saved "
                                                      "to \n\n";
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
                        std::string profileHelpMsg = "Usage: spear analyze <arguments>\n========================="
                                                     "=====\nArguments:"
                                                     "\n\tAnalyzes a given program. Further parameters are needed:"
                                                     "\n\t\t --mode Type of analysis (program/function)"
                                                     "\n\t\t --format Format of the result to print (plain/json)"
                                                     "\n\t\t --strategy Type of analysis-strategy (worst/best/average)"
                                                     "\n\t\t --loopbound Value with with which loops get approximed if "
                                                     "their upper bound can't be calculated (0 - INT_MAX)"
                                                     "\n\n";

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
            std::cerr << "Config parsing failed! Ensure the path is accessible and the file is not empty!"
            << std::endl;
        }


    } else {
        std::cout << helpString;
    }
}
