/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "MonolithicAnalysis.h"

#include "HLAC/hlac.h"
#include "Logger.h"
#include "PassUtil.h"
#include "nlohmann/json.hpp"

nlohmann::json MonolithicAnalysis::run(std::shared_ptr<HLAC::hlac> graph, bool showTimings) {
    Logger::getInstance().log("Running Monolithic ILP Analysis for Energy", LOGLEVEL::INFO);

    // ================= Monolithic ILP =================

    auto monoTotalStart = std::chrono::high_resolution_clock::now();

    auto totalBuildDuration = std::chrono::microseconds::zero();
    auto totalSolveDuration = std::chrono::microseconds::zero();

    for (auto &funcNode : graph->functions) {
        auto monoBuildStart = std::chrono::high_resolution_clock::now();

        // Build one big ILP for the program under analysis
        auto ilp = graph->buildMonolithicILP(funcNode.get());

        auto monoBuildEnd = std::chrono::high_resolution_clock::now();
        auto monoBuildDuration = std::chrono::duration_cast<std::chrono::microseconds>(
            monoBuildEnd - monoBuildStart);
        totalBuildDuration += monoBuildDuration;

        if (ilp.has_value()) {
            auto monoSolveStart = std::chrono::high_resolution_clock::now();

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

                /*graph->printDotRepresentationWithSolution(
                    graph->getFunctionByName(funcName),
                    resultPair.variableValues,
                    "monolithic");*/
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
