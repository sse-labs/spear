/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "analyses/LoopBoundEdgeFunction.h"

namespace loopbound {

/**
 *
 * Identity Edge function implementation
 *
 */

[[nodiscard]] DeltaIntervalIdentity::l_t
DeltaIntervalIdentity::computeTarget(const l_t &source) const {
    return source;
}

EF DeltaIntervalIdentity::compose(psr::EdgeFunctionRef<DeltaIntervalIdentity>,
                                 const EF &second) {
    // Calculate the identity of the input second parameter
    return second;
}

psr::EdgeFunction<DeltaIntervalIdentity::l_t>
DeltaIntervalIdentity::join(psr::EdgeFunctionRef<DeltaIntervalIdentity> thisFunc,
                            const EF &otherFunc) {
    if (otherFunc.template isa<DeltaIntervalIdentity>()) {
        return EF(std::in_place_type<DeltaIntervalIdentity>);
    }

    return EF(std::in_place_type<DeltaIntervalTop>);
}

bool DeltaIntervalIdentity::isConstant() const noexcept {
    return false;
}

/**
 *
 * All top Edge function implementation
 *
 */

[[nodiscard]] DeltaIntervalTop::l_t
DeltaIntervalTop::computeTarget(const l_t &source) const {
    return l_t::top();
}

EF DeltaIntervalTop::compose(psr::EdgeFunctionRef<DeltaIntervalTop>,
                                 const EF &second) {
    // Calculate the identity of the input second parameter
    return EF(std::in_place_type<DeltaIntervalTop>);
}

psr::EdgeFunction<DeltaIntervalTop::l_t>
DeltaIntervalTop::join(psr::EdgeFunctionRef<DeltaIntervalTop> thisFunc,
                            const EF &otherFunc) {
    return EF(std::in_place_type<DeltaIntervalTop>);
}

bool DeltaIntervalTop::isConstant() const noexcept {
    return true;
}

/**
 *
 * Interval Edge function implementation
 *
 */

[[nodiscard]] DeltaIntervalNormal::l_t
DeltaIntervalNormal::computeTarget(const l_t &source) const {
    // If input is bottom just hand down the bottom value
    if (source.isBottom()) {
        return source;
    }

    // if input is top return a new TOP
    if (source.isTop()) {
        return l_t::top();
    }

    // In any other case we receive an interval [l,h] as input
    // We use the interval [a,b] stored in the current node
    // and calculate [a + l, b + h]
    return l_t::interval(source.getLowerBound() + lowerBound, source.getUpperBound() + upperBound);
}

EF DeltaIntervalNormal::compose(psr::EdgeFunctionRef<DeltaIntervalNormal> self,
                                 const EF &second) {
    // Calculate the identity of the input second parameter
    if (second.template isa<DeltaIntervalIdentity>()) {
        return EF(self);
    }

    if (second.template isa<DeltaIntervalTop>()) {
        return EF(std::in_place_type<DeltaIntervalTop>);
    }

    if (auto *secondAsNormalInterval = second.template dyn_cast<DeltaIntervalNormal>()) {
        return EF(
            std::in_place_type<DeltaIntervalNormal>,
            self->lowerBound + secondAsNormalInterval->lowerBound,
            self->upperBound + secondAsNormalInterval->upperBound);
    }

    return EF(std::in_place_type<DeltaIntervalTop>);
}

EF DeltaIntervalNormal::join(psr::EdgeFunctionRef<DeltaIntervalNormal> thisFunc,
                            const EF &otherFunc) {

    if (otherFunc.template isa<DeltaIntervalTop>()) {
        return EF(std::in_place_type<DeltaIntervalTop>);
    }

    if (otherFunc.template isa<DeltaIntervalIdentity>()) {
        // Special case [0,0] ?????
        if (thisFunc->lowerBound == 0 && thisFunc->upperBound == 0) {
            return EF(std::in_place_type<DeltaIntervalIdentity>);
        }
        return EF(std::in_place_type<DeltaIntervalTop>);
    }

    if (auto *otherAsInterval = otherFunc.template dyn_cast<DeltaIntervalNormal>()) {
        return EF(
            std::in_place_type<DeltaIntervalNormal>,
            std::min(thisFunc->lowerBound, otherAsInterval->lowerBound),
            std::max(thisFunc->upperBound, otherAsInterval->upperBound));
    }

    return EF(std::in_place_type<DeltaIntervalTop>);
}

bool DeltaIntervalNormal::isConstant() const noexcept {
    return true;
}

}