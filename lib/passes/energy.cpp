/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>

#include <llvm/Transforms/Scalar/IndVarSimplify.h>
#include <llvm/Transforms/Scalar/LoopPassManager.h>
#include <llvm/Transforms/Scalar/LoopRotation.h>
#include <llvm/Transforms/Utils/InstructionNamer.h>
#include <llvm/Transforms/Utils/Mem2Reg.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ConfigParser.h"
#include "DeMangler.h"
#include "EnergyFunction.h"
#include "FunctionTree.h"
#include "HLAC/hlac.h"
#include "HLAC/hlacwrapper.h"
#include "LLVMHandler.h"
#include "PhasarHandler.h"
#include "ProfileHandler.h"
#include "ProgramGraph.h"

#include <nlohmann/json.hpp>

#include "ClusteredAnalysis.h"
#include "HLAC/util.h"
#include "ILP/ILPBuilder.h"
#include "ILP/ILPClusterCache.h"
#include "LegacyAnalysis.h"
#include "Logger.h"
#include "MonolithicAnalysis.h"
#include "analyses/ResultRegistry.h"


#define SHOWTIMINGS true

using json = nlohmann::json;

llvm::cl::opt<std::string> energyModelPath("profile", llvm::cl::desc("Energymodel as JSON"),
                                           llvm::cl::value_desc("filepath to .json file"));

llvm::cl::opt<std::string> modeParameter("mode", llvm::cl::desc("Mode the analysis runs on"),
                                    llvm::cl::value_desc("Please choose out of the options program/function"));

llvm::cl::opt<std::string> formatParameter("format", llvm::cl::desc("Format to print as result"),
                                           llvm::cl::value_desc("Please choose out of the options json/plain"));

llvm::cl::opt<std::string>
        analysisStrategyParameter("strategy", llvm::cl::desc("The strategy to analyze"),
                                  llvm::cl::value_desc("Please choose out of the options worst/average/best"));

llvm::cl::opt<std::string>
        loopboundParameter("loopbound",
                           llvm::cl::desc("A value to over-approximate loops, which upper bound can't "
                                          "be calculated"),
                           llvm::cl::value_desc("Please provide a positive integer value"));

llvm::cl::opt<std::string>
        deepCallsParameter("withcalls",
                           llvm::cl::desc("If flag is provided calls will contribute their own energy "
                                          "usage and the usage of the "
                                          "called function to the result"),
                           llvm::cl::value_desc(""));

struct Energy : llvm::PassInfoMixin<Energy> {
    json energyJson;
    bool deepCallsEnabled;
    std::string forFunction;
    std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<uint64_t, std::ratio<1, 1000000000>>>
            stopwatch_start;
    std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<uint64_t, std::ratio<1, 1000000000>>>
            stopwatch_end;

    ResultRegistry resultRegistry;

    /**
     * Constructor to run, when called from a method
     * @param filename Path to the .json file containing the energymodel
     */
    explicit Energy(const std::string &filename, ResultRegistry &registry) {
        if (llvm::sys::fs::exists(filename) && !llvm::sys::fs::is_directory(filename)) {
            // Create a JSONHandler object and read in the energypath
            ProfileHandler &phandler = ProfileHandler::get_instance();
            phandler.read(filename);
            this->energyJson = phandler.getProfile()["cpu"];

            this->stopwatch_start = std::chrono::steady_clock::now();
            this->deepCallsEnabled = true;
            this->forFunction = std::move(forFunction);

            this->resultRegistry = registry;
        }
    }

    /**
     * Constructor called by the passmanager
     */
    Energy() {
        if (llvm::sys::fs::exists(energyModelPath) && !llvm::sys::fs::is_directory(energyModelPath)) {
            // Create a JSONHandler object and read in the energypath
            ProfileHandler &phandler = ProfileHandler::get_instance();
            phandler.read(energyModelPath.c_str());
            this->energyJson = phandler.getProfile()["profile"];
            this->deepCallsEnabled = !deepCallsParameter.empty();

            this->stopwatch_start = std::chrono::steady_clock::now();
        }
    }

