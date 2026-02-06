/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <catch2/catch_test_macros.hpp>
#include "../testutils.h"


TEST_CASE("Arrayreducer_simple.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_simple.ll");

    auto classifiers = Run->phasarHandler.loopboundwrapper->getClassifiers();

    INFO("Classifier size " << classifiers.size());
    REQUIRE(classifiers.size() == 1);

    auto firstClassifier = classifiers.at(0);

    INFO("Classifier increment " << firstClassifier.increment);
    REQUIRE(firstClassifier.increment == LoopBound::DeltaInterval::interval(
        1, 1, LoopBound::DeltaInterval::ValueType::Additive));

    INFO("Classifier init " << firstClassifier.init.value());
    REQUIRE(firstClassifier.init.value() == 0);

    INFO("Classifier predicate " << firstClassifier.predicate);
    REQUIRE(firstClassifier.predicate == llvm::ICmpInst::Predicate::ICMP_SLT);

    INFO("Classifier check " << firstClassifier.check.value());
    REQUIRE(firstClassifier.check.value() == 9000);

    INFO("Classifier bound " << firstClassifier.bound);
    REQUIRE(firstClassifier.bound == LoopBound::DeltaInterval::interval(
        9000, 9000, LoopBound::DeltaInterval::ValueType::Additive));
}

TEST_CASE("Arrayreducer_complex.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_complex.ll");

    auto classifiers = Run->phasarHandler.loopboundwrapper->getClassifiers();

    INFO("Classifier size " << classifiers.size());
    REQUIRE(classifiers.size() == 1);

    auto firstClassifier = classifiers.at(0);

    INFO("Classifier increment " << firstClassifier.increment);
    REQUIRE(firstClassifier.increment == LoopBound::DeltaInterval::interval(
        4, 4, LoopBound::DeltaInterval::ValueType::Additive));

    INFO("Classifier init " << firstClassifier.init.value());
    REQUIRE(firstClassifier.init.value() == 0);

    INFO("Classifier predicate " << firstClassifier.predicate);
    REQUIRE(firstClassifier.predicate == llvm::ICmpInst::Predicate::ICMP_SLT);

    INFO("Classifier check " << firstClassifier.check.value());
    REQUIRE(firstClassifier.check.value() == 9000);

    INFO("Classifier bound " << firstClassifier.bound);
    REQUIRE(firstClassifier.bound == LoopBound::DeltaInterval::interval(
        2250, 2250, LoopBound::DeltaInterval::ValueType::Additive));
}

TEST_CASE("Arrayreducer_while.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_while.ll");

    auto classifiers = Run->phasarHandler.loopboundwrapper->getClassifiers();

    INFO("Classifier size " << classifiers.size());
    REQUIRE(classifiers.size() == 1);

    auto firstClassifier = classifiers.at(0);

    INFO("Classifier increment " << firstClassifier.increment);
    REQUIRE(firstClassifier.increment == LoopBound::DeltaInterval::interval(
        3, 3, LoopBound::DeltaInterval::ValueType::Additive));

    INFO("Classifier init " << firstClassifier.init.value());
    REQUIRE(firstClassifier.init.value() == 0);

    INFO("Classifier predicate " << firstClassifier.predicate);
    REQUIRE(firstClassifier.predicate == llvm::ICmpInst::Predicate::ICMP_SLT);

    INFO("Classifier check " << firstClassifier.check.value());
    REQUIRE(firstClassifier.check.value() == 9000);

    INFO("Classifier bound " << firstClassifier.bound);
    REQUIRE(firstClassifier.bound == LoopBound::DeltaInterval::interval(
        3000, 3000, LoopBound::DeltaInterval::ValueType::Additive));
}

