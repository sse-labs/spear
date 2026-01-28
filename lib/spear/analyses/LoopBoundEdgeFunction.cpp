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
    // second ∘ id = second
    return second;
}

psr::EdgeFunction<DeltaIntervalIdentity::l_t>
DeltaIntervalIdentity::join(psr::EdgeFunctionRef<DeltaIntervalIdentity>,
                            const EF &otherFunc) {

    if (otherFunc == EF{}) {
        llvm::errs() << "[ID] " << "Called with otherfunc as NULL!" << "\n";
    }

    if (otherFunc.template isa<DeltaIntervalIdentity>()) {
        return EF(std::in_place_type<DeltaIntervalIdentity>);
    }

    if (otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(std::in_place_type<DeltaIntervalIdentity>);
    }

    if (otherFunc.template isa<DeltaIntervalBottom>()) {
        return EF(std::in_place_type<DeltaIntervalIdentity>);
    }

    if (otherFunc.template isa<DeltaIntervalTop>()) {
        return EF(std::in_place_type<DeltaIntervalTop>);
    }

    // Identity ⊔ Normal([a,b])  ==> Normal([min(0,a), max(0,b)])
    if (auto *N = otherFunc.template dyn_cast<DeltaIntervalNormal>()) {
        auto outLo = std::min<int64_t>(0, N->lowerBound);
        auto outHi = std::max<int64_t>(0, N->upperBound);

        // If the hull collapses back to [0,0], keep Identity.
        if (outLo == 0 && outHi == 0) {
            return EF(std::in_place_type<DeltaIntervalIdentity>);
        }
        return EF(std::in_place_type<DeltaIntervalNormal>, outLo, outHi);
    }

    return EF(std::in_place_type<DeltaIntervalTop>);
}


bool DeltaIntervalIdentity::isConstant() const noexcept {
    return false;
}

/**
 *
 * Bottom Edge function implementation
 * (replacement for psr::AllBottom<l_t>)
 *
 */

[[nodiscard]] DeltaIntervalBottom::l_t
DeltaIntervalBottom::computeTarget(const l_t &source) const {
    // Preserve unreachable: bottom stays bottom
    if (source.isBottom()) {
        return source;
    }
    // Everything else maps to bottom
    return l_t::bottom();
}

EF DeltaIntervalBottom::compose(psr::EdgeFunctionRef<DeltaIntervalBottom>,
                               const EF &second) {
    // We are computing: second ∘ bottom
    // bottom maps to ⊥ (except it preserves ⊥). Apply second to ⊥.
    // All of our EFs preserve ⊥, so result is still bottom for any second in this family.
    // (If you ever introduce a non-bottom-preserving EF, revisit this.)
    (void)second;
    return EF(std::in_place_type<DeltaIntervalBottom>);
}

psr::EdgeFunction<DeltaIntervalBottom::l_t>
DeltaIntervalBottom::join(psr::EdgeFunctionRef<DeltaIntervalBottom>,
                          const EF &otherFunc) {
    if (otherFunc == EF{}) {
        llvm::errs() << "[BOT] " << "Called with otherfunc as NULL!" << "\n";
    }
    // bottom ⊔ f = f
    return otherFunc;
}

bool DeltaIntervalBottom::isConstant() const noexcept {
    return true;
}

/**
 *
 * Top Edge function implementation
 *
 */

[[nodiscard]] DeltaIntervalTop::l_t
DeltaIntervalTop::computeTarget(const l_t &source) const {
    // IMPORTANT: preserve unreachable
    if (source.isBottom()) {
        return source;
    }
    return l_t::top();
}

EF DeltaIntervalTop::compose(psr::EdgeFunctionRef<DeltaIntervalTop>,
                            const EF &second) {
    // second ∘ top = top if second is top; otherwise still top in our restricted family
    (void)second;
    return EF(std::in_place_type<DeltaIntervalTop>);
}

psr::EdgeFunction<DeltaIntervalTop::l_t>
DeltaIntervalTop::join(psr::EdgeFunctionRef<DeltaIntervalTop>,
                       const EF &otherFunc) {
    if (otherFunc == EF{}) {
        llvm::errs() << "[TOP] " << "Called with otherfunc as NULL!" << "\n";
    }
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
    // Preserve unreachable
    if (source.isBottom()) {
        return source;
    }

    if (source.isTop()) {
        return l_t::top();
    }

    return l_t::interval(source.getLowerBound() + lowerBound,
                         source.getUpperBound() + upperBound);
}

