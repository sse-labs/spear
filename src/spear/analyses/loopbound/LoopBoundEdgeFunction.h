/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SPEAR_LOOPBOUNDEDGEFUNCTION_H
#define SPEAR_LOOPBOUNDEDGEFUNCTION_H

#include <cstdint>
#include <algorithm>

#include <phasar/DataFlow/IfdsIde/EdgeFunction.h>
#include "loopbound.h"

namespace loopbound {

using l_t = DeltaInterval;
using EF  = psr::EdgeFunction<l_t>;

struct DeltaIntervalCollect {
    using l_t = loopbound::l_t;

    int64_t lowerBound = 0;
    int64_t upperBound = 0;

    DeltaIntervalCollect() = default;
    DeltaIntervalCollect(int64_t L, int64_t U);

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    // second ∘ this
    static EF compose(psr::EdgeFunctionRef<DeltaIntervalCollect> self,
                      const EF &second);

    static EF join(psr::EdgeFunctionRef<DeltaIntervalCollect> self,
                   const EF &other);

    bool operator==(const DeltaIntervalCollect &) const = default;
    bool isConstant() const noexcept;
};

struct DeltaIntervalAssign {
    using l_t = loopbound::DeltaInterval;

    int64_t lowerBound = 0;
    int64_t upperBound = 0;

    DeltaIntervalAssign() = default;
    DeltaIntervalAssign(int64_t L, int64_t U);

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    // second ∘ this
    static loopbound::EF compose(psr::EdgeFunctionRef<DeltaIntervalAssign> self,
                                 const loopbound::EF &second);

    static loopbound::EF join(psr::EdgeFunctionRef<DeltaIntervalAssign> thisFunc,
                          const loopbound::EF &other);

    bool operator==(const DeltaIntervalAssign &) const = default;
    bool isConstant() const noexcept;
};

struct DeltaIntervalIdentity {
    using l_t = loopbound::l_t;

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    static EF compose(psr::EdgeFunctionRef<DeltaIntervalIdentity>,
                      const EF &second);

    static EF join(psr::EdgeFunctionRef<DeltaIntervalIdentity> thisFunc,
                                       const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const DeltaIntervalIdentity &) const = default;

    bool isConstant() const noexcept;
};

struct DeltaIntervalTop {
    using l_t = loopbound::l_t;

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    static EF compose(psr::EdgeFunctionRef<DeltaIntervalTop>,
                      const EF &second);

    static EF join(psr::EdgeFunctionRef<DeltaIntervalTop> thisFunc,
                                       const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const DeltaIntervalTop &) const = default;

    bool isConstant() const noexcept;
};

struct DeltaIntervalBottom {
    using l_t = loopbound::l_t;

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    static EF compose(psr::EdgeFunctionRef<DeltaIntervalBottom>,
                      const EF &second);

    static EF join(psr::EdgeFunctionRef<DeltaIntervalBottom> thisFunc,
                                       const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const DeltaIntervalBottom &) const = default;

    bool isConstant() const noexcept;
};

struct DeltaIntervalNormal {
    using l_t = loopbound::l_t;

    int64_t lowerBound;
    int64_t upperBound;

    DeltaIntervalNormal() = default;
    DeltaIntervalNormal(int64_t lowerBound, int64_t upperBound) {

        if (lowerBound > upperBound) {
            std::swap(lowerBound, upperBound);
        }

        this->lowerBound = lowerBound;
        this->upperBound = upperBound;
    }

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    static EF compose(psr::EdgeFunctionRef<DeltaIntervalNormal>,
                      const EF &second);

    static EF join(psr::EdgeFunctionRef<DeltaIntervalNormal> thisFunc,
                                       const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const DeltaIntervalNormal &) const = default;

    bool isConstant() const noexcept;
};

// ---- helpers ----
inline EF edgeIdentity() { return EF(std::in_place_type<DeltaIntervalIdentity>); }
inline EF edgeTop() { return EF(std::in_place_type<DeltaIntervalTop>); }
inline EF addConst(int64_t C) { return EF(std::in_place_type<DeltaIntervalNormal>, C, C); }
inline EF addInterval(int64_t L, int64_t U) { return EF(std::in_place_type<DeltaIntervalNormal>, L, U); }


} // namespace loopbound

#endif //SPEAR_LOOPBOUNDEDGEFUNCTION_H