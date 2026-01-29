/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SPEAR_DELTAINTERVAL_H
#define SPEAR_DELTAINTERVAL_H
#include <cstdint>

namespace loopbound {

class DeltaInterval {
public:
    enum class ValueType { TOP, BOTTOM, NORMAL, EMPTY };

    DeltaInterval();

    static DeltaInterval empty();     // EMPTY
    bool isEmpty() const noexcept;

    static DeltaInterval bottom();
    static DeltaInterval top();
    static DeltaInterval interval(int64_t low, int64_t high);

    static DeltaInterval ideNeutral();
    static DeltaInterval ideAbsorbing();

    [[nodiscard]] bool isBottom() const;
    bool isTop() const;
    bool isNORMAL() const;
    bool isIdeNeutral() const;
    bool isIdeAbsorbing() const;

    int64_t getLowerBound() const;
    int64_t getUpperBound() const;

    DeltaInterval join(const DeltaInterval &other) const;

    DeltaInterval leastUpperBound(const DeltaInterval &other) const;
    DeltaInterval greatestLowerBound(const DeltaInterval &other) const;

    bool operator==(const DeltaInterval &other) const;
    bool operator!=(const DeltaInterval &other) const;

    DeltaInterval add(int64_t constant) const;

private:
    DeltaInterval(ValueType valuetype, int64_t low, int64_t high)
      : valueType(valuetype), lowerBound(low), upperBound(high) {}

    ValueType valueType;
    int64_t lowerBound;
    int64_t upperBound;
};

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                                 const DeltaInterval &DI) {
    if (DI.isBottom()) return OS << "⊥";
    if (DI.isTop()) return OS << "⊤";
    return OS << "[" << DI.getLowerBound() << ", " << DI.getUpperBound() << "]";
}

}

#endif //SPEAR_DELTAINTERVAL_H