    /**
     * Construct the JSON-Object from which we will draw all necessary information
     * for the output
     * @param funcpool Array of EnergyFunctions containing the information about the analyzed functions
     * @param numberOfFuncs Number of functions in the funcpool
     * @param duration Duration of the analysis
     * @param forFunction If the analysis was only executed for a specific function, this parameter contains the name
     * of the function. Otherwise, it is empty.
     * output
     */
    static json constructOutputObject(EnergyFunction funcpool[],
                                    int numberOfFuncs,
                                    double duration,
                                    std::string forFunction) {
        json outputObject = nullptr;

        if (ConfigParser::getAnalysisConfiguration().legacyconfig.mode == Mode::PROGRAM) {
            outputObject = json::object();
            outputObject["functions"] = json::array();
            outputObject["duration"] = duration;

            for (int i = 0; i < numberOfFuncs; i++) {
                auto energyFunction = &funcpool[i];
                std::string fName = energyFunction->func->getName().str();

                json functionObject = json::object();
                functionObject["name"] = fName;
                functionObject["nM"] = DeMangler::demangle(fName);
                functionObject["energy"] = energyFunction->energy;
                functionObject["numberOfBasicBlocks"] = energyFunction->func->size();
                functionObject["numberOfInstructions"] = energyFunction->func->getInstructionCount();

                if (!energyFunction->func->empty()) {
                    functionObject["averageEnergyPerBlock"] =
                            energyFunction->energy / static_cast<double>(energyFunction->func->size());
                } else {
                    functionObject["averageEnergyPerBlock"] = 0;
                }

                if (energyFunction->func->getInstructionCount() > 0) {
                    functionObject["averageEnergyPerInstruction"] =
                            energyFunction->energy / static_cast<double>(energyFunction->func->getInstructionCount());
                } else {
                    functionObject["averageEnergyPerInstruction"] = 0;
                }

                outputObject["functions"][i] = functionObject;
            }
        } else if (ConfigParser::getAnalysisConfiguration().legacyconfig.mode == Mode::BLOCK) {
            outputObject = json::object();
            outputObject["functions"] = json::array();
            outputObject["duration"] = duration;

            for (int i = 0; i < numberOfFuncs; i++) {
                auto energyFunction = &funcpool[i];
                json functionObject = json::object();
                std::string fName = energyFunction->func->getName().str();

                functionObject["name"] = fName;
                functionObject["demangled"] = DeMangler::demangle(fName);
                functionObject["nodes"] = json::array();

                if (energyFunction->programGraph != nullptr) {
                    std::vector<Node *> nodelist = energyFunction->programGraph->getNodes();
                    if (!nodelist.empty()) {
                        for (int j = 0; j < nodelist.size(); j++) {
                            json nodeObject = json::object();
                            Node *Node = nodelist[j];

                            if (Node->block) {
                                nodeObject["name"] = Node->block->getName().str();
                                nodeObject["energy"] = Node->energy;
                                functionObject["nodes"][j] = nodeObject;
                            }
                        }
                    }
                }

                outputObject["functions"][i] = functionObject;
            }
        } else if (ConfigParser::getAnalysisConfiguration().legacyconfig.mode == Mode::INSTRUCTION) {
            outputObject = json::object();
            outputObject["functions"] = json::array();
            outputObject["duration"] = duration;

            for (int i = 0; i < numberOfFuncs; i++) {
                auto energyFunction = &funcpool[i];
                json functionObject = json::object();
                std::string fName = energyFunction->func->getName().str();
                std::string dMnglName = DeMangler::demangle(fName);
                functionObject["external"] = energyFunction->func->isDeclarationForLinker();
                const auto subProgram = energyFunction->func->getSubprogram();
                if (subProgram != nullptr) {
                    functionObject["file"] = subProgram->getFile()->getDirectory().str() + "/" +
                                             subProgram->getFile()->getFilename().str();
                } else {
                    functionObject["file"] = "";
                }

                functionObject["energy"] = energyFunction->energy;

                if (forFunction.empty() || forFunction == dMnglName) {
                    functionObject["name"] = fName;
                    functionObject["demangled"] = dMnglName;
                    functionObject["nodes"] = json::array();

                    if (energyFunction->programGraph != nullptr) {
                        functionObject = energyFunction->programGraph->populateJsonRepresentation(functionObject);
                    }

                    outputObject["functions"].push_back(functionObject);
                }
            }
        } else if (ConfigParser::getAnalysisConfiguration().legacyconfig.mode == Mode::GRAPH) {
            bool functionExists = false;
            if (!forFunction.empty()) {
                for (int i = 0; i < numberOfFuncs; i++) {
                    auto energyFunction = &funcpool[i];
                    json functionObject = json::object();
                    std::string fName = energyFunction->func->getName().str();
                    std::string dMnglName = DeMangler::demangle(fName);
                    functionExists = functionExists || dMnglName == forFunction;
                }
            } else {
                functionExists = true;
            }

            if (functionExists) {
                llvm::outs() << "digraph " << "SPEARGRAPH" << "{\n";
                llvm::outs() << "compound=true;\n";
                llvm::outs() << "rankdir=\"TB\";\n";
                llvm::outs() << "nodesep=1.5;\n";
                llvm::outs() << "ranksep=1.5;\n";
                llvm::outs() << "linelength=30;\n";
                llvm::outs() << "graph[fontname=Arial]\n";
                llvm::outs() << "node[fontname=Arial, shape=\"rect\"]\n";
                llvm::outs() << "edge[fontname=Arial]\n";
                for (int i = 0; i < numberOfFuncs; i++) {
                    auto energyFunction = &funcpool[i];
                    json functionObject = json::object();
                    std::string fName = energyFunction->func->getName().str();
                    std::string dMnglName = DeMangler::demangle(fName);

                    if (forFunction.empty() || forFunction == dMnglName) {
                        if (energyFunction->programGraph != nullptr) {
                            double maxEng = energyFunction->programGraph->findMaxEnergy();
                            llvm::outs() << "subgraph cluster_" << energyFunction->func->getName() << "{\n";
                            llvm::outs() << "rank=\"same\"\n";
                            llvm::outs() << "margin=40\n";
                            llvm::outs() << "bgcolor=white\n";
                            llvm::outs() << "cluster=true\n";
                            llvm::outs() << "\tlabel=<<b>Function " + energyFunction->func->getName()
                            + "</b><br/>" + std::to_string(maxEng) + " J>\n";
                            llvm::outs() << energyFunction->programGraph->printDotRepresentation();
                            llvm::outs() << "}" << "\n";
                        }
                    }
                }
                llvm::outs() << "subgraph scale {\n";
                llvm::outs() << "scale_image [label=\"\" shape=none "
                                "image=\"/usr/share/spear/scale.png\"];\n";
                llvm::outs() << "margin=40\n";
                llvm::outs() << "bgcolor=white\n";
                llvm::outs() << "}";
                llvm::outs() << "}\n";
            } else {
                throw std::invalid_argument("Function does not exist!");
            }
        }

        return outputObject;
    }

