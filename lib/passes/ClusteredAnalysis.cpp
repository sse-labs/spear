/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "ClusteredAnalysis.h"

#include "ConfigParser.h"
#include "ILP/ILPClusterCache.h"
#include "Logger.h"
#include "PassUtil.h"

nlohmann::json ClusteredAnalysis::run(std::shared_ptr<HLAC::hlac> graph, bool showTimings) {
    Logger::getInstance().log("Running Clustered ILP Analysis for Energy", LOGLEVEL::INFO);

    std::string cacheActiveStr = (ConfigParser::getAnalysisConfiguration().cachingEnabled ? "enabled" : "disabled");
    Logger::getInstance().log("Cluster cache is " + cacheActiveStr, LOGLEVEL::INFO);

    bool clusteredCacheEnabled = ConfigParser::getAnalysisConfiguration().cachingEnabled;
    ILPClusterCache clusterCache("cluster_cache.json", clusteredCacheEnabled);

    // ================= Clustered ILP  =================

    auto clusteredTotalStart = std::chrono::high_resolution_clock::now();

    auto totalBuildDuration = std::chrono::microseconds::zero();
    auto totalSolveDuration = std::chrono::microseconds::zero();
    auto totalDagDuration = std::chrono::microseconds::zero();

    for (auto &funcNode : graph->functions) {
        auto &sortedNodeList = funcNode->topologicalSortedRepresentationOfNodes;
        auto &nodeEnergy = funcNode->nodeEnergy;

        if (nodeEnergy.size() != sortedNodeList.size()) {
            nodeEnergy.resize(sortedNodeList.size());
        }

        std::fill(nodeEnergy.begin(), nodeEnergy.end(), 0.0);

        for (std::size_t index = 0; index < sortedNodeList.size(); ++index) {
            HLAC::GenericNode *currentNode = sortedNodeList[index];

            if (dynamic_cast<HLAC::LoopNode *>(currentNode) != nullptr) {
                continue;
            }

            nodeEnergy[index] = currentNode->getEnergy();
        }

        auto clusteredBuildStart = std::chrono::high_resolution_clock::now();

        // build the clustered ILPs
        auto clusteredILPs = graph->buildClusteredILPS(funcNode.get());

        auto clusteredBuildEnd = std::chrono::high_resolution_clock::now();
        auto clusteredBuildDuration = std::chrono::duration_cast<std::chrono::microseconds>(
            clusteredBuildEnd - clusteredBuildStart);
        totalBuildDuration += clusteredBuildDuration;

        if (clusteredILPs.has_value()) {
            auto clusteredSolveStart = std::chrono::high_resolution_clock::now();

            // Solve the clustered ILPs of the program
            auto clusteredSolvedResults = graph->solveClusteredIlps(clusteredILPs.value());

            auto clusteredSolveEnd = std::chrono::high_resolution_clock::now();
            auto clusteredSolveDuration = std::chrono::duration_cast<std::chrono::microseconds>(
                clusteredSolveEnd - clusteredSolveStart);
            totalSolveDuration += clusteredSolveDuration;

            auto solvedResults = clusteredSolvedResults;

            auto dagStart = std::chrono::high_resolution_clock::now();

            // Calculate the longest path (= most expensive path) from the clustered results
            auto dagResults = graph->DAGLongestPath(funcNode.get(), solvedResults);

            auto dagEnd = std::chrono::high_resolution_clock::now();
            auto dagDuration = std::chrono::duration_cast<std::chrono::microseconds>(
                dagEnd - dagStart);
            totalDagDuration += dagDuration;

            funcNode->nodeEnergy = std::vector<double>(funcNode->topologicalSortedRepresentationOfNodes.size(), 0.0);

            // Trace the taken path and print the result of the clustered approach
            if (dagResults.has_value()) {
                auto resultPair = dagResults.value();
                auto resVector = resultPair.longestPath;
                auto funcName = funcNode->function->getName().str();
                auto funcEnergy = resultPair.WCEC;

                graph->FunctionEnergyCache[funcNode->name] = funcEnergy;

                Logger::getInstance().log(
                    "Clustered Energy of " + funcName + ": " + formatScientific(funcEnergy) + " J",
                    LOGLEVEL::HIGHLIGHT
                );

                if (ConfigParser::getAnalysisConfiguration().writeDotFiles) {
                    graph->printDotRepresentationWithSolution(
                        graph->getFunctionByName(funcName),
                        resVector,
                        "clustered");
                }
            }
        }
    }

    auto clusteredTotalEnd = std::chrono::high_resolution_clock::now();
    auto clusteredTotalDuration = std::chrono::duration_cast<std::chrono::microseconds>(
        clusteredTotalEnd - clusteredTotalStart);

    if (showTimings) {
        auto &logger = Logger::getInstance();
        logger.log("Clustered ILP Build Time: " + std::to_string(totalBuildDuration.count()) + " µs",
                   LOGLEVEL::INFO);
        logger.log("Clustered ILP Solve Time: " + std::to_string(totalSolveDuration.count()) + " µs",
                   LOGLEVEL::INFO);
        logger.log("Clustered DAG Time: " + std::to_string(totalDagDuration.count()) + " µs",
                   LOGLEVEL::INFO);
        logger.log("Clustered Total Time: " + std::to_string(clusteredTotalDuration.count()) + " µs",
                   LOGLEVEL::INFO);
    }

    clusterCache.writeBackCache();

    return json::object();
}
