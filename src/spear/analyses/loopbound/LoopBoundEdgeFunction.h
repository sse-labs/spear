/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

/**
 * LoopBoundEdgeFunction
 *
*  The edge functions form a small lattice that describes how loop-step
 * information is transformed along control-flow paths.
 *
 *                           DeltaIntervalTop
 *                                 (⊤)
 *                                  |
 *          -------------------------------------------------
 *          |                                                |
 *   DeltaIntervalCollect                         DeltaIntervalIdentity
 *   (collect increments)                               (id)
 *          |                                                |
 *          -------------------------------------------------
 *                                  |
 *                           DeltaIntervalBottom
 *                                 (⊥)
 *
 *  DeltaIntervalIdentity
 *   Neutral element for composition.
 *   computeTarget(x) = x
 *
 *  DeltaIntervalCollect
 *   Collects possible increments observed along a path and merges them
 *   conservatively. This is the only edge function that actively changes
 *   the delta interval during analysis.
 *
 *  DeltaIntervalTop
 *   Represents an unknown transformation.
 *   Dominates joins and poisons compositions.
 *
 *  DeltaIntervalBottom
 *   Represents an impossible or killed path.
 *   Absorbing element for composition.
 *
 *
 */

#ifndef SRC_SPEAR_ANALYSES_LOOPBOUND_LOOPBOUNDEDGEFUNCTION_H_
#define SRC_SPEAR_ANALYSES_LOOPBOUND_LOOPBOUNDEDGEFUNCTION_H_

#include <phasar/DataFlow/IfdsIde/EdgeFunction.h>
#include "LoopBound.h"

namespace LoopBound {

using l_t = DeltaInterval;
using EF  = psr::EdgeFunction<l_t>;

/**
 * DeltaIntervalCollect
 *
 * Defines edge function operations used for calculating the increment interval
 */
struct DeltaIntervalCollect {
    // Define the lattice elements
    using l_t = LoopBound::l_t;

    /**
     * Lower increment value
     */
    int64_t lowerBound = 0;

    /**
     * Higher increment value
     */
    int64_t upperBound = 0;

    /**
     * Default constructor
     */
    DeltaIntervalCollect() = default;

    /**
     * Create a new DeltaInterval edge function
     * @param lowerIncrement lower increment
     * @param upperIncrement upper increment
     */
    DeltaIntervalCollect(int64_t lowerIncrement, int64_t upperIncrement);

    /**
     * Calculates the lattice value by computing source with the value of this instance
     *
     * @param source Incoming lattice value
     * @return Computed lattice value
     */
    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    /**
     * Calculates composition of the two given edge functions along one path
     *
     * @param self This DeltaIntervalCollect instance
     * @param second Another possible edge function
     * @return Returns the resulting edge function calculated from the two given edge functions
     */
    static EF compose(psr::EdgeFunctionRef<DeltaIntervalCollect> self,
                      const EF &second);

    /**
     * Calculates the merging of the two given edge functions
     *
     * @param self This DeltaIntervalCollect instance
     * @param other Another possible edge function
     * @return Returns the resulting edge function calculated from the two given edge functions
     */
    static EF join(psr::EdgeFunctionRef<DeltaIntervalCollect> self,
                   const EF &other);

    /**
     * Implementation of the equal operator for DeltaIntervalCollect edge functions
     * @return True if the instances match, false otherwise
     */
    bool operator==(const DeltaIntervalCollect &) const = default;

    /**
     * Describes if this edge function is constant
     * @return Returns false in any case.
     */
    bool isConstant() const noexcept;
};

/**
 * DeltaIntervalIdentity
 *
 * Identity edge function.
 *
 * This edge function does not modify the incoming lattice value and therefore
 * represents the neutral element for composition.
 */
struct DeltaIntervalIdentity {
    using l_t = LoopBound::l_t;