    /**
     * Prints the provided JSON-Object as stylized json string
     * @param outputObject JSON-Object containing information about the analysis
     */
    static void outputMetricsJSON(json outputObject) {
        if (outputObject != nullptr) {
            llvm::outs() << outputObject.dump(4) << "\n";
        }
    }

    /**
     * Print the provided JSON-Object as string
     * @param outputObject JSON-Object containing information about the analysis
     */
    static void outputMetricsPlain(json &outputObject) {
        if (outputObject != nullptr) {
            if (ConfigParser::getAnalysisConfiguration().legacyconfig.mode == Mode::PROGRAM) {
                auto timeused = outputObject["duration"].get<double>();
                outputObject.erase("duration");

                for (auto functionObject : outputObject) {
                    if (functionObject.contains("name")) {
                        // llvm::errs() << functionObject.toStyledString() << "\n\n\n";
                        llvm::outs() << "\n";
                        llvm::outs() << "Function " << functionObject["name"].dump() << "\n";
                        llvm::outs() << "=================================================="
                                        "===================="
                                     << "\n";
                        llvm::outs() << "Estimated energy consumption: " << functionObject["energy"].get<double>()
                                     << " J\n";
                        llvm::outs() << "Number of basic blocks: " << functionObject["numberOfBasicBlocks"].get<int>()
                                     << "\n";
                        llvm::outs() << "Number of instruction: " << functionObject["numberOfInstructions"].get<int>()
                                     << "\n";
                        llvm::outs() << "Ø energy per block: " << functionObject["averageEnergyPerBlock"].get<double>()
                                     << " J\n";
                        llvm::outs() << "Ø energy per instruction: "
                                     << functionObject["averageEnergyPerInstruction"].get<double>() << " J\n";
                        llvm::outs() << "=================================================="
                                        "===================="
                                     << "\n";
                        llvm::outs() << "\n";
                    }
                }
                llvm::outs() << "The Analysis took: " << timeused << " s\n";
            } else if (ConfigParser::getAnalysisConfiguration().legacyconfig.mode == Mode::BLOCK) {
                llvm::errs() << "Not implemented" << "\n";
            } else {
                llvm::errs() << "Please specify the mode the pass should run "
                                "on:\n\t-mode program analyzes the program "
                                "starting in the main function\n\t-mode function "
                                "analyzes all functions, "
                                "without respect to calls"
                             << "\n";
            }
        }
    }


