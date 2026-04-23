/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "MonolithicAnalysis.h"

#include <vector>
#include <string>
#include <unordered_map>

#include "ConfigParser.h"
#include "HLAC/hlac.h"
#include "HLAC/util.h"
#include "ILP/ILPBuilder.h"
#include "ILP/ILPUtil.h"
#include "Logger.h"
#include "PassUtil.h"
#include "ProfileHandler.h"
#include "nlohmann/json.hpp"

nlohmann::json MonolithicAnalysis::run(std::shared_ptr<HLAC::hlac> graph, bool showTimings, bool showAllTimings) {
    Logger::getInstance().log("Running Monolithic ILP Analysis for Energy", LOGLEVEL::INFO);
    std::unordered_map<std::string, std::optional<ILPModel>> functionILPCache;

    // ================= Monolithic ILP =================

    auto monoTotalStart = std::chrono::high_resolution_clock::now();

    auto totalGetEnergyInitDuration = std::chrono::microseconds::zero();
    auto totalBuildDuration = std::chrono::microseconds::zero();
    auto totalSolveDuration = std::chrono::microseconds::zero();

    for (auto &funcNode : graph->functions) {
        auto getEnergyInitStart = std::chrono::high_resolution_clock::now();
        funcNode->nodeEnergy = funcNode->baseNodeEnergy;

        for (const auto &binding : funcNode->callNodeBindings) {
            auto cacheIterator = graph->FunctionEnergyCache.find(binding.calleeName);

            if (cacheIterator != graph->FunctionEnergyCache.end()) {
                funcNode->nodeEnergy[binding.nodeIndex] = cacheIterator->second;
            } else {
                funcNode->nodeEnergy[binding.nodeIndex] = 0.0;
            }
        }

        auto getEnergyInitEnd = std::chrono::high_resolution_clock::now();
        auto getEnergyInitDuration = std::chrono::duration_cast<std::chrono::microseconds>(
            getEnergyInitEnd - getEnergyInitStart);
        totalGetEnergyInitDuration += getEnergyInitDuration;

        auto monoBuildStart = std::chrono::high_resolution_clock::now();

        // Build one big ILP for the program under analysis
        auto ilp = graph->buildMonolithicILP(funcNode.get());

        if (!ilp.has_value()) {
            if (!HLAC::Util::starts_with(funcNode->name, "__psr")
                && !HLAC::Util::starts_with(funcNode->name, "__clang")) {
                Logger::getInstance().log(
                "Failed to build monolithic ILP for function " + funcNode->name,
                LOGLEVEL::ERROR);
            }
            continue;
        }

        // auto solvedResults = graph->solveMonolithicIlp(ilp.value());

        auto monoBuildEnd = std::chrono::high_resolution_clock::now();
        auto monoBuildDuration = std::chrono::duration_cast<std::chrono::microseconds>(
            monoBuildEnd - monoBuildStart);
        totalBuildDuration += monoBuildDuration;

        if (ilp.has_value()) {
            auto funcName = funcNode->function->getName().str();
            auto monoSolveStart = std::chrono::high_resolution_clock::now();

            // ILPUtil::printILPModelHumanReadable(funcNode->function->getName().str(), ilp.value());

            functionILPCache[funcName] = ilp;

            auto solvedResults = graph->solveMonolithicIlp(ilp.value());

            if (!solvedResults.has_value()) {
                Logger::getInstance().log(
                    "Failed to solve monolithic ILP for function " + funcNode->name,
                    LOGLEVEL::ERROR);

                auto hmmm = ConfigParser::getAnalysisConfiguration().fallback["calls"]["UNKNOWN_FUNCTION"];
                graph->FunctionEnergyCache[funcNode->name] = hmmm;
            }

            auto monoSolveEnd = std::chrono::high_resolution_clock::now();
            auto monoSolveDuration = std::chrono::duration_cast<std::chrono::microseconds>(
                monoSolveEnd - monoSolveStart);
            totalSolveDuration += monoSolveDuration;

            /*funcNode->nodeEnergy = std::vector<double>(
                funcNode->topologicalSortedRepresentationOfNodes.size(), 0.0);*/

            if (solvedResults.has_value()) {
                auto resultPair = solvedResults.value();

                auto funcEnergy = resultPair.optimalValue;

                // If we encounter the main function add the additional program start offset cost to it!
                if (funcName == "main") {
                    auto offsetCost = ProfileHandler::get_instance().getProgramOffset();
                    if (offsetCost.has_value()) {
                        funcEnergy += offsetCost.value();
                    }
                }

                graph->FunctionEnergyCache[funcNode->name] = funcEnergy;

                Logger::getInstance().log(
                    "Monolithic Energy of " + funcName + ": " + PassUtil::formatScientific(funcEnergy) + " J",
                    LOGLEVEL::HIGHLIGHT);

                if (ConfigParser::getAnalysisConfiguration().writeDotFiles) {
                    graph->printDotRepresentationWithSolution(
                    graph->getFunctionByName(funcName),
                    resultPair.variableValues,
                    "monolithic");
                }
            }
        }
    }

    auto monoTotalEnd = std::chrono::high_resolution_clock::now();

    auto monoTotalDuration = std::chrono::duration_cast<std::chrono::microseconds>(
        monoTotalEnd - monoTotalStart);

    if (showTimings) {
        auto &logger = Logger::getInstance();
        if (showAllTimings) {
            logger.log("Monolithic getEnergy Init Time: " + std::to_string(totalGetEnergyInitDuration.count()) + " µs",
                   LOGLEVEL::INFO);
            logger.log("Monolithic ILP Build Time: " + std::to_string(totalBuildDuration.count()) + " µs",
                       LOGLEVEL::INFO);
            logger.log("Monolithic ILP Solve Time: " + std::to_string(totalSolveDuration.count()) + " µs",
                       LOGLEVEL::INFO);
        }
        logger.log("Monolithic Total Time: " + std::to_string(monoTotalDuration.count()) + " µs",
                   LOGLEVEL::INFO);
    }

    nlohmann::json outputObject = nlohmann::json::object();
    outputObject["analysis"] = "monolithic";
    outputObject["duration"] = monoTotalDuration.count();
    outputObject["functions"] = {};

    for (const auto& [functionName, energy] : graph->FunctionEnergyCache) {
        auto ilpArr = nlohmann::json::array();
        auto ilpObj = nlohmann::json::object();

        auto ilpIterator = functionILPCache.find(functionName);
        if (ilpIterator != functionILPCache.end() && ilpIterator->second.has_value()) {
            const auto& ilpModel = ilpIterator->second.value();
            ilpObj["numVariables"] = ilpModel.col_lb.size();
            ilpObj["numConstrains"] = ilpModel.row_lb.size();
            ilpObj["status"] = "solved";
        } else {
            ilpObj["numVariables"] = 0;
            ilpObj["numConstrains"] = 0;
            ilpObj["status"] = "fallback";
        }

        ilpArr.push_back(ilpObj);

        outputObject["functions"][functionName] = {
            {"energy", energy},
            {"ILPS", ilpArr}
        };

        auto functionNode = graph->getFunctionByName(functionName);
        for (auto& node : functionNode->Nodes) {
            PassUtil::appendGraphContent(outputObject["functions"][functionName], node.get());
        }
    }

    return outputObject;
}
