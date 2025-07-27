#include <cstring>
#include "src/main/include/Profiler/Profiler.h"
#include "src/main/include/LLVM-Handler/LLVMHandler.h"
#include "src/main/include/LLVM-Handler/InstructionCategory.h"
#include "src/main/include/JSON-Handler/JSONHandler.h"
#include "src/main/passes/energy/energy.cpp"

#include "iostream"
#include "filesystem"
#include <stdexcept>
#include <chrono>
#include <getopt.h>
#include <csignal>

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Analysis/RegionPass.h"
#include "llvm/Transforms/Utils/InstructionNamer.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/Scalar/LoopRotation.h"

#include <filesystem>

#include "src/main/include/CLIHandler/CLIHandler.h"


void runProfileRoutine(CLIOptions opts){
    //Get the parameters from the arguments
    int rep = opts.repeatAmount;
    std::string compiledPath = opts.codePath;

    std::vector<std::string> filenames;
    for (const auto& entry : std::filesystem::directory_iterator(compiledPath + "/")) {
        if (entry.is_regular_file()) {
            std::string filename = entry.path().filename().string();
            std::cout << filename << std::endl;
            filenames.push_back(entry.path().filename().string()); // only the file name, not full path
        }
    }

    std::map<std::string, std::string> profileCode;
    /*profileCode["call"] = compiledPath + "/" + "call";
    profileCode["memory"] = compiledPath + "/" + "memoryread";
    profileCode["programflow"] = compiledPath + "/" + "programflow";
    profileCode["division"] = compiledPath + "/" + "division";
    profileCode["others"] = compiledPath + "/" + "stdbinary";*/

    for (const std::string& filename : filenames) {
        profileCode[filename] = compiledPath + "/" + filename;
    }

    /*std::cout << profileCode["call"] << " -> " << std::filesystem::exists(profileCode["call"])  << std::endl;
    std::cout << profileCode["memory"] << " -> " << std::filesystem::exists(profileCode["memory"]) << std::endl;
    std::cout << profileCode["programflow"] << " -> " << std::filesystem::exists(profileCode["programflow"]) << std::endl;
    std::cout << profileCode["division"] << " -> " << std::filesystem::exists(profileCode["division"]) << std::endl;
    std::cout << profileCode["others"] << " -> " << std::filesystem::exists(profileCode["others"]) << std::endl;*/

    if(true){

        std::cout << "Starting the profile..." << std::endl;

        Profiler profiler = Profiler(rep, &profileCode);

        //Start the time measurement
        auto start = std::chrono::system_clock::now();
        //Launch the benchmarking
        try{
            std::map<std::string, double> result = profiler.profile();
            //Stop the time measurement
            auto end = std::chrono::system_clock::now();
            //Calculate the elapsed time by substracting the two timestamps
            std::chrono::duration<double> timerun = end - start;

            std::stringstream starttimestream;
            std::stringstream endtimestream;
            starttimestream << start.time_since_epoch().count();
            endtimestream << end.time_since_epoch().count();

            std::map<std::string, std::string> cpu = {
                    {"name", Profiler::getCPUName()},
                    {"architecture", Profiler::getArchitecture()},
                    {"cores", Profiler::getNumberOfCores()}
            };

            //Group the vector format of the results
            std::map<std::string, double> data = {};

            for (const auto& [key, value] : result) {
                data[key] = value;
            }

            //Pass the grouped values to the csv handler, so it can be written to a file
            //CSVHandler::writeCSV("benchmarkresult.csv", ',' , data);
            char *outputpath = new char[255];
            sprintf(outputpath, "%s/profile.json", opts.saveLocation.c_str());
            std::cout << "Writing " << outputpath << "\n";
            JSONHandler::write(outputpath, cpu, starttimestream.str(), endtimestream.str(), std::to_string(profiler.repetitions),  data, Profiler::getUnit());

            std::cout << "Profiling finished!" << std::endl;
            std::cout << "Elapsed Time: " << timerun.count() << "s" << std::endl;
        }catch(std::invalid_argument &ia){
            std::cerr << "Execution of profile code failed..." << "\n";
        }
    }else{
        std::cerr << "The given path to the profile does not contain a /src/compiled folder!" << "\n";
    }


}

void runAnalysisRoutine(CLIOptions opts){
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

    //instname
    functionPassManager.addPass(llvm::InstructionNamerPass());
    //mem2reg
    functionPassManager.addPass(llvm::PromotePass());

    //loop-simplify
    functionPassManager.addPass(llvm::LoopSimplifyPass());

    //loop-rotate
    functionPassManager.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::LoopRotatePass()));
    modulePassManager.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(functionPassManager)));



    modulePassManager.addPass(Energy(opts.profilePath, opts.mode, opts.format, opts.strategy, opts.loopBound, opts.deepCalls, opts.forFunction));
    modulePassManager.run(*module_up, moduleAnalysisManager);
}


int main(int argc, char *argv[]){
    std::string helpString = "Usage: spear option <arguments>\n==============================\nOptions:"
                             "\n\tprofile\t Profile the system and generate the estimated energy usage of the device. Used for any further analysis"
                             "\n\tanalyze\t Analyzes a given program. Further parameters are needed:"
                             "\n\t\t\t --mode Type of analysis (program/function)"
                             "\n\t\t\t --format Format of the result to print (plain/json)"
                             "\n\t\t\t --strategy Type of analysis-strategy (worst/best/average)"
                             "\n\t\t\t --loopbound Value with with which loops get approximed if their upper bound can't be calculated (0 - INT_MAX)"
                             "\n\n";

    if(argc > 1){
        CLIOptions opts = CLIHandler::parseCLI(argc, argv);

        if(opts.operation == Operation::PROFILE){
            // Check if the parser returned valid options
            if(!opts.codePath.empty() && opts.repeatAmount != -1 && !opts.saveLocation.empty()){
                //std::cout << "Options valid" << std::endl;
                runProfileRoutine(opts);
                return 0;
            }else{
                std::string profileHelpMsg =  "Usage: spear profile <arguments>\n==============================\nArguments:"
                                              "\n\t Profile the system and generate the estimated energy usage of the device. Used for any further analysis"
                                              "\n\t\t --iterations Amount of measurement repetitions (int)"
                                              "\n\t\t --model Path to the compiled profile programs"
                                              "\n\t\t --savelocation Path the calculated profile will be saved to"
                                              "\n\n";
                std::cerr << profileHelpMsg << std::endl;
                return 1;
            }
        }else if(opts.operation == Operation::ANALYZE){
            // Check if the parser returned valid options
            if(!opts.profilePath.empty() && opts.mode != Mode::UNDEFINED && opts.format != Format::UNDEFINED && opts.strategy != Strategy::UNDEFINED && opts.loopBound != -1 && !opts.programPath.empty()){
                //std::cout << "Options valid" << std::endl;
                runAnalysisRoutine(opts);
                return 0;
            }else{
                std::string profileHelpMsg = "Usage: spear analyze <arguments>\n==============================\nArguments:"
                                             "\n\tAnalyzes a given program. Further parameters are needed:"
                                             "\n\t\t --mode Type of analysis (program/function)"
                                             "\n\t\t --format Format of the result to print (plain/json)"
                                             "\n\t\t --strategy Type of analysis-strategy (worst/best/average)"
                                             "\n\t\t --loopbound Value with with which loops get approximed if their upper bound can't be calculated (0 - INT_MAX)"
                                             "\n\n";
                std::cerr << profileHelpMsg << std::endl;
                return 1;
            }
        }else{
            return 1;
        }
    }else{
        std::cout << helpString;
    }
}