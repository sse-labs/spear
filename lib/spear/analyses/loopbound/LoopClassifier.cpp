/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/loopbound/loopBoundWrapper.h"
#include "analyses/loopbound/util.h"


std::optional<int64_t> LoopClassifier::solveBound(llvm::CmpInst::Predicate pred,
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

std::optional<LoopBound::DeltaInterval> LoopClassifier::calculateBound() {
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

    int64_t lowerval = increment.value().getLowerBound();
    int64_t upperval = increment.value().getUpperBound();

    auto optLowerboundVal = solveBound(predicate, init.value(), check.value(), lowerval);
    auto optUpperboundVal = solveBound(predicate, init.value(), check.value(), upperval);

    if (!optLowerboundVal || !optUpperboundVal) {
        return std::nullopt;
    }

    return LoopBound::DeltaInterval::interval(optLowerboundVal.value(), optUpperboundVal.value());
}
