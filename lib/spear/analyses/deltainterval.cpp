/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "analyses/loopbound.h"

namespace loopbound {

DeltaInterval::DeltaInterval()
    : valueType(ValueType::TOP),
      lowerBound(std::numeric_limits<int64_t>::min()),
      upperBound(std::numeric_limits<int64_t>::max()) {}

DeltaInterval DeltaInterval::bottom() { return DeltaInterval(ValueType::BOTTOM, 0, 0); }

DeltaInterval DeltaInterval::top() {
    return DeltaInterval(ValueType::TOP, std::numeric_limits<int64_t>::min(),
                         std::numeric_limits<int64_t>::max());
}

DeltaInterval DeltaInterval::interval(int64_t low, int64_t high) {
    if (low > high) std::swap(low, high);
    return DeltaInterval(ValueType::NORMAL, low, high);
}

bool DeltaInterval::isBottom() const { return valueType == ValueType::BOTTOM; }
bool DeltaInterval::isTop() const { return valueType == ValueType::TOP; }
bool DeltaInterval::isNORMAL() const { return valueType == ValueType::NORMAL; }

int64_t DeltaInterval::getLowerBound() const { return lowerBound; }
int64_t DeltaInterval::getUpperBound() const { return upperBound; }

DeltaInterval DeltaInterval::join(const DeltaInterval &other) const {
    // Keep your "⊓" semantics from the logs: intersection / meet.
    if (isBottom() || other.isBottom()) return bottom();
    if (isTop()) return other;
    if (other.isTop()) return *this;

    const int64_t L = std::max(lowerBound, other.lowerBound);
    const int64_t U = std::min(upperBound, other.upperBound);
    if (L > U) return bottom();
    return interval(L, U);
}

bool DeltaInterval::operator==(const DeltaInterval &other) const {
    if (valueType != other.valueType) return false;
    if (valueType == ValueType::TOP || valueType == ValueType::BOTTOM) return true;
    return lowerBound == other.lowerBound && upperBound == other.upperBound;
}

bool DeltaInterval::operator!=(const DeltaInterval &other) const {
    return !(*this == other);
}

DeltaInterval DeltaInterval::add(int64_t constant) const {
    if (isBottom()) return *this;
    if (isTop()) return top();
    return interval(lowerBound + constant, upperBound + constant);
}

}