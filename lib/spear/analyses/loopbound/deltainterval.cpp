/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <algorithm>
#include <limits>
#include <utility>

#include "analyses/loopbound/LoopBound.h"

namespace LoopBound {

DeltaInterval::DeltaInterval()
    : valueType(ValueType::TOP), lowerBound(std::numeric_limits<int64_t>::min()),
upperBound(std::numeric_limits<int64_t>::max()) {}

DeltaInterval DeltaInterval::bottom() {
  return {ValueType::BOTTOM, 0, 0};
}

DeltaInterval DeltaInterval::top() {
  return {ValueType::TOP,
                       std::numeric_limits<int64_t>::min(),
                       std::numeric_limits<int64_t>::max()};
}

DeltaInterval DeltaInterval::empty() {
  // Neutral element for LUB over NORMAL increments.
  // Means: "no increments observed yet".
  return {ValueType::EMPTY, 0, 0};
}

DeltaInterval DeltaInterval::interval(int64_t low, int64_t high) {
  if (low > high) {
    std::swap(low, high);
  }
  return {ValueType::NORMAL, low, high};
}

DeltaInterval DeltaInterval::ideNeutral() {
  return empty();
}

DeltaInterval DeltaInterval::ideAbsorbing() {
  return top();
}

bool DeltaInterval::isBottom() const {
  return valueType == ValueType::BOTTOM;
}

bool DeltaInterval::isTop() const {
  return valueType == ValueType::TOP;
}

bool DeltaInterval::isNormal() const {
  return valueType == ValueType::NORMAL;
}

bool DeltaInterval::isEmpty() const noexcept {
  return valueType == ValueType::EMPTY;
}

bool DeltaInterval::isIdeNeutral() const {
  return isEmpty();
}

bool DeltaInterval::isIdeAbsorbing() const {
  return isTop();
}

int64_t DeltaInterval::getLowerBound() const {
  return lowerBound;
}

int64_t DeltaInterval::getUpperBound() const {
  return upperBound;
}

DeltaInterval DeltaInterval::join(const DeltaInterval &other) const {
  return leastUpperBound(other);
}

DeltaInterval DeltaInterval::leastUpperBound(const DeltaInterval &other) const {
  if (isBottom()) return other;
  if (other.isBottom()) return *this;

  if (isEmpty()) return other;
  if (other.isEmpty()) return *this;

  if (isTop() || other.isTop()) return top();

  const int64_t L = std::min(lowerBound, other.lowerBound);
  const int64_t U = std::max(upperBound, other.upperBound);
  return interval(L, U);
}

DeltaInterval DeltaInterval::greatestLowerBound(const DeltaInterval &other) const {
  if (isBottom() || other.isBottom()) return bottom();

  if (isEmpty() || other.isEmpty()) return empty();

  if (isTop()) return other;
  if (other.isTop()) return *this;

  const int64_t L = std::max(lowerBound, other.lowerBound);
  const int64_t U = std::min(upperBound, other.upperBound);
  if (L > U) {
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

}  // namespace LoopBound
