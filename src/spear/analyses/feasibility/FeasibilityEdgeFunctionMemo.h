/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_FEASIBILITYEDGEFUNCTIONMEMO_H
#define SPEAR_FEASIBILITYEDGEFUNCTIONMEMO_H

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "analyses/feasibility/FeasibilityElement.h"

namespace Feasibility {
struct CTKey {
    uint8_t kind;
    uint32_t pc;
    uint32_t env;

    bool operator==(const CTKey &o) const noexcept {
        return kind == o.kind && pc == o.pc && env == o.env;
    }
};

struct CTKeyHash {
    std::size_t operator()(const CTKey &k) const noexcept {
        std::size_t h = std::hash<uint32_t>{}(k.pc);
        auto mix = [&](std::size_t x) {
            h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };
        mix(std::hash<uint32_t>{}(k.env));
        mix(std::hash<uint8_t>{}(k.kind));
        return h;
    }
};

template <typename L>
struct ComputeTargetMemo {
    mutable std::unordered_map<CTKey, L, CTKeyHash> Cache;
    mutable std::mutex Mu;

    bool lookup(const L &src, L &out) const {
        CTKey key{(uint8_t)src.getKind(), src.getFormulaId(), src.getEnvId()};
        std::lock_guard<std::mutex> Lk(Mu);
        auto it = Cache.find(key);
        if (it == Cache.end()) return false;
        out = it->second;
        return true;
    }

    void store(const L &src, const L &out) const {
        CTKey key{(uint8_t)src.getKind(), src.getFormulaId(), src.getEnvId()};
        std::lock_guard<std::mutex> Lk(Mu);
        Cache.emplace(key, out);
    }
 };
}

#endif //SPEAR_FEASIBILITYEDGEFUNCTIONMEMO_H