/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/loopbound/loopBoundWrapper.h"
#include "analyses/loopbound/util.h"

std::optional<LoopBound::DeltaInterval> LoopClassifier::calculateBound() {
    // Check if properties are valid and exist
    if (!init || !check || !increment) {
        return std::nullopt;
    }

    if (!predicate) {
        return std::nullopt;
    }

    int64_t lowerbound = 0;
    int64_t upperbound = 0;

    if (LoopBound::Util::isEqualityPred(predicate)) {
        lowerbound = std::ceil((check.value() - init.value()) / increment.value().getLowerBound());
        upperbound = std::ceil((check.value() - init.value()) / increment.value().getUpperBound());
    } else {
        lowerbound = std::floor((check.value() - init.value()) / increment.value().getLowerBound());
        upperbound = std::floor((check.value() - init.value()) / increment.value().getUpperBound());
    }

    return LoopBound::DeltaInterval::interval(lowerbound, upperbound);
}
