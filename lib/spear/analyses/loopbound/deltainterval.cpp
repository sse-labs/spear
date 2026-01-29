// /*
//  * Copyright (c) 2026 Maximilian Krebs
//  * All rights reserved.
// *

#include "../../../../src/spear/analyses/loopbound/loopbound.h"

#include <algorithm>
#include <limits>

namespace loopbound {

DeltaInterval::DeltaInterval()
    : valueType(ValueType::TOP),
      lowerBound(std::numeric_limits<int64_t>::min()),
      upperBound(std::numeric_limits<int64_t>::max()) {}

DeltaInterval DeltaInterval::bottom() {
  return DeltaInterval(ValueType::BOTTOM, 0, 0);
}

DeltaInterval DeltaInterval::top() {
  return DeltaInterval(ValueType::TOP,
                       std::numeric_limits<int64_t>::min(),
                       std::numeric_limits<int64_t>::max());
}

DeltaInterval DeltaInterval::empty() {
  // Neutral element for LUB over NORMAL increments.
  // Means: "no increments observed yet".
  return DeltaInterval(ValueType::EMPTY, 0, 0);
}

DeltaInterval DeltaInterval::interval(int64_t low, int64_t high) {
  if (low > high) {
    std::swap(low, high);
  }
  return DeltaInterval(ValueType::NORMAL, low, high);
}

bool DeltaInterval::isBottom() const { return valueType == ValueType::BOTTOM; }
bool DeltaInterval::isTop() const { return valueType == ValueType::TOP; }
bool DeltaInterval::isNORMAL() const { return valueType == ValueType::NORMAL; }
bool DeltaInterval::isEmpty() const noexcept { return valueType == ValueType::EMPTY; }

int64_t DeltaInterval::getLowerBound() const { return lowerBound; }
int64_t DeltaInterval::getUpperBound() const { return upperBound; }

DeltaInterval DeltaInterval::join(const DeltaInterval &other) const {
  return leastUpperBound(other);
}

DeltaInterval DeltaInterval::leastUpperBound(const DeltaInterval &other) const {
  if (isBottom()) return other;
  if (other.isBottom()) return *this;

  // IMPORTANT: EMPTY = "no increments yet"
  if (isEmpty()) return other;
  if (other.isEmpty()) return *this;

  if (isTop() || other.isTop()) return top();

  const int64_t L = std::min(lowerBound, other.lowerBound);
  const int64_t U = std::max(upperBound, other.upperBound);
  return interval(L, U);
}

DeltaInterval DeltaInterval::greatestLowerBound(const DeltaInterval &other) const {
  // unreachable handling
  if (isBottom() || other.isBottom()) return bottom();

  // EMPTY: no increments known; GLB with EMPTY should stay EMPTY
  if (isEmpty() || other.isEmpty()) return empty();

  if (isTop()) return other;
  if (other.isTop()) return *this;

  const int64_t L = std::max(lowerBound, other.lowerBound);
  const int64_t U = std::min(upperBound, other.upperBound);
  if (L > U) {
    // For your domain you can represent "no overlap" as EMPTY or BOTTOM.
    // For increment-sets, EMPTY is the natural "no possible increment".
    return empty();
  }
  return interval(L, U);
}

bool DeltaInterval::operator==(const DeltaInterval &other) const {
  if (valueType != other.valueType) {
    return false;
  }
  if (valueType == ValueType::TOP || valueType == ValueType::BOTTOM ||
      valueType == ValueType::EMPTY) {
    return true;
  }
  return lowerBound == other.lowerBound && upperBound == other.upperBound;
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
  if (isEmpty()) {
    return empty();
  }
  return interval(lowerBound + constant, upperBound + constant);
}

} // namespace loopbound
