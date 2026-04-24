/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "ClusteredAnalysis.h"

#include <vector>
#include <string>
#include <unordered_map>

#include "ConfigParser.h"
#include "ILP/ILPClusterCache.h"
#include "ILP/ILPDebug.h"
#include "ILP/ILPUtil.h"
#include "Logger.h"
#include "PassUtil.h"
#include "ProfileHandler.h"

nlohmann::json ClusteredAnalysis::run(std::shared_ptr<HLAC::hlac> graph, bool showTimings, bool showAllTimings) {
    Logger::getInstance().log("Running Clustered ILP Analysis for Energy", LOGLEVEL::INFO);

    std::unordered_map<std::string, std::vector<ILPModel>> functionILPCache;

    std::string cacheActiveStr = (ConfigParser::getAnalysisConfiguration().cachingEnabled ? "enabled" : "disabled");
    Logger::getInstance().log("Cluster cache is " + cacheActiveStr, LOGLEVEL::INFO);

    bool clusteredCacheEnabled = ConfigParser::getAnalysisConfiguration().cachingEnabled;
    ILPClusterCache clusterCache("cluster_cache.json", clusteredCacheEnabled);

    auto totalGetEnergyInitDuration = std::chrono::microseconds::zero();
    auto totalBuildDuration = std::chrono::microseconds::zero();
    auto totalSolveDuration = std::chrono::microseconds::zero();
    auto totalDagDuration = std::chrono::microseconds::zero();

    std::unordered_map<HLAC::FunctionNode *, ILPClusteredLoopResult> clusteredLoopResults;

    auto clusteredTotalStart = std::chrono::high_resolution_clock::now();

    // ================= Clustered ILP  =================

    for (auto &funcNode : graph->functions) {
        auto getEnergyInitStart = std::chrono::high_resolution_clock::now();
        funcNode->nodeEnergy = funcNode->baseNodeEnergy;

        for (const auto &binding : funcNode->callNodeBindings) {
            auto cacheIterator = graph->FunctionEnergyCache.find(binding.calleeName);

            if (cacheIterator != graph->FunctionEnergyCache.end()) {
                funcNode->nodeEnergy[binding.nodeIndex] = cacheIterator->second;
            } else {
                // If we dont know the function, we need to get the energy
                double eng = binding.reference->getEnergy();
                funcNode->nodeEnergy[binding.nodeIndex] = eng;
            }
        }

        auto getEnergyInitEnd = std::chrono::high_resolution_clock::now();
        auto getEnergyInitDuration = std::chrono::duration_cast<std::chrono::microseconds>(
            getEnergyInitEnd - getEnergyInitStart);
        totalGetEnergyInitDuration += getEnergyInitDuration;

        auto clusteredBuildStart = std::chrono::high_resolution_clock::now();

        // build the clustered ILPs
        auto clusteredILPs = graph->buildClusteredILPS(funcNode.get());

        auto clusteredBuildEnd = std::chrono::high_resolution_clock::now();
        auto clusteredBuildDuration = std::chrono::duration_cast<std::chrono::microseconds>(
            clusteredBuildEnd - clusteredBuildStart);
        totalBuildDuration += clusteredBuildDuration;

        if (clusteredILPs.has_value()) {
            for (const auto& ilp : clusteredILPs.value()) {
                /*if (funcNode->function->getName().str() == "main") {
                    ILPDebug::dumpILPModel(ilp.second, funcNode->Edges, "main");
                }*/

                auto funcName = funcNode->function->getName().str();
                functionILPCache[funcName].push_back(ilp.second);
            }

            auto clusteredSolveStart = std::chrono::high_resolution_clock::now();

            // Solve the clustered ILPs of the program
            auto clusteredSolvedResults = HLAC::hlac::solveClusteredIlps(clusteredILPs.value());

            auto clusteredSolveEnd = std::chrono::high_resolution_clock::now();
            auto clusteredSolveDuration = std::chrono::duration_cast<std::chrono::microseconds>(
                clusteredSolveEnd - clusteredSolveStart);
            totalSolveDuration += clusteredSolveDuration;

            const auto& solvedResults = clusteredSolvedResults;
            clusteredLoopResults[funcNode.get()] = solvedResults;

            auto dagStart = std::chrono::high_resolution_clock::now();

            // Calculate the longest path (= most expensive path) from the clustered results
            auto dagResults = HLAC::hlac::DAGLongestPath(funcNode.get(), solvedResults);

            auto dagEnd = std::chrono::high_resolution_clock::now();
            auto dagDuration = std::chrono::duration_cast<std::chrono::microseconds>(
                dagEnd - dagStart);
            totalDagDuration += dagDuration;

            funcNode->nodeEnergy = std::vector<double>(funcNode->topologicalSortedRepresentationOfNodes.size(), 0.0);

            // Trace the taken path and print the result of the clustered approach
            if (dagResults.has_value()) {
                const auto& resultPair = dagResults.value();
                auto resVector = resultPair.longestPath;

                auto funcName = funcNode->function->getName().str();
                auto funcEnergy = resultPair.WCEC;

                // If we encounter the main function add the additional program start offset cost to it!
                if (funcName == "main") {
                    auto offsetCost = ProfileHandler::get_instance().getProgramOffset();
                    if (offsetCost.has_value()) {
                        funcEnergy += offsetCost.value();
                    }
                }

                graph->FunctionEnergyCache[funcNode->name] = funcEnergy;

                Logger::getInstance().log(
                    "Clustered Energy of " + funcName + ": " + PassUtil::formatScientific(funcEnergy) + " J",
                    LOGLEVEL::HIGHLIGHT);

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
        if (showAllTimings) {
            logger.log("Clustered getEnergy Init Time: " + std::to_string(totalGetEnergyInitDuration.count()) + " µs",
                   LOGLEVEL::INFO);
            logger.log("Clustered ILP Build Time: " + std::to_string(totalBuildDuration.count()) + " µs",
                       LOGLEVEL::INFO);
            logger.log("Clustered ILP Solve Time: " + std::to_string(totalSolveDuration.count()) + " µs",
                       LOGLEVEL::INFO);
            logger.log("Clustered DAG Time: " + std::to_string(totalDagDuration.count()) + " µs",
                       LOGLEVEL::INFO);
        }
        logger.log("Clustered Total Time: " + std::to_string(clusteredTotalDuration.count()) + " µs",
                   LOGLEVEL::INFO);
    }

    clusterCache.writeBackCache();

    nlohmann::json outputObject = nlohmann::json::object();
    outputObject["analysis"] = "clustered";
    outputObject["duration"] = clusteredTotalDuration.count();
    outputObject["functions"] = {};

    for (auto [funcname, energy] : graph->FunctionEnergyCache) {
        auto ilpVec = functionILPCache[funcname];
        auto ilpArr = nlohmann::json::array();

        for (const auto& ilp : ilpVec) {
            auto ilpObj = nlohmann::json::object();

            ilpObj["numVariables"] = ilp.col_lb.size();
            ilpObj["numConstrains"] = ilp.row_lb.size();

            ilpArr.push_back(ilpObj);
        }

        outputObject["functions"][funcname] = {
            {"energy", energy},
            {"ILPS", ilpArr}
        };

        auto funcNode = graph->getFunctionByName(funcname);
        auto loopres = clusteredLoopResults[funcNode];

        for ( auto &node : funcNode->Nodes ) {
            PassUtil::appendGraphContent(outputObject["functions"][funcname], node.get(), loopres);
        }
    }

    return outputObject;
}
