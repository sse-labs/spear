/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <algorithm>

#include "ConfigParser.h"
#include "analyses/loopbound/loopBoundWrapper.h"
#include "analyses/loopbound/util.h"


std::optional<int64_t> LoopBound::LoopClassifier::solveAdditiveBound(llvm::CmpInst::Predicate pred,
    int64_t init, int64_t check, int64_t increment) {
    int64_t delta = check - init;
    if (increment < 0) {
        increment = -increment;
        delta = -delta;
        pred = LoopBound::Util::flipPredicate(pred);
    }

    if (increment == 0) {
        return 0;
    }

    switch (pred) {
        // check > init + x*increment
        case llvm::CmpInst::Predicate::ICMP_SLT:
        case llvm::CmpInst::Predicate::ICMP_ULT:
            return std::max<int64_t>(0, LoopBound::Util::ceilDiv(delta, increment));

        // check >= init + x*increment
        case llvm::CmpInst::Predicate::ICMP_SLE:
        case llvm::CmpInst::Predicate::ICMP_ULE:
            return std::max<int64_t>(0, LoopBound::Util::floorDiv(delta, increment) + 1);

        case llvm::CmpInst::Predicate::ICMP_SGT:
        case llvm::CmpInst::Predicate::ICMP_UGT:
        case llvm::CmpInst::Predicate::ICMP_SGE:
        case llvm::CmpInst::Predicate::ICMP_UGE:
            return std::nullopt;

        case llvm::CmpInst::Predicate::ICMP_EQ:
            return std::nullopt;

        case llvm::CmpInst::Predicate::ICMP_NE:
            return std::nullopt;

        default:
            return std::nullopt;
    }
}

std::optional<int64_t> LoopBound::LoopClassifier::solveMultiplicativeBound(
llvm::CmpInst::Predicate pred, int64_t init, int64_t check, int64_t increment) {
    // We do not deal with == or !=
    // Only calculate check o init * increment^k
    // Where o is one of { <, <= } and init > 0, check > 0, increment > 0
    if (pred == llvm::CmpInst::ICMP_EQ || pred == llvm::CmpInst::ICMP_NE) {
        return std::nullopt;
    }

    const bool isLessThan = pred == llvm::CmpInst::ICMP_SLT || pred == llvm::CmpInst::ICMP_ULT;
    const bool isLessOrEqualThan = pred == llvm::CmpInst::ICMP_SLE || pred == llvm::CmpInst::ICMP_ULE;

    if (!isLessThan && !isLessOrEqualThan) {
        return std::nullopt;
    }

    // Sanity check for paremeter value domain
    if (increment <= 0 || init <= 0 || check <=0) {
        return std::nullopt;
    }

    // Validate if the condition is already false at the beginning of the loop, so we would not iterate even once
    bool conditionHolds = LoopBound::Util::predicatesCoditionHolds(pred, init, check);

    if (!conditionHolds) {
        return 0;
    }

    long double iterationCandidate = (std::log((long double) check) - std::log((long double) init)) /
    std::log((long double) increment);

    long double iterations = iterationCandidate;
    if (isLessThan) {
        iterations = std::ceil(iterations);
    }

    if (isLessOrEqualThan) {
        iterations = std::floor(iterations) + 1;
    }

    // Sanity check
    if (iterations < 0.0) {
        iterations = 0.0;
    }

    if (iterations > (long double)std::numeric_limits<int64_t>::max()) {
        return std::nullopt;
    }

    return iterations;
}

std::optional<int64_t> LoopBound::LoopClassifier::solveDivisionBound(
llvm::CmpInst::Predicate pred, int64_t init, int64_t check, int64_t increment) {
    // We do not deal with == or !=
    // Only calculate check o init * increment^k
    // Where o is one of { <, <= } and init > 0, check > 0, increment > 0
    if (pred == llvm::CmpInst::ICMP_EQ || pred == llvm::CmpInst::ICMP_NE) {
        return std::nullopt;
    }

    const bool isGreaterThan = pred == llvm::CmpInst::ICMP_SGT || pred == llvm::CmpInst::ICMP_UGT;
    const bool isGreaterOrEqualThan = pred == llvm::CmpInst::ICMP_SGE || pred == llvm::CmpInst::ICMP_UGE;

    if (!isGreaterThan && !isGreaterOrEqualThan) {
        return std::nullopt;
    }

    // Sanity check for paremeter value domain
    if (increment <= 0 || init <= 0 || check <= 0) {
        return std::nullopt;
    }

    // Validate if the condition is already false at the beginning of the loop, so we would not iterate even once
    bool conditionHolds = LoopBound::Util::predicatesCoditionHolds(pred, init, check);

    if (!conditionHolds) {
        return 0;
    }

    // For divisions we need
    long double iterationCandidate = (std::log((long double) init) - std::log((long double) check)) /
    std::log((long double) increment);

    long double iterations = iterationCandidate;
    if (isGreaterThan) {
        iterations = std::ceil(iterations);
    }

    if (isGreaterOrEqualThan) {
        iterations = std::floor(iterations) + 1;
    }

    // Sanity check
    if (!std::isfinite(iterations)) {
        return std::nullopt;
    }

    if (iterations < 0.0) {
        iterations = 0.0;
    }

    if (iterations > (long double)std::numeric_limits<int64_t>::max()) {
        return std::nullopt;
    }

    return iterations;
}