EF DeltaIntervalNormal::compose(psr::EdgeFunctionRef<DeltaIntervalNormal> self,
                               const EF &second) {
    // We compute: second ∘ self

    if (second.template isa<DeltaIntervalIdentity>()) {
        return EF(self);
    }

    if (second.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(self);
    }

    if (second.template isa<DeltaIntervalBottom>()) {
        // bottom ∘ normal = bottom
        return EF(std::in_place_type<DeltaIntervalBottom>);
    }

    if (second.template isa<DeltaIntervalTop>()) {
        // top ∘ normal = top
        return EF(std::in_place_type<DeltaIntervalTop>);
    }

    if (auto *secondAsNormal = second.template dyn_cast<DeltaIntervalNormal>()) {
        // (x+[a,b]) then +[c,d] => x+[a+c, b+d]
        return EF(std::in_place_type<DeltaIntervalNormal>,
                  self->lowerBound + secondAsNormal->lowerBound,
                  self->upperBound + secondAsNormal->upperBound);
    }

    return EF(std::in_place_type<DeltaIntervalTop>);
}

EF DeltaIntervalNormal::join(psr::EdgeFunctionRef<DeltaIntervalNormal> thisFunc,
                            const EF &otherFunc) {

    if (otherFunc == EF{}) {
        llvm::errs() << "[NORMAL] " << "Called with otherfunc as NULL!" << "\n";
    }

    if (otherFunc.template isa<DeltaIntervalTop>()) {
        return EF(std::in_place_type<DeltaIntervalTop>);
    }

    if (otherFunc.template isa<DeltaIntervalBottom>()) {
        // normal ⊔ bottom = normal
        return EF(thisFunc);
    }

    if (otherFunc.template isa<DeltaIntervalIdentity>()) {
        // Normal([a,b]) ⊔ Identity  ==> Normal([min(a,0), max(b,0)])
        auto outLo = std::min<int64_t>(thisFunc->lowerBound, 0);
        auto outHi = std::max<int64_t>(thisFunc->upperBound, 0);

        if (outLo == 0 && outHi == 0) {
            return EF(std::in_place_type<DeltaIntervalIdentity>);
        }

        llvm::errs() << "[Normal join ID] -> "
            << "["
            << thisFunc->lowerBound << ", "
            << thisFunc->upperBound << "] o ["
            << 0 << ", "
            << 0 << "] = ["
            << outLo << ", "
            << outHi << "]\n";

        return EF(std::in_place_type<DeltaIntervalNormal>, outLo, outHi);
    }

    if (otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
        // Treat PhASAR identity as our identity
        auto outLo = std::min<int64_t>(thisFunc->lowerBound, 0);
        auto outHi = std::max<int64_t>(thisFunc->upperBound, 0);

        if (outLo == 0 && outHi == 0) {
            return EF(std::in_place_type<DeltaIntervalIdentity>);
        }

        llvm::errs() << "[Normal join ID(PSR)] -> "
            << "["
            << thisFunc->lowerBound << ", "
            << thisFunc->upperBound << "] o ["
            << 0 << ", "
            << 0 << "] = ["
            << outLo << ", "
            << outHi << "]\n";

        return EF(std::in_place_type<DeltaIntervalNormal>, outLo, outHi);
    }

    if (auto *otherAsNormal = otherFunc.template dyn_cast<DeltaIntervalNormal>()) {
        auto outLo = std::min(thisFunc->lowerBound, otherAsNormal->lowerBound);
        auto outHi = std::max(thisFunc->upperBound, otherAsNormal->upperBound);

        llvm::errs() << "[Normal join Normal] -> "
            << "["
            << thisFunc->lowerBound << ", "
            << thisFunc->upperBound << "] o ["
            << otherAsNormal->lowerBound << ", "
            << otherAsNormal->upperBound << "] = ["
            << outLo << ", "
            << outHi << "]\n";

        return EF(std::in_place_type<DeltaIntervalNormal>, outLo, outHi);
    }

    if (llvm::isa<psr::AllTop<l_t>>(otherFunc)) {
        llvm::errs() << "Our unknown function is a phasar alltop" << "\n";
    }

    if (llvm::isa<psr::EdgeIdentity<l_t>>(otherFunc)) {
        llvm::errs() << "Our unknown function is a phasar edgeidentity" << "\n";
    }

    if (llvm::isa<psr::AllBottom<l_t>>(otherFunc)) {
        llvm::errs() << "Our unknown function is a phasar allbot" << "\n";
    }

    return EF(std::in_place_type<DeltaIntervalTop>);
}

bool DeltaIntervalNormal::isConstant() const noexcept {
    return true;
}

} // namespace loopbound
