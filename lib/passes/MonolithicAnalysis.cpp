/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "MonolithicAnalysis.h"

#include "HLAC/hlac.h"
#include "Logger.h"
#include "nlohmann/json.hpp"

nlohmann::json MonolithicAnalysis::run(std::shared_ptr<HLAC::hlac> graph, bool showTimings) {
    Logger::getInstance().log("Running Monolithic ILP Analysis for Energy", LOGLEVEL::INFO);
    // ================= Monolithic ILP =================

    auto monoTotalStart = std::chrono::high_resolution_clock::now();

    auto monoBuildStart = std::chrono::high_resolution_clock::now();

    // Build one big ILP for the program under analysis
    auto ilps = graph->buildMonolithicILPS();
    auto monoBuildEnd = std::chrono::high_resolution_clock::now();

    auto monoBuildDuration = std::chrono::duration_cast<std::chrono::microseconds>(
        monoBuildEnd - monoBuildStart);


    auto monoSolveStart = std::chrono::high_resolution_clock::now();

    // Calculate the result of the big ILP
    auto solvedResults = graph->solveMonolithicIlps(ilps);
    auto monoSolveEnd = std::chrono::high_resolution_clock::now();

    auto monoSolveDuration = std::chrono::duration_cast<std::chrono::microseconds>(
        monoSolveEnd - monoSolveStart);


    auto monoTotalEnd = std::chrono::high_resolution_clock::now();

    auto monoTotalDuration = std::chrono::duration_cast<std::chrono::microseconds>(
        monoTotalEnd - monoTotalStart);


    if (showTimings) {
        auto &logger = Logger::getInstance();
        logger.log("Monolithic ILP Build Time: " + std::to_string(monoBuildDuration.count()) + " µs", LOGLEVEL::INFO);
        logger.log("Monolithic ILP Solve Time: " + std::to_string(monoSolveDuration.count()) + " µs", LOGLEVEL::INFO);
        logger.log("Monolithic Total Time: " + std::to_string(monoTotalDuration.count()) + " µs", LOGLEVEL::INFO);
    }

    for (const auto &[funcName, resultpair] : solvedResults) {
        // std::cout << "Monolithic Energy of " << funcName << ": " << resultpair.optimalValue << " J\n";

        graph->printDotRepresentationWithSolution(
            graph->getFunctionByName(funcName),
            resultpair.variableValues,
            "monolithic");
    }


    return nlohmann::json::object();
}
