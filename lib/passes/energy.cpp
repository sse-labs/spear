#include <llvm/Passes/PassBuilder.h>

#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/LegacyPassManager.h"
//#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "LLVMHandler.h"
#include "ProfileHandler.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Analysis/PostDominators.h"
#include "DeMangler.h"


#include <nlohmann/json.hpp>
#include <cxxabi.h>

using json = nlohmann::json;


#include "ProgramGraph.h"
#include "FunctionTree.h"
#include "EnergyFunction.h"
#include "CLIOptions.h"


llvm::cl::opt<std::string> energyModelPath("profile", llvm::cl::desc("Energymodel as JSON"), llvm::cl::value_desc("filepath to .json file"));
llvm::cl::opt<std::string> modeParameter("mode", llvm::cl::desc("Mode the analysis runs on"), llvm::cl::value_desc("Please choose out of the options program/function"));
llvm::cl::opt<std::string> formatParameter("format", llvm::cl::desc("Format to print as result"), llvm::cl::value_desc("Please choose out of the options json/plain"));
llvm::cl::opt<std::string> analysisStrategyParameter("strategy", llvm::cl::desc("The strategy to analyze"), llvm::cl::value_desc("Please choose out of the options worst/average/best"));
llvm::cl::opt<std::string> loopboundParameter("loopbound", llvm::cl::desc("A value to over-approximate loops, which upper bound can't be calculated"), llvm::cl::value_desc("Please provide a positive integer value"));
llvm::cl::opt<std::string> deepCallsParameter("withcalls", llvm::cl::desc("If flag is provided calls will contribute their own energy usage and the usage of the called function to the result"), llvm::cl::value_desc(""));



struct Energy : llvm::PassInfoMixin<Energy> {
    json energyJson;
    Mode mode;
    Format format;
    Strategy strategy;
    int loopbound;
    bool deepCallsEnabled;
    std::string forFunction;
    std::chrono::time_point<std::chrono::system_clock, std::chrono::duration<long, std::ratio<1, 1000000000>>> stopwatch_start;
    std::chrono::time_point<std::chrono::system_clock, std::chrono::duration<long, std::ratio<1, 1000000000>>> stopwatch_end;

    /**
     * Constructor to run, when called from a method
     * @param filename Path to the .json file containing the energymodel
     * @param mode Mode to run the analysis on
     * @param format Outputformat
     * @param strategy Strategy to analyze the program with
     * @param loopbound Upper bound of loops that can't be analyzed
     */
    explicit Energy(const std::string& filename, Mode mode, Format format, Strategy strategy, int loopbound, DeepCalls deepCalls, std::string forFunction){
        if( llvm::sys::fs::exists( filename ) && !llvm::sys::fs::is_directory( filename ) ){
            //Create a JSONHandler object and read in the energypath
            ProfileHandler phandler;
            phandler.read(filename);
            this->energyJson = phandler.getProfile()["cpu"];

            this->mode = mode;
            this->format = format;
            this->strategy = strategy;
            this->loopbound = loopbound;
            this->stopwatch_start = std::chrono::high_resolution_clock::now();
            this->deepCallsEnabled = (deepCalls == DeepCalls::ENABLED);
            this->forFunction = std::move(forFunction);
        }
    }

    /**
     * Constructor called by the passmanager
     */
    Energy(){
        if( llvm::sys::fs::exists( energyModelPath ) && !llvm::sys::fs::is_directory( energyModelPath ) ){
            //Create a JSONHandler object and read in the energypath
            ProfileHandler phandler;
            phandler.read(energyModelPath.c_str());
            this->energyJson = phandler.getProfile()["profile"];
            this->mode = CLIOptions::strToMode(modeParameter.c_str());
            this->format = CLIOptions::strToFormat(formatParameter.c_str());
            this->strategy = CLIOptions::strToStrategy(analysisStrategyParameter.c_str());
            this->strategy = CLIOptions::strToStrategy(analysisStrategyParameter.c_str());
            this->deepCallsEnabled = !deepCallsParameter.empty();

            //Try to get the requested loopbound value
            try {
                this->loopbound = std::stoi(loopboundParameter.c_str());

                if(this->loopbound < 0){
                    throw std::invalid_argument("Loopbound can't be negative");
                }
            }catch(std::invalid_argument &I){
                llvm::errs() << "Please provide a positive integer for the loopbound" << "\n";
                return;
            }


            this->stopwatch_start = std::chrono::high_resolution_clock::now();
        }
    }

