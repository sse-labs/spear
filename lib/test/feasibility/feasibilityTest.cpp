/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <catch2/catch_test_macros.hpp>
#include "../testutils.h"

TestConfig feasibilityConfig = {.runFeasibilityAnalysis = true, .runLoopBoundAnalysis = false};

TEST_CASE("feasibility_simple.ll") {
    auto Run = runSpearOnFile(std::filesystem::path(TEST_INPUT_DIR),
                              "programs/feasibility/compiled/feasibility_simple.ll", feasibilityConfig, true);

    auto feasibilityMap = Run->phasarHandler.queryFeasibilty();

    auto feasibilityOfFunction = feasibilityMap["main"];

    CHECK_INFEASIBLE_BLOCKS_STRICT(&feasibilityOfFunction, {"if.then"}, {"entry", "if.then", "if.end"});
}

TEST_CASE("feasibility_edit.ll") {
    auto Run = runSpearOnFile(std::filesystem::path(TEST_INPUT_DIR),
                              "programs/feasibility/compiled/feasibility_edit.ll", feasibilityConfig, true);

    auto feasibilityMap = Run->phasarHandler.queryFeasibilty();
    auto feasibilityOfFunction = feasibilityMap["main"];

    CHECK_INFEASIBLE_BLOCKS_STRICT(&feasibilityOfFunction, {}, {"entry", "if.then", "if.end"});
}

TEST_CASE("feasibility_bothloads.ll") {
    auto Run = runSpearOnFile(std::filesystem::path(TEST_INPUT_DIR),
                              "programs/feasibility/compiled/feasibility_bothloads.ll", feasibilityConfig, true);

    auto feasibilityMap = Run->phasarHandler.queryFeasibilty();
    auto feasibilityOfFunction = feasibilityMap["main"];

    CHECK_INFEASIBLE_BLOCKS_STRICT(&feasibilityOfFunction, {"if.then"}, {"entry", "if.then", "if.end"});
}

TEST_CASE("feasibility_two_ssa.ll") {
    auto Run = runSpearOnFile(std::filesystem::path(TEST_INPUT_DIR),
                              "programs/feasibility/compiled/feasibility_two_ssa.ll", feasibilityConfig, true);

    auto feasibilityMap = Run->phasarHandler.queryFeasibilty();
    auto feasibilityOfFunction = feasibilityMap["main"];

    CHECK_INFEASIBLE_BLOCKS_STRICT(&feasibilityOfFunction, {"if.then6"},
                                   {"entry", "if.then", "if.then4", "if.then6", "if.end", "if.end7", "if.end8"});
}

TEST_CASE("feasibility_nested_one_wrong.ll") {
    auto Run = runSpearOnFile(std::filesystem::path(TEST_INPUT_DIR),
                              "programs/feasibility/compiled/feasibility_nested_one_wrong.ll", feasibilityConfig, true);

    auto feasibilityMap = Run->phasarHandler.queryFeasibilty();
    auto feasibilityOfFunction = feasibilityMap["main"];

    CHECK_INFEASIBLE_BLOCKS_STRICT(&feasibilityOfFunction, {"if.then2"},                  // expected infeasible
                                   {"entry", "if.then", "if.then2", "if.end", "if.end3"});
}

TEST_CASE("feasibility_nested_both_wrong.ll") {
    auto Run =
            runSpearOnFile(std::filesystem::path(TEST_INPUT_DIR),
                           "programs/feasibility/compiled/feasibility_nested_both_wrong.ll", feasibilityConfig, true);

    auto feasibilityMap = Run->phasarHandler.queryFeasibilty();
    auto feasibilityOfFunction = feasibilityMap["main"];

    CHECK_INFEASIBLE_BLOCKS_STRICT(&feasibilityOfFunction, {"if.then"}, {"entry", "if.then", "if.end3"});
}

TEST_CASE("feasibility_two_ssa_sq.ll") {
    auto Run = runSpearOnFile(std::filesystem::path(TEST_INPUT_DIR),
                              "programs/feasibility/compiled/feasibility_two_ssa_sq.ll", feasibilityConfig, true);

    auto feasibilityMap = Run->phasarHandler.queryFeasibilty();
    auto feasibilityOfFunction = feasibilityMap["main"];

    CHECK_INFEASIBLE_BLOCKS_STRICT(
            &feasibilityOfFunction, {"if.then6",   "if.then12",  "if.then21",  "if.then31",  "if.then41",  "if.then51",
                                     "if.then61",  "if.then71",  "if.then81",  "if.then91",  "if.then101", "if.then111",
                                     "if.then121", "if.then131", "if.then141", "if.then151", "if.then161", "if.then171",
                                     "if.then181", "if.then191", "if.then201", "if.then211"},
            {"entry",      "if.then",    "if.then4",   "if.then6",   "if.end",     "if.end7",    "if.end8",
             "if.then10",  "if.then12",  "if.end14",   "if.end15",   "if.then17",  "if.then19",  "if.then21",
             "if.end23",   "if.end24",   "if.end25",   "if.then27",  "if.then29",  "if.then31",  "if.end33",
             "if.end34",   "if.end35",   "if.then37",  "if.then39",  "if.then41",  "if.end43",   "if.end44",
             "if.end45",   "if.then47",  "if.then49",  "if.then51",  "if.end53",   "if.end54",   "if.end55",
             "if.then57",  "if.then59",  "if.then61",  "if.end63",   "if.end64",   "if.end65",   "if.then67",
             "if.then69",  "if.then71",  "if.end73",   "if.end74",   "if.end75",   "if.then77",  "if.then79",
             "if.then81",  "if.end83",   "if.end84",   "if.end85",   "if.then87",  "if.then89",  "if.then91",
             "if.end93",   "if.end94",   "if.end95",   "if.then97",  "if.then99",  "if.then101", "if.end103",
             "if.end104",  "if.end105",  "if.then107", "if.then109", "if.then111", "if.end113",  "if.end114",
             "if.end115",  "if.then117", "if.then119", "if.then121", "if.end123",  "if.end124",  "if.end125",
             "if.then127", "if.then129", "if.then131", "if.end133",  "if.end134",  "if.end135",  "if.then137",
             "if.then139", "if.then141", "if.end143",  "if.end144",  "if.end145",  "if.then147", "if.then149",
             "if.then151", "if.end153",  "if.end154",  "if.end155",  "if.then157", "if.then159", "if.then161",
             "if.end163",  "if.end164",  "if.end165",  "if.then167", "if.then169", "if.then171", "if.end173",
             "if.end174",  "if.end175",  "if.then177", "if.then179", "if.then181", "if.end183",  "if.end184",
             "if.end185",  "if.then187", "if.then189", "if.then191", "if.end193",  "if.end194",  "if.end195",
             "if.then197", "if.then199", "if.then201", "if.end203",  "if.end204",  "if.end205",  "if.then207",
             "if.then209", "if.then211", "if.end213",  "if.end214",  "if.end215"});
}

TEST_CASE("feasibility_wide.ll") {
    auto Run = runSpearOnFile(std::filesystem::path(TEST_INPUT_DIR),
                              "programs/feasibility/compiled/feasibility_wide.ll", feasibilityConfig, true);

    auto feasibilityMap = Run->phasarHandler.queryFeasibilty();
    auto feasibilityOfFunction = feasibilityMap["main"];

    CHECK_INFEASIBLE_BLOCKS_STRICT(&feasibilityOfFunction, {"if.then", "if.then2"},
                                   {"entry", "if.then", "if.then2", "if.end", "if.end3"});
}