    /**
     * Returns the incoming lattice value unchanged.
     *
     * @param source Incoming lattice value
     * @return Same lattice value as source
     */
    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    /**
     * Composition with identity yields the second edge function.
     *
     * @param second Another possible edge function
     * @return second
     */
    static EF compose(psr::EdgeFunctionRef<DeltaIntervalIdentity>,
                      const EF &second);

    /**
     * Join of two edge functions with identity prefers the more precise one.
     * If both are identity the result stays identity.
     *
     * @param thisFunc Identity edge function
     * @param otherFunc Another possible edge function
     * @return Joined edge function
     */
    static EF join(psr::EdgeFunctionRef<DeltaIntervalIdentity> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    /**
     * Implementation of the equal operator for DeltaIntervalIdentity edge functions
     * @return True if the instances match, false otherwise
     */
    bool operator==(const DeltaIntervalIdentity &) const = default;

    /**
     * Describes if this edge function is constant
     * @return Returns false in any case.
     */
    bool isConstant() const noexcept;
};

/**
 * DeltaIntervalTop
 *
 * Top edge function for the edge-function lattice.
 *
 * Represents "unknown" transformation. When applied it yields the top element
 * of the value lattice (unless source is bottom/unreachable, depending on the
 * DeltaInterval semantics).
 */
struct DeltaIntervalTop {
    using l_t = LoopBound::l_t;

    /**
     * Returns the TOP lattice value (unknown), preserving unreachable (BOT) if needed.
     *
     * @param source Incoming lattice value
     * @return TOP value or BOT if the incoming value was already BOT
     */
    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    /**
     * Composition with TOP yields TOP. Preserves BOT
     *
     * @param second Another possible edge function
     * @return TOP edge function or BOT if incoming value was BOT
     */
    static EF compose(psr::EdgeFunctionRef<DeltaIntervalTop>,
                      const EF &second);

    /**
     * Join with TOP yields TOP;
     *
     * @param thisFunc TOP edge function
     * @param otherFunc Another possible edge function
     * @return Joined edge function
     */
    static EF join(psr::EdgeFunctionRef<DeltaIntervalTop> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    /**
     * Implementation of the equal operator for DeltaIntervalTop edge functions
     * @return True if the instances match, false otherwise
     */
    bool operator==(const DeltaIntervalTop &) const = default;

    /**
     * Describes if this edge function is constant
     * @return Returns false: it always maps to TOP.
     */
    bool isConstant() const noexcept;
};

/**
 * DeltaIntervalBottom
 *
 * Bottom edge function for the edge-function lattice.
 *
 * Represents an impossible / killed path transformation.
 */
struct DeltaIntervalBottom {
    using l_t = LoopBound::l_t;

    /**
     * Returns the BOTTOM lattice value (unreachable).
     *
     * @param source Incoming lattice value
     * @return BOTTOM value
     */
    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    /**
     * Composition with BOTTOM yields BOTTOM (absorbing element).
     *
     * @param second Another possible edge function
     * @return BOTTOM edge function
     */
    static EF compose(psr::EdgeFunctionRef<DeltaIntervalBottom>,
                      const EF &second);

    /**
     * Join with BOTTOM yields the other function (BOTTOM is neutral for join).
     *
     * @param thisFunc BOTTOM edge function
     * @param otherFunc Another possible edge function
     * @return Joined edge function
     */
    static EF join(psr::EdgeFunctionRef<DeltaIntervalBottom> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    /**
     * Implementation of the equal operator for DeltaIntervalBottom edge functions
     * @return True if the instances match, false otherwise
     */
    bool operator==(const DeltaIntervalBottom &) const = default;

    /**
     * Describes if this edge function is constant
     * @return Returns true: it always maps to BOTTOM.
     */
    bool isConstant() const noexcept;
};

/*
 * Calculates the edge identity
 * @return DeltaIntervalIdentity
 */
EF edgeIdentity();

/**
 * Calculates top
 * @return DeltaIntervalTop
 */
EF edgeTop();

}  // namespace LoopBound

#endif  // SRC_SPEAR_ANALYSES_LOOPBOUND_LOOPBOUNDEDGEFUNCTION_H_