TEST_CASE("Arrayreducer_whileif.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_whileif.ll");

    auto classifiers = Run->phasarHandler.loopboundwrapper->getClassifiers();

    INFO("Classifier size " << classifiers.size());
    REQUIRE(classifiers.size() == 1);

    auto firstClassifier = classifiers.at(0);

    INFO("Classifier increment " << firstClassifier.increment);
    REQUIRE(firstClassifier.increment == LoopBound::DeltaInterval::interval(
        3, 4, LoopBound::DeltaInterval::ValueType::Additive));

    INFO("Classifier init " << firstClassifier.init.value());
    REQUIRE(firstClassifier.init.value() == 0);

    INFO("Classifier predicate " << firstClassifier.predicate);
    REQUIRE(firstClassifier.predicate == llvm::ICmpInst::Predicate::ICMP_SLT);

    INFO("Classifier check " << firstClassifier.check.value());
    REQUIRE(firstClassifier.check.value() == 9000);

    INFO("Classifier bound " << firstClassifier.bound);
    REQUIRE(firstClassifier.bound == LoopBound::DeltaInterval::interval(
        2250, 3000, LoopBound::DeltaInterval::ValueType::Additive));
}

TEST_CASE("Arrayreducer_multiply.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_multiply.ll");

    auto classifiers = Run->phasarHandler.loopboundwrapper->getClassifiers();

    INFO("Classifier size " << classifiers.size());
    REQUIRE(classifiers.size() == 1);

    auto firstClassifier = classifiers.at(0);

    INFO("Classifier increment " << firstClassifier.increment);
    REQUIRE(firstClassifier.increment == LoopBound::DeltaInterval::interval(
        3, 3, LoopBound::DeltaInterval::ValueType::Multiplicative));

    INFO("Classifier init " << firstClassifier.init.value());
    REQUIRE(firstClassifier.init.value() == 1);

    INFO("Classifier predicate " << firstClassifier.predicate);
    REQUIRE(firstClassifier.predicate == llvm::ICmpInst::Predicate::ICMP_SLT);

    INFO("Classifier check " << firstClassifier.check.value());
    REQUIRE(firstClassifier.check.value() == 9000);

    INFO("Classifier bound " << firstClassifier.bound);
    REQUIRE(firstClassifier.bound == LoopBound::DeltaInterval::interval(
        9, 9, LoopBound::DeltaInterval::ValueType::Multiplicative));
}

TEST_CASE("Arrayreducer_negative.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_negative.ll");

    auto classifiers = Run->phasarHandler.loopboundwrapper->getClassifiers();

    INFO("Classifier size " << classifiers.size());
    REQUIRE(classifiers.size() == 1);

    auto firstClassifier = classifiers.at(0);

    INFO("Classifier increment " << firstClassifier.increment);
    REQUIRE(firstClassifier.increment == LoopBound::DeltaInterval::interval(
        -23, -23, LoopBound::DeltaInterval::ValueType::Additive));

    INFO("Classifier init " << firstClassifier.init.value());
    REQUIRE(firstClassifier.init.value() == 9000);

    INFO("Classifier predicate " << firstClassifier.predicate);
    REQUIRE(firstClassifier.predicate == llvm::ICmpInst::Predicate::ICMP_SGE);

    INFO("Classifier check " << firstClassifier.check.value());
    REQUIRE(firstClassifier.check.value() == 0);

    INFO("Classifier bound " << firstClassifier.bound);
    REQUIRE(firstClassifier.bound == LoopBound::DeltaInterval::interval(
        392, 392, LoopBound::DeltaInterval::ValueType::Additive));
}

TEST_CASE("Arrayreducer_nonlinearincrement.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_nonlinearincrement.ll");

    auto classifiers = Run->phasarHandler.loopboundwrapper->getClassifiers();

    INFO("Classifier size " << classifiers.size());
    REQUIRE(classifiers.size() == 1);

    auto firstClassifier = classifiers.at(0);

    INFO("Classifier increment " << firstClassifier.increment);
    REQUIRE(firstClassifier.increment == LoopBound::DeltaInterval::interval(
        3, 3, LoopBound::DeltaInterval::ValueType::Multiplicative));

    INFO("Classifier init " << firstClassifier.init.value());
    REQUIRE(firstClassifier.init.value() == 1);

    INFO("Classifier predicate " << firstClassifier.predicate);
    REQUIRE(firstClassifier.predicate == llvm::ICmpInst::Predicate::ICMP_SLT);

    INFO("Classifier check " << firstClassifier.check.value());
    REQUIRE(firstClassifier.check.value() == 9000);

    INFO("Classifier bound " << firstClassifier.bound);
    REQUIRE(firstClassifier.bound == LoopBound::DeltaInterval::interval(
        9, 9, LoopBound::DeltaInterval::ValueType::Multiplicative));
}