    static void prepareFunctionsForLegacyAnalysis(
    llvm::Module &module,
    llvm::FunctionAnalysisManager &functionAnalysisManager) {

        llvm::FunctionPassManager functionPassManager;

        // Give unnamed instructions stable names for debugging
        functionPassManager.addPass(llvm::InstructionNamerPass());

        // Promote allocas to SSA form
        functionPassManager.addPass(llvm::PromotePass());

        // Canonicalize loops so ScalarEvolution can reason about them better
        //functionPassManager.addPass(llvm::LoopSimplifyPass());

        // Keep loops in LCSSA form
        //functionPassManager.addPass(llvm::LCSSAPass());

        // Simplify induction variables
        llvm::LoopPassManager loopPassManager;
        //loopPassManager.addPass(llvm::IndVarSimplifyPass());

        functionPassManager.addPass(
            llvm::createFunctionToLoopPassAdaptor(std::move(loopPassManager)));

        for (llvm::Function &function : module) {
            if (function.isDeclaration()) {
                continue;
            }

            functionPassManager.run(function, functionAnalysisManager);
        }
    }

   void legacyWrapper(llvm::Module &module, llvm::FunctionAnalysisManager &functionAnalysisManager) {
        auto legacyPreparationStart = std::chrono::high_resolution_clock::now();

        prepareFunctionsForLegacyAnalysis(module, functionAnalysisManager);

        auto legacyPreparationEnd = std::chrono::high_resolution_clock::now();
        auto legacyPreparationTime = std::chrono::duration_cast<std::chrono::microseconds>(
            legacyPreparationEnd - legacyPreparationStart);

        Logger::getInstance().log(
            "Legacy IR preparation took: " + std::to_string(legacyPreparationTime.count()) + " µs",
            LOGLEVEL::INFO);

        // Construct the functionTrees to the functions of the module
        FunctionTree *functionTree = nullptr;
        for (llvm::Function &function : module) {
            if (function.getName() == "main") {
                functionTree = FunctionTree::construct(&function);
                break;
            }
        }

        if (functionTree == nullptr) {
            throw std::runtime_error("Could not construct FunctionTree: main function not found.");
        }

        LegacyAnalysis::run(functionAnalysisManager, functionTree, SHOWTIMINGS);
    }