    /**
     * Construct the JSON-Object from which we will draw all necessary information for the output
     * @param handler LLVMHandler so we can get the functionQueue
     * @return Retuns a JSON-Object containing all needed information for the output
     */
    static json constructOutputObject(EnergyFunction funcpool[], int numberOfFuncs, double duration, Mode mode, std::string forFunction){
        json outputObject = nullptr;

        if(mode == Mode::PROGRAM){
            outputObject = json::object();
            outputObject["functions"] = json::array();
            outputObject["duration"] = duration;

            for (int i=0; i < numberOfFuncs; i++){
                auto energyFunction = &funcpool[i];
                std::string fName = energyFunction->func->getName().str();

                json functionObject = json::object();
                functionObject["name"] = fName;
                functionObject["nM"] = DeMangler::demangle(fName);
                functionObject["energy"] = energyFunction->energy;
                functionObject["numberOfBasicBlocks"] = energyFunction->func->size();
                functionObject["numberOfInstructions"] = energyFunction->func->getInstructionCount();

                if(!energyFunction->func->empty()){
                    functionObject["averageEnergyPerBlock"] = energyFunction->energy / (double) energyFunction->func->size();
                } else {
                    functionObject["averageEnergyPerBlock"] = 0;
                }

                if(energyFunction->func->getInstructionCount() > 0){
                    functionObject["averageEnergyPerInstruction"] = energyFunction->energy / (double) energyFunction->func->getInstructionCount();
                } else {
                    functionObject["averageEnergyPerInstruction"] = 0;
                }

                outputObject["functions"][i] = functionObject;
            }
        }else if(mode == Mode::BLOCK){
            outputObject = json::object();
            outputObject["functions"] = json::array();
            outputObject["duration"] = duration;

            for (int i=0; i < numberOfFuncs; i++){
                auto energyFunction = &funcpool[i];
                json functionObject = json::object();
                std::string fName = energyFunction->func->getName().str();

                functionObject["name"] = fName;
                functionObject["demangled"] = DeMangler::demangle(fName);
                functionObject["nodes"] = json::array();

                if(energyFunction->programGraph != nullptr){
                    std::vector<Node *> nodelist = energyFunction->programGraph->getNodes();
                    if(!nodelist.empty()){
                        for(int j=0; j < nodelist.size(); j++){
                            json nodeObject = json::object();
                            Node* Node = nodelist[j];

                            if(Node->block){
                                nodeObject["name"] = Node->block->getName().str();
                                nodeObject["energy"] = Node->energy;
                                functionObject["nodes"][j] = nodeObject;
                            }
                        }
                    }
                }

                outputObject["functions"][i] = functionObject;
            }
        }else if(mode == Mode::INSTRUCTION){
            outputObject = json::object();
            outputObject["functions"] = json::array();
            outputObject["duration"] = duration;

            for (int i=0; i < numberOfFuncs; i++){
                auto energyFunction = &funcpool[i];
                json functionObject = json::object();
                std::string fName = energyFunction->func->getName().str();
                std::string dMnglName = DeMangler::demangle(fName);
                functionObject["external"] = energyFunction->func->isDeclarationForLinker();
                const auto subProgram = energyFunction->func->getSubprogram();
                if(subProgram != nullptr){
                    functionObject["file"] = subProgram->getFile()->getDirectory().str() + "/" + subProgram->getFile()->getFilename().str();
                }else{
                    functionObject["file"] = "";
                }

                functionObject["energy"] = energyFunction->energy;

                if(forFunction.empty() || forFunction == dMnglName){
                    functionObject["name"] = fName;
                    functionObject["demangled"] = dMnglName;
                    functionObject["nodes"] = json::array();

                    if(energyFunction->programGraph != nullptr){
                        functionObject = energyFunction->programGraph->populateJsonRepresentation(functionObject);
                    }
                    
                    outputObject["functions"].push_back(functionObject);
                }

            }
        }else if(mode == Mode::GRAPH){
            bool functionExists = false;
            if(!forFunction.empty()){
                for (int i=0; i < numberOfFuncs; i++) {
                    auto energyFunction = &funcpool[i];
                    json functionObject = json::object();
                    std::string fName = energyFunction->func->getName().str();
                    std::string dMnglName = DeMangler::demangle(fName);
                    functionExists = functionExists || dMnglName == forFunction;
                }
            }else{
                functionExists = true;
            }

            if(functionExists){
                llvm::outs() << "digraph " << "SPEARGRAPH" << "{\n";
                llvm::outs() << "compound=true;\n";
                llvm::outs() << "rankdir=\"TB\";\n";
                llvm::outs() << "nodesep=1.5;\n";
                llvm::outs() << "ranksep=1.5;\n";
                llvm::outs() << "linelength=30;\n";
                llvm::outs() << "graph[fontname=Arial]\n";
                llvm::outs() << "node[fontname=Arial, shape=\"rect\"]\n";
                llvm::outs() << "edge[fontname=Arial]\n";
                for (int i=0; i < numberOfFuncs; i++){
                    auto energyFunction = &funcpool[i];
                    json functionObject = json::object();
                    std::string fName = energyFunction->func->getName().str();
                    std::string dMnglName = DeMangler::demangle(fName);

                    if(forFunction.empty() || forFunction == dMnglName){
                        if(energyFunction->programGraph != nullptr){
                            double maxEng = energyFunction->programGraph->findMaxEnergy();
                            llvm::outs() << "subgraph cluster_" << energyFunction->func->getName() << "{\n";
                            llvm::outs() << "rank=\"same\"\n";
                            llvm::outs() << "margin=40\n";
                            llvm::outs() << "bgcolor=white\n";
                            llvm::outs() << "cluster=true\n";
                            llvm::outs() << "\tlabel=<<b>Function " + energyFunction->func->getName() + "</b><br/>" + std::to_string(maxEng) + " J>\n";
                            llvm::outs() << energyFunction->programGraph->printDotRepresentation();
                            llvm::outs() << "}" << "\n";
                        }
                    }

                }
                llvm::outs() << "subgraph scale {\n";
                llvm::outs() << "scale_image [label=\"\" shape=none image=\"/usr/share/spear/scale.png\"];\n";
                llvm::outs() << "margin=40\n";
                llvm::outs() << "bgcolor=white\n";
                llvm::outs() << "}";
                llvm::outs() << "}\n";
            }else{
                throw std::invalid_argument("Function does not exist!");
            }
        }

        return outputObject;
    }