TEST_CASE("Arrayreducer_nonlinearincrementDIV.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_nonlinearincrementDIV.ll");

    auto classifiers = Run->phasarHandler.loopboundwrapper->getClassifiers();

    INFO("Classifier size " << classifiers.size());
    REQUIRE(classifiers.size() == 1);

    auto firstClassifier = classifiers.at(0);

    INFO("Classifier increment " << firstClassifier.increment);
    REQUIRE(firstClassifier.increment == LoopBound::DeltaInterval::interval(
        3, 3, LoopBound::DeltaInterval::ValueType::Division));

    INFO("Classifier init " << firstClassifier.init.value());
    REQUIRE(firstClassifier.init.value() == 9000);

    INFO("Classifier predicate " << firstClassifier.predicate);
    REQUIRE(firstClassifier.predicate == llvm::ICmpInst::Predicate::ICMP_SGT);

    INFO("Classifier check " << firstClassifier.check.value());
    REQUIRE(firstClassifier.check.value() == 100);

    INFO("Classifier bound " << firstClassifier.bound);
    REQUIRE(firstClassifier.bound == LoopBound::DeltaInterval::interval(
        5, 5, LoopBound::DeltaInterval::ValueType::Division));
}

TEST_CASE("Arrayreducer_whilenonlinearincrementWithIFMultipleFamily.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_whilenonlinearincrementWithIFMultipleFamily.ll");

    auto classifiers = Run->phasarHandler.loopboundwrapper->getClassifiers();

    INFO("Classifier size " << classifiers.size());
    REQUIRE(classifiers.size() == 1);

    auto firstClassifier = classifiers.at(0);

    INFO("Classifier increment " << firstClassifier.increment);
    REQUIRE(firstClassifier.increment == std::nullopt);

    INFO("Classifier init " << firstClassifier.init.value());
    REQUIRE(firstClassifier.init.value() == 1);

    INFO("Classifier predicate " << firstClassifier.predicate);
    REQUIRE(firstClassifier.predicate == llvm::ICmpInst::Predicate::ICMP_SLT);

    INFO("Classifier check " << firstClassifier.check.value());
    REQUIRE(firstClassifier.check.value() == 9000);

    INFO("Classifier bound " << firstClassifier.bound);
    REQUIRE(firstClassifier.bound == std::nullopt);
}

TEST_CASE("Arrayreducer_whilenonlinearincrementWithIFOneFamily.ll") {
    auto Run = runSpearOnFile(
        std::filesystem::path(TEST_INPUT_DIR),
        "programs/loopbound/compiled/arrayReducer_whilenonlinearincrementWithIFOneFamily.ll");

    auto classifiers = Run->phasarHandler.loopboundwrapper->getClassifiers();

    INFO("Classifier size " << classifiers.size());
    REQUIRE(classifiers.size() == 1);

    auto firstClassifier = classifiers.at(0);

    INFO("Classifier increment " << firstClassifier.increment);
    REQUIRE(firstClassifier.increment == LoopBound::DeltaInterval::interval(
        3, 12, LoopBound::DeltaInterval::ValueType::Multiplicative));

    INFO("Classifier init " << firstClassifier.init.value());
    REQUIRE(firstClassifier.init.value() == 1);

    INFO("Classifier predicate " << firstClassifier.predicate);
    REQUIRE(firstClassifier.predicate == llvm::ICmpInst::Predicate::ICMP_SLT);

    INFO("Classifier check " << firstClassifier.check.value());
    REQUIRE(firstClassifier.check.value() == 9000);

    INFO("Classifier bound " << firstClassifier.bound);
    REQUIRE(firstClassifier.bound == LoopBound::DeltaInterval::interval(
        4, 9, LoopBound::DeltaInterval::ValueType::Multiplicative));
}
