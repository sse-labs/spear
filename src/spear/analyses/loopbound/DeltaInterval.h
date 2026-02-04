/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_ANALYSES_LOOPBOUND_DELTAINTERVAL_H_
#define SRC_SPEAR_ANALYSES_LOOPBOUND_DELTAINTERVAL_H_

namespace LoopBound {

/**
 * DeltaInterval class to represent the lattice values
 */
class DeltaInterval {
 public:
    // Type of the object
    enum class ValueType { TOP, BOTTOM, Additive, MULTIPLICATIVE, EMPTY };

    /**
     * Generic constructor
     */
    DeltaInterval();

    /**
     * Create a new empty instance
     * @return DeltaInterval of type EMPTY
     */
    static DeltaInterval empty();

    /**
     * Create a new bottom instance
     * @return DeltaInterval of type bottom
     */
    static DeltaInterval bottom();

    /**
     * Create a new top instance
     * @return DeltaInterval of type top
     */
    static DeltaInterval top();

    /**
     * Create a new normal instance
     * @param low Lower value of the interval
     * @param high Upper value of the interval
     * @return DeltaInterval of type normal
     */
    static DeltaInterval interval(int64_t low, int64_t high, ValueType valueType);

    /**
     * Create a neutral value = empty
     * @return DeltaInterval of type empty
     */
    static DeltaInterval ideNeutral();

    /**
     * Create an absorbing value = top
     * @return DeltaInterval of type top
     */
    static DeltaInterval ideAbsorbing();

    /**
     * Check if the current instance is of type bottom
     * @return true if bottom, false otherwise
     */
    bool isBottom() const;

    /**
     * Check if the current instance is of type top
     * @return true if top, false otherwise
     */
    bool isTop() const;

    /**
     * Check if the current instance is of type Additive
     * @return true if normal, false otherwise
     */
    bool isAdditive() const;

    /**
     * Checks if the current instance is of type Multiplicative
     * @return True if multiplicative, false otherwise
     */
    bool isMultiplicative() const;

    /**
     * Check if the current instance is neutral
     * @return true if neutral, false otherwise
     */
    bool isIdeNeutral() const;

    /**
     * Check if the current instance is absorbing
     * @return true if absorbing, false otherwise
     */
    bool isIdeAbsorbing() const;

    /**
     * Check if the current instance is of type empty
     * @return  true if empty, false otherwise
     */
    bool isEmpty() const noexcept;

    /**
     * Return the lower bound of the represented interval
     * @return Lower bound
     */
    int64_t getLowerBound() const;

    /**
     * Return the upper bound of the represented interval
     * @return Upper bound
     */
    int64_t getUpperBound() const;

    /**
     * Join two DeltaIntervals
     * @param other The other interval to join with
     * @return Joined statement
     */
    DeltaInterval join(const DeltaInterval &other) const;

    /**
     * Calculate least upper bound of both intervals [min(l1,l2), max(u1,u2)]
     * @param other Other interval to calculate with
     * @return Resulting interval
     */
    DeltaInterval leastUpperBound(const DeltaInterval &other) const;

    /**
     * Calculate the greatest upper bound of both intervals [max(l1,l2), min(u1,u2)]
     * @param other Other interval to calculate with
     * @return Resulting interval
     */
    DeltaInterval greatestLowerBound(const DeltaInterval &other) const;

    /**
     * Comparison operator ==
     * @param other Other interval to compare with
     * @return True if equal, false otherwise
     */
    bool operator==(const DeltaInterval &other) const;

    /**
     * Comparison operator !=
     * @param other Other interval to compare with
     * @return True if unequal, false otherwise
     */
    bool operator!=(const DeltaInterval &other) const;

 private:
    /**
     * Internal constructor to create a new DeltaInterval
     * @param valuetype Type the interval should be represented
     * @param low Lowerbound
     * @param high Upperbound
     */
    DeltaInterval(ValueType valuetype, int64_t low, int64_t high)
      : valueType(valuetype), lowerBound(low), upperBound(high) {}

    /**
     * Valuetype of this instance
     */
    ValueType valueType;

    /**
     * Lower bound of this instance
     */
    int64_t lowerBound;

    /**
     * Upper bound of this instance
     */
    int64_t upperBound;
};

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                                 const DeltaInterval &DI) {
    if (DI.isBottom()) return OS << "⊥";
    if (DI.isTop()) return OS << "⊤";
    return OS << "[" << DI.getLowerBound() << ", " << DI.getUpperBound() << "]";
}

}  // namespace LoopBound

#endif  // SRC_SPEAR_ANALYSES_LOOPBOUND_DELTAINTERVAL_H_