    /**
     * Prints the provided JSON-Object as stylized json string
     * @param outputObject JSON-Object containing information about the analysis
     */
    static void outputMetricsJSON(json outputObject, Mode mode){

        if(outputObject != nullptr){
            llvm::outs() << outputObject.dump(4) << "\n";
        }
    }

    /**
     * Print the provided JSON-Object as string
     * @param outputObject JSON-Object containing information about the analysis
     */
    static void outputMetricsPlain(json& outputObject, Mode mode){

        if(outputObject != nullptr){
            if(mode == Mode::PROGRAM){
                auto timeused = outputObject["duration"].get<double>();
                outputObject.erase("duration");

                for(auto functionObject : outputObject){
                    if(functionObject.contains("name")){
                        //llvm::errs() << functionObject.toStyledString() << "\n\n\n";
                        llvm::outs() << "\n";
                        llvm::outs() << "Function " << functionObject["name"].dump() << "\n";
                        llvm::outs() << "======================================================================" << "\n";
                        llvm::outs() << "Estimated energy consumption: " << functionObject["energy"].get<double>()  << " J\n";
                        llvm::outs() << "Number of basic blocks: " << functionObject["numberOfBasicBlocks"].get<int>()  << "\n";
                        llvm::outs() << "Number of instruction: " << functionObject["numberOfInstructions"].get<int>() << "\n";
                        llvm::outs() << "Ø energy per block: " << functionObject["averageEnergyPerBlock"].get<double>()  << " J\n";
                        llvm::outs() << "Ø energy per instruction: " << functionObject["averageEnergyPerInstruction"].get<double>() << " J\n";
                        llvm::outs() << "======================================================================" << "\n";
                        llvm::outs() << "\n";
                    }
                }
                llvm::outs() << "The Analysis took: " << timeused << " s\n";
            }else if(mode == Mode::BLOCK){

            }else{
                llvm::errs() << "Please specify the mode the pass should run on:\n\t-mode program analyzes the program starting in the main function\n\t-mode function analyzes all functions, without respect to calls" << "\n";
            }
        }
    }