    /**
     * Function to run the analysis on a given module
     * @param module LLVM::Module to run the analysis on
     * @param moduleAnalysisManager llvm::ModuleAnalysisManager
     * @param analysisStrategy Strategy to analyze the module with
     */
    void analysisRunner(llvm::Module &module, llvm::ModuleAnalysisManager &moduleAnalysisManager,
                    AnalysisStrategy::Strategy analysisStrategy) {

        Logger::getInstance().setLogLevel(LOGLEVEL::ERROR);

        auto &functionAnalysisManager =
            moduleAnalysisManager.getResult<llvm::FunctionAnalysisManagerModuleProxy>(module).getManager();

        if (ConfigParser::getAnalysisConfiguration().analysisType == AnalysisType::LEGACY) {
            legacyWrapper(module, functionAnalysisManager);
            return;
        }

        if (ConfigParser::getAnalysisConfiguration().analysisType == AnalysisType::COMPARISON) {
            legacyWrapper(module, functionAnalysisManager);
        }

        auto postOrderFuncList = HLAC::Util::getLazyCallGraphPostOrder(module, functionAnalysisManager);

        std::vector<std::string> functionNamesInPostOrder;
        for (auto &function : postOrderFuncList) {
            functionNamesInPostOrder.push_back(function->getName().str());
        }

        std::unique_ptr<HLAC::hlac> graph = HLAC::HLACWrapper::makeHLAC(this->resultRegistry);
        std::shared_ptr<HLAC::hlac> sharedGraph = std::move(graph);

        auto startConstruction = std::chrono::high_resolution_clock::now();

        for (auto function : postOrderFuncList) {
            sharedGraph->makeFunction(function, &functionAnalysisManager);
        }

        auto endConstruction = std::chrono::high_resolution_clock::now();
        auto constructionTime = std::chrono::duration_cast<std::chrono::microseconds>(
            endConstruction - startConstruction);

        Logger::getInstance().log(
            "HLAC construction took: " + std::to_string(constructionTime.count()) + " µs",
            LOGLEVEL::INFO);

        auto dotWritingStart = std::chrono::high_resolution_clock::now();
        sharedGraph->printDotRepresentation();
        auto dotWritingEnd = std::chrono::high_resolution_clock::now();

        auto dotTime = std::chrono::duration_cast<std::chrono::microseconds>(dotWritingEnd - dotWritingStart);
        Logger::getInstance().log("DOT writing took: " + std::to_string(dotTime.count()) + " µs", LOGLEVEL::INFO);

        switch (ConfigParser::getAnalysisConfiguration().analysisType) {
            case AnalysisType::MONOLITHIC:
                MonolithicAnalysis::run(sharedGraph, SHOWTIMINGS);
                break;
            case AnalysisType::CLUSTERED:
                ClusteredAnalysis::run(sharedGraph, SHOWTIMINGS);
                break;
            case AnalysisType::COMPARISON:
                MonolithicAnalysis::run(sharedGraph, SHOWTIMINGS);
                ClusteredAnalysis::run(sharedGraph, SHOWTIMINGS);
                break;
            default:
                llvm::errs() << "Please provide a valid analysis type: monolithic/clustered/legacy/comparison\n";
                break;
        }

        /**
         * TODO Handle the output generated from the methods
         */
    }

    /**
     * Main runner of the energy pass. The pass will apply module-wise.
     * @param module Reference to a Module
     * @param moduleAnalysisManager Reference to a ModuleAnalysisManager
     */
    llvm::PreservedAnalyses run(llvm::Module &module, llvm::ModuleAnalysisManager &moduleAnalysisManager) {
        auto strategy = ConfigParser::getAnalysisConfiguration().legacyconfig.strategy;

        // Check the analysis-strategy the user requestet
        if (strategy == Strategy::BEST) {
            analysisRunner(module, moduleAnalysisManager, AnalysisStrategy::BESTCASE);
        } else if (strategy == Strategy::WORST) {
            analysisRunner(module, moduleAnalysisManager, AnalysisStrategy::WORSTCASE);
        } else if (strategy == Strategy::AVERAGE) {
            analysisRunner(module, moduleAnalysisManager, AnalysisStrategy::AVERAGECASE);
        } else {
            llvm::errs() << "Please provide a valid analysis strategy: best/worst/average"
                         << "\n";
        }
        return llvm::PreservedAnalyses::all();
    }

    static bool isRequired() { return true; }
};

/**
 * Method for providing some basic information about the pass
 */
llvm::PassPluginLibraryInfo getEnergyPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "Energy", LLVM_VERSION_STRING, [](llvm::PassBuilder &PB) {
                PB.registerPipelineParsingCallback([](llvm::StringRef Name, llvm::ModulePassManager &modulePassManager,
                                                      llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) {
                    if (Name == "energy") {
                        modulePassManager.addPass(Energy());
                        return true;
                    }
                    return false;
                });
            }};
}

// Register the pass in llvm, so it is useable with opt
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() { return getEnergyPluginInfo(); }
