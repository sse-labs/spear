/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <catch2/catch_test_macros.hpp>
#include "../testutils.h"

TestConfig loopBoundConfig = {
    .runFeasibilityAnalysis = false,
    .runLoopBoundAnalysis = true
};

TEST_CASE("Arrayreducer_simple.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_simple.ll",
        loopBoundConfig);

    auto classifierMap = Run->phasarHandler.queryLoopBounds();

    auto mainClassifiers = classifierMap["main"];

    INFO("Classifier size " << mainClassifiers.size());
    REQUIRE(mainClassifiers.size() == 1);

    auto firstClassifier = mainClassifiers["for.cond"];

    INFO("Classifier increment " << "[" << firstClassifier.getLowerBound() << ", " << firstClassifier.getUpperBound() << "]");
    REQUIRE(firstClassifier == LoopBound::DeltaInterval::interval(
        9000, 9000, LoopBound::DeltaInterval::ValueType::Additive));
}

TEST_CASE("Arrayreducer_complex.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_complex.ll",
        loopBoundConfig);

    auto classifierMap = Run->phasarHandler.queryLoopBounds();

    auto mainClassifiers = classifierMap["main"];

    INFO("Classifier size " << mainClassifiers.size());
    REQUIRE(mainClassifiers.size() == 1);

    auto firstClassifier = mainClassifiers["for.cond"];

    INFO("Classifier increment " << "[" << firstClassifier.getLowerBound() << ", " << firstClassifier.getUpperBound() << "]");
    REQUIRE(firstClassifier == LoopBound::DeltaInterval::interval(
        2250, 2250, LoopBound::DeltaInterval::ValueType::Additive));
}

TEST_CASE("Arrayreducer_while.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_while.ll",
        loopBoundConfig);

    auto classifierMap = Run->phasarHandler.queryLoopBounds();

    auto mainClassifiers = classifierMap["main"];

    INFO("Classifier size " << mainClassifiers.size());
    REQUIRE(mainClassifiers.size() == 1);

    auto firstClassifier = mainClassifiers["while.cond"];

    INFO("Classifier increment " << "[" << firstClassifier.getLowerBound() << ", " << firstClassifier.getUpperBound() << "]");
    REQUIRE(firstClassifier == LoopBound::DeltaInterval::interval(
        3000, 3000, LoopBound::DeltaInterval::ValueType::Additive));
}

TEST_CASE("Arrayreducer_whileif.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_whileif.ll",
        loopBoundConfig);

    auto classifierMap = Run->phasarHandler.queryLoopBounds();

    auto mainClassifiers = classifierMap["main"];

    INFO("Classifier size " << mainClassifiers.size());
    REQUIRE(mainClassifiers.size() == 1);

    auto firstClassifier = mainClassifiers["while.cond"];

    INFO("Classifier increment " << "[" << firstClassifier.getLowerBound() << ", " << firstClassifier.getUpperBound() << "]");
    REQUIRE(firstClassifier == LoopBound::DeltaInterval::interval(
        2250, 3000, LoopBound::DeltaInterval::ValueType::Additive));
}

TEST_CASE("Arrayreducer_multiply.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_multiply.ll",
        loopBoundConfig);

    auto classifierMap = Run->phasarHandler.queryLoopBounds();

    auto mainClassifiers = classifierMap["main"];

    INFO("Classifier size " << mainClassifiers.size());
    REQUIRE(mainClassifiers.size() == 1);

    auto firstClassifier = mainClassifiers["while.cond"];

    INFO("Classifier increment " << "[" << firstClassifier.getLowerBound() << ", " << firstClassifier.getUpperBound() << "]");
    REQUIRE(firstClassifier == LoopBound::DeltaInterval::interval(
        9, 9, LoopBound::DeltaInterval::ValueType::Multiplicative));
}

TEST_CASE("Arrayreducer_negative.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_negative.ll",
        loopBoundConfig);

    auto classifierMap = Run->phasarHandler.queryLoopBounds();

    auto mainClassifiers = classifierMap["main"];

    INFO("Classifier size " << mainClassifiers.size());
    REQUIRE(mainClassifiers.size() == 1);

    auto firstClassifier = mainClassifiers["while.cond"];

    INFO("Classifier increment " << "[" << firstClassifier.getLowerBound() << ", " << firstClassifier.getUpperBound() << "]");
    REQUIRE(firstClassifier == LoopBound::DeltaInterval::interval(
        392, 392, LoopBound::DeltaInterval::ValueType::Additive));
}

TEST_CASE("Arrayreducer_nonlinearincrement.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_nonlinearincrement.ll",
        loopBoundConfig);

    auto classifierMap = Run->phasarHandler.queryLoopBounds();

    auto mainClassifiers = classifierMap["main"];

    INFO("Classifier size " << mainClassifiers.size());
    REQUIRE(mainClassifiers.size() == 1);

    auto firstClassifier = mainClassifiers["for.cond"];

    INFO("Classifier increment " << "[" << firstClassifier.getLowerBound() << ", " << firstClassifier.getUpperBound() << "]");
    REQUIRE(firstClassifier == LoopBound::DeltaInterval::interval(
        9, 9, LoopBound::DeltaInterval::ValueType::Multiplicative));
}

TEST_CASE("Arrayreducer_nonlinearincrementDIV.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_nonlinearincrementDIV.ll",
        loopBoundConfig);

    auto classifierMap = Run->phasarHandler.queryLoopBounds();

    auto mainClassifiers = classifierMap["main"];

    INFO("Classifier size " << mainClassifiers.size());
    REQUIRE(mainClassifiers.size() == 1);

    auto firstClassifier = mainClassifiers["for.cond"];

    INFO("Classifier increment " << "[" << firstClassifier.getLowerBound() << ", " << firstClassifier.getUpperBound() << "]");
    REQUIRE(firstClassifier == LoopBound::DeltaInterval::interval(
        5, 5, LoopBound::DeltaInterval::ValueType::Division));
}

TEST_CASE("Arrayreducer_whilenonlinearincrementWithIFMultipleFamily.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_whilenonlinearincrementWithIFMultipleFamily.ll",
        loopBoundConfig);

    auto classifierMap = Run->phasarHandler.queryLoopBounds();

    auto mainClassifiers = classifierMap["main"];

    /**
     * TODO: The mixed families currently cause the classifier to be discarded, as we currently do not support mixed
     * families. We should add support for this in the future, as it is a common pattern in real-world code and can
     * lead to more precise results in some cases.
     */

    INFO("Classifier size " << mainClassifiers.size());
    REQUIRE(mainClassifiers.empty());

}

TEST_CASE("Arrayreducer_whilenonlinearincrementWithIFOneFamily.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_whilenonlinearincrementWithIFOneFamily.ll",
        loopBoundConfig);

    auto classifierMap = Run->phasarHandler.queryLoopBounds();

    auto mainClassifiers = classifierMap["main"];

    INFO("Classifier size " << mainClassifiers.size());
    REQUIRE(mainClassifiers.size() == 1);

    auto firstClassifier = mainClassifiers["while.cond"];

    INFO("Classifier increment " << "[" << firstClassifier.getLowerBound() << ", " << firstClassifier.getUpperBound() << "]");
    REQUIRE(firstClassifier == LoopBound::DeltaInterval::interval(
        4, 9, LoopBound::DeltaInterval::ValueType::Multiplicative));
}