    /**
     * Calculates ProgramGraph-representation of a function
     * @param function Function to construct the graph for
     * @param handler A LLVMHandler containing the energy-Model
     * @param FAM A llvm::FunctionAnalysisManager
     * @param analysisStrategy The strategy to analyze the function with
     * @return Returns the calculated ProgramGraph
     */
    static void constructProgramRepresentation(ProgramGraph* pGraph, EnergyFunction *energyFunc, LLVMHandler *handler, llvm::FunctionAnalysisManager *FAM, AnalysisStrategy::Strategy analysisStrategy){
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

                llvm::errs() << energyFunc->name << "\n";
                //Construct the LoopTree from the Information of the current top-level loop
                LoopTree *LT = new LoopTree(topLoop, topLoop->getSubLoops(), handler, &scalarEvolution);

                //Construct a LoopNode for the current loop
                LoopNode *loopNode = LoopNode::construct(LT, pGraph, analysisStrategy);
                //Replace the blocks used by loop in the previous created ProgramGraph
                pGraph->replaceNodesWithLoopNode(topLoop->getBlocksVector(), loopNode);
            }


            //energyCalculation(pGraph, handler, function);
            energyFunc->energy = pGraph->getEnergy(handler);

        }else{

            //energyCalculation(pGraph, handler, function);
            energyFunc->energy = pGraph->getEnergy(handler);
        }
    }

    /**
     * Function to run the analysis on a given module
     * @param module LLVM::Module to run the analysis on
     * @param MAM llvm::ModuleAnalysisManager
     * @param analysisStrategy Strategy to analyze the module with
     * @param maxiterations Upper bound of loops
     */
    void analysisRunner(llvm::Module &module, llvm::ModuleAnalysisManager &MAM, AnalysisStrategy::Strategy analysisStrategy, int maxiterations) {
        //Get the FunctionAnalysisManager from the ModuleAnalysisManager
        auto &functionAnalysisManager = MAM.getResult<llvm::FunctionAnalysisManagerModuleProxy>(module).getManager();

        //If a model was provided
        if( this->energyJson.contains("add") && this->energyJson.contains("urem") ){
            //Get the functions from the module
            auto funcList = &module.getFunctionList();
            FunctionTree * functionTree;

            //Construct the functionTrees to the functions of the module
            for(auto &function : *funcList){
                auto name = function.getName();
                if(name == "main"){
                    auto mainFunctionTree = FunctionTree::construct(&function);
                    functionTree = (mainFunctionTree);
                }
            }



            std::vector<llvm::StringRef> names;
            for (auto function: functionTree->getPreOrderVector()) {
                names.push_back(function->getName());
            }

            EnergyFunction funcPool[functionTree->getPreOrderVector().size()];


            for (int i=0; i < functionTree->getPreOrderVector().size(); i++) {
                //Construct a new EnergyFunction to the current function
                //auto newFuntion = new EnergyFunction(function);
                llvm::Function * function = functionTree->getPreOrderVector()[i];

                //Add the EnergyFunction to the queue
                //handler.funcqueue.push_back(newFuntion);
                //auto energyFunction = handler.funcmap.at(function->getName().str());

                funcPool[i].func = function;
                funcPool[i].name = DeMangler::demangle(function->getName().str());
            }

            //Init the LLVMHandler with the given model and the upper bound for unbounded loops
            LLVMHandler handler = LLVMHandler(this->energyJson, maxiterations, deepCallsEnabled, funcPool, functionTree->getPreOrderVector().size());

            for(int i = 0; i < functionTree->getPreOrderVector().size(); i++){
                llvm::Function * function = functionTree->getPreOrderVector()[i];

                //Check if the current function is external. Analysis of external functions, that only were declared, will result in an infinite loop
                if (!function->isDeclarationForLinker()) {
                    //Calculate the energy
                    constructProgramRepresentation(funcPool[i].programGraph, &funcPool[i], &handler, &functionAnalysisManager, analysisStrategy);
                    // Calculate the maximal amount of energy of the programgraph
                }else{
                    funcPool[i].programGraph = nullptr;
                }
            }

            this->stopwatch_end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> ms_double = this->stopwatch_end - this->stopwatch_start;

            double duration = ms_double.count()/1000;

            //Construct the output
            json output = constructOutputObject(funcPool, functionTree->getPreOrderVector().size(), duration,  this->mode, this->forFunction);

            if(format == Format::JSON){
                outputMetricsJSON(output, this->mode);
            }else if(format == Format::PLAIN){
                outputMetricsPlain(output, this->mode);
            }else{
                llvm::errs() << "Please provide a valid output format: plain/JSON" << "\n";
            }


        }else{
            llvm::errs() << "Please provide valid an energyfile" << "\n";
        }
    }

    /**
     * Main runner of the energy pass. The pass will apply module-wise.
     * @param module Reference to a Module
     * @param moduleAnalysisManager Reference to a ModuleAnalysisManager
     */
    llvm::PreservedAnalyses run(llvm::Module &module, llvm::ModuleAnalysisManager &moduleAnalysisManager) {

        //Get the int-value from the provided string
        int maxiterations = this->loopbound;


        //Check the analysis-strategy the user requestet
        if(this->strategy == Strategy::BEST){
            analysisRunner(module, moduleAnalysisManager, AnalysisStrategy::BESTCASE, maxiterations);
        }else if(this->strategy == Strategy::WORST){
            analysisRunner(module, moduleAnalysisManager, AnalysisStrategy::WORSTCASE, maxiterations);
        }else if(this->strategy == Strategy::AVERAGE){
            analysisRunner(module, moduleAnalysisManager, AnalysisStrategy::AVERAGECASE, maxiterations);
        }else{
            llvm::errs() << "Please provide a valid analysis strategy: best/worst/average" << "\n";
        }

        return llvm::PreservedAnalyses::all();

    }

    static bool isRequired() { return true; }
};



/**
 * Method for providing some basic information about the pass
 */
llvm::PassPluginLibraryInfo getEnergyPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "Energy", LLVM_VERSION_STRING,
            [](llvm::PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                        [](llvm::StringRef Name, llvm::ModulePassManager &modulePassManager,
                           llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) {
                            if (Name == "energy") {
                                modulePassManager.addPass(Energy());
                                return true;
                            }
                            return false;
                        });
            }};
}

//Register the pass in llvm, so it is useable with opt
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getEnergyPluginInfo();
}