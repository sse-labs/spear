/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "ClusteredAnalysis.h"

#include "ConfigParser.h"
#include "ILP/ILPClusterCache.h"
#include "Logger.h"

nlohmann::json ClusteredAnalysis::run(std::shared_ptr<HLAC::hlac> graph, bool showTimings) {
    Logger::getInstance().log("Running Clustered ILP Analysis for Energy", LOGLEVEL::INFO);

    std::string cacheActiveStr = (ConfigParser::getAnalysisConfiguration().cachingEnabled ? "enabled" : "disabled");
    Logger::getInstance().log("Cluster cache is " + cacheActiveStr, LOGLEVEL::INFO);

    bool clusteredCacheEnabled = ConfigParser::getAnalysisConfiguration().cachingEnabled;
    ILPClusterCache clusterCache("cluster_cache.json", clusteredCacheEnabled);

    // ================= Clustered ILP  =================

    auto clusteredTotalStart = std::chrono::high_resolution_clock::now();

    auto clusteredBuildStart = std::chrono::high_resolution_clock::now();

    // build the clustered ILPs
    auto clusteredILPs = graph->buildClusteredILPS();
    auto clusteredBuildEnd = std::chrono::high_resolution_clock::now();

    auto clusteredBuildDuration = std::chrono::duration_cast<std::chrono::microseconds>(
        clusteredBuildEnd - clusteredBuildStart);

    auto clusteredSolveStart = std::chrono::high_resolution_clock::now();

    // Solve the clustered ILPs of the program
    auto clusteredSolvedResults = graph->solveClusteredIlps(clusteredILPs);
    auto clusteredSolveEnd = std::chrono::high_resolution_clock::now();

    auto clusteredSolveDuration = std::chrono::duration_cast<std::chrono::microseconds>(
        clusteredSolveEnd - clusteredSolveStart);


    auto clusteredDagStart = std::chrono::high_resolution_clock::now();

    // Calculate the longest path (= most expensive path) from the clustered results
    auto dagResults = graph->DAGLongestPath(clusteredSolvedResults);

    auto clusteredDagEnd = std::chrono::high_resolution_clock::now();
    auto clusteredDagDuration = std::chrono::duration_cast<std::chrono::microseconds>(
        clusteredDagEnd - clusteredDagStart);

    auto clusteredTotalEnd = std::chrono::high_resolution_clock::now();
    auto clusteredTotalDuration = std::chrono::duration_cast<std::chrono::microseconds>(
        clusteredTotalEnd - clusteredTotalStart);

    if (showTimings) {
        auto &logger = Logger::getInstance();
        logger.log("Clustered ILP Build Time: " + std::to_string(clusteredBuildDuration.count()) + " µs", LOGLEVEL::INFO);
        logger.log("Clustered ILP Solve Time: " + std::to_string(clusteredSolveDuration.count()) + " µs", LOGLEVEL::INFO);
        logger.log("DAG Longest Path Time: " + std::to_string(clusteredDagDuration.count()) + " µs", LOGLEVEL::INFO);
        logger.log("Clustered Total Time: " + std::to_string(clusteredTotalDuration.count()) + " µs", LOGLEVEL::INFO);
    }

    // Trace the taken path and print the result of the clustered approach
    for (const auto &[funcName, resultpair] : dagResults) {
        auto resVector = resultpair.longestPath;

        auto loopResults = clusteredSolvedResults[funcName];

        // std::cout << "Clustered Energy of " << funcName << ": " << resultpair.WCEC << " J\n";
        graph->printDotRepresentationWithSolution(graph->getFunctionByName(funcName), resVector, "clustered");
    }

    clusterCache.writeBackCache();

    return json::object();
}