std::optional<LoopBound::DeltaInterval> LoopBound::LoopClassifier::calculateBound() {
    auto fallback = ConfigParser::getAnalysisConfiguration().fallback;

    // Handle fallback values for bounding first
    if (type == LoopBound::MALFORMED_LOOP) {
        auto boundval = fallback["MALFORMED_LOOP"];
        return LoopBound::DeltaInterval::interval(boundval,
            boundval, LoopBound::DeltaInterval::ValueType::FALLBACK);
    }

    if (type == LoopBound::SYMBOLIC_BOUND_LOOP) {
        auto boundval = fallback["SYMBOLIC_BOUND_LOOP"];
        return LoopBound::DeltaInterval::interval(boundval,
            boundval, LoopBound::DeltaInterval::ValueType::FALLBACK);
    }

    if (type == LoopBound::NON_COUNTING_LOOP) {
        auto boundval = fallback["NON_COUNTING_LOOP"];
        return LoopBound::DeltaInterval::interval(boundval,
            boundval, LoopBound::DeltaInterval::ValueType::FALLBACK);
    }

    if (type == LoopBound::NESTED_LOOP) {
        auto boundval = fallback["NESTED_LOOP"];
        return LoopBound::DeltaInterval::interval(boundval,
            boundval, LoopBound::DeltaInterval::ValueType::FALLBACK);
    }

    // Handle normal loop type
    if (type == LoopBound::NORMAL_LOOP) {
        // Check if properties are valid and exist
        if (!init || !check) {
            return std::nullopt;
        }

        if (!increment) {
            return std::nullopt;
        }

        if (!predicate) {
            return std::nullopt;
        }

        // If we have an additive interval just use the generic calculation method
        if (increment.value().isAdditive()) {
            if (LoopBound::Util::LB_DebugEnabled) {
                llvm::errs() << "CALCULATING ADDITIVE BOUND!!!!!" << "\n";
            }
            int64_t lowerval = increment.value().getLowerBound();
            int64_t upperval = increment.value().getUpperBound();

            auto optLowerboundVal = solveAdditiveBound(predicate,
                init.value(), check.value(), lowerval);
            auto optUpperboundVal = solveAdditiveBound(predicate,
                init.value(), check.value(), upperval);

            if (!optLowerboundVal || !optUpperboundVal) {
                return std::nullopt;
            }

            return LoopBound::DeltaInterval::interval(
                optLowerboundVal.value(), optUpperboundVal.value(),
                LoopBound::DeltaInterval::ValueType::Additive);
        }


        if (increment.value().isMultiplicative()) {
            if (LoopBound::Util::LB_DebugEnabled) {
                llvm::errs() << "CALCULATING MULTIPLICATIVE BOUND!!!!!" << "\n";
            }

            int64_t lowerval = increment.value().getLowerBound();
            int64_t upperval = increment.value().getUpperBound();

            auto optLowerboundVal = solveMultiplicativeBound(predicate,
                init.value(), check.value(), lowerval);
            auto optUpperboundVal = solveMultiplicativeBound(predicate,
                init.value(), check.value(), upperval);

            if (!optLowerboundVal || !optUpperboundVal) {
                return std::nullopt;
            }

            return LoopBound::DeltaInterval::interval(
                optLowerboundVal.value(), optUpperboundVal.value(),
                LoopBound::DeltaInterval::ValueType::Multiplicative);
        }

        if (increment.value().isDivision()) {
            if (LoopBound::Util::LB_DebugEnabled) {
                llvm::errs() << "CALCULATING DIVISION BOUND!!!!!" << "\n";
            }

            int64_t lowerval = increment.value().getLowerBound();
            int64_t upperval = increment.value().getUpperBound();

            auto optLowerboundVal = solveDivisionBound(predicate,
                init.value(), check.value(), lowerval);
            auto optUpperboundVal = solveDivisionBound(predicate,
                init.value(), check.value(), upperval);

            if (!optLowerboundVal || !optUpperboundVal) {
                return std::nullopt;
            }

            return LoopBound::DeltaInterval::interval(
                optLowerboundVal.value(), optUpperboundVal.value(),
                LoopBound::DeltaInterval::ValueType::Division);
        }
    }


    // If the loop could not be classified we need to rely on our unknown fallback value...
    auto boundval = fallback["UNKNOWN_LOOP"];
    return LoopBound::DeltaInterval::interval(boundval, boundval,
        LoopBound::DeltaInterval::ValueType::FALLBACK);
}
