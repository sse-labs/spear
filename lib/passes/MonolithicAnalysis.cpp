/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "MonolithicAnalysis.h"

#include "ConfigParser.h"
#include "HLAC/hlac.h"
#include "ILP/ILPUtil.h"
#include "Logger.h"
#include "PassUtil.h"
#include "nlohmann/json.hpp"

nlohmann::json MonolithicAnalysis::run(std::shared_ptr<HLAC::hlac> graph, bool showTimings) {
    Logger::getInstance().log("Running Monolithic ILP Analysis for Energy", LOGLEVEL::INFO);

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

        auto monoBuildEnd = std::chrono::high_resolution_clock::now();
        auto monoBuildDuration = std::chrono::duration_cast<std::chrono::microseconds>(
            monoBuildEnd - monoBuildStart);
        totalBuildDuration += monoBuildDuration;

        if (ilp.has_value()) {
            auto monoSolveStart = std::chrono::high_resolution_clock::now();

            // ILPUtil::printILPModelHumanReadable(funcNode->function->getName().str(), ilp.value());

            auto solvedResults = graph->solveMonolithicIlp(ilp.value());

            auto monoSolveEnd = std::chrono::high_resolution_clock::now();
            auto monoSolveDuration = std::chrono::duration_cast<std::chrono::microseconds>(
                monoSolveEnd - monoSolveStart);
            totalSolveDuration += monoSolveDuration;

            if (solvedResults.has_value()) {
                auto resultPair = solvedResults.value();
                auto funcName = funcNode->function->getName().str();

                auto funcEnergy = resultPair.optimalValue;
                graph->FunctionEnergyCache[funcNode->name] = funcEnergy;

                Logger::getInstance().log(
                    "Monolithic Energy of " + funcName + ": " + formatScientific(funcEnergy) + " J",
                    LOGLEVEL::HIGHLIGHT
                );

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
        logger.log("Monolithic ILP Build Time: " + std::to_string(totalBuildDuration.count()) + " µs",
                   LOGLEVEL::INFO);
        logger.log("Monolithic ILP Solve Time: " + std::to_string(totalSolveDuration.count()) + " µs",
                   LOGLEVEL::INFO);
        logger.log("Monolithic Total Time: " + std::to_string(monoTotalDuration.count()) + " µs",
                   LOGLEVEL::INFO);
    }

    return nlohmann::json::object();
}
