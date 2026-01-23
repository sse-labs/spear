/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "analyses/loopbound.h"

namespace loopbound {

DeltaInterval::DeltaInterval()
    : valueType(ValueType::BOTTOM), lowerBound(0), upperBound(0) {}

DeltaInterval DeltaInterval::bottom() {
    return {ValueType::BOTTOM, 0, 0};
}

DeltaInterval DeltaInterval::top() {
    return {ValueType::TOP, 0, 0};
}

DeltaInterval DeltaInterval::constant() {
    return {ValueType::NORMAL, 0, 0};
}

DeltaInterval DeltaInterval::interval(int64_t low, int64_t high) {
    return {ValueType::NORMAL, low, high};
}

bool DeltaInterval::isBottom() const {
    return valueType == ValueType::BOTTOM;
}

bool DeltaInterval::isTop() const {
    return valueType == ValueType::TOP;
}

bool DeltaInterval::isNORMAL() const {
    return valueType == ValueType::NORMAL;
}

int64_t DeltaInterval::getLowerBound() const {
    return lowerBound;
}

int64_t DeltaInterval::getUpperBound() const {
    return upperBound;
}

DeltaInterval DeltaInterval::join(const DeltaInterval &other) const {
    if (isTop() && other.isTop()) {
        return top();
    }

    if (isBottom() && other.isBottom()) {
        return bottom();
    }

    // If we land here, this has to be of valueType::Normal
    // ---

    // If we are of type normal and the other node is Bottom (= unreachable) we do not have any work
    // and just return this value
    if (other.isBottom()) {
        return *this;
    }

    // In any other case, calculate the hull over lower and upper bound
    return interval(
        std::min(lowerBound, other.lowerBound),
        std::max(upperBound, other.upperBound)
    );
}

bool DeltaInterval::operator==(const DeltaInterval &other) const {
    if (this->valueType != other.valueType) {
        return false;
    }

    if (this->valueType == ValueType::NORMAL) {
        return lowerBound == other.lowerBound &&
               upperBound == other.upperBound;
    }

    return true;
}

bool DeltaInterval::operator!=(const DeltaInterval &other) const {
    return !(*this == other);
}

DeltaInterval DeltaInterval::add(int64_t constant) const {
    if (isBottom()) {
        return *this;
    }

    if (isTop()) {
        return top();
    }

    return interval(lowerBound + constant, upperBound + constant);
}

}