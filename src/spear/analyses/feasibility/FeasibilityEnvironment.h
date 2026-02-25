/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_FEASIBILITYENVIRONMENT_H
#define SPEAR_FEASIBILITYENVIRONMENT_H
#include <llvm/IR/Value.h>

namespace Feasibility {

struct EnvNode {
    const EnvNode *parent = nullptr;
    const llvm::Value *key = nullptr;
    const llvm::Value *val = nullptr;
};

struct EnvKey {
    uint32_t Base = 0;
    const llvm::Value *K = nullptr;
    const llvm::Value *V = nullptr;

    bool operator==(const EnvKey &o) const noexcept {
        return Base == o.Base && K == o.K && V == o.V;
    }
};

struct EnvKeyHash {
    std::size_t operator()(const EnvKey &k) const noexcept {
        std::size_t h = std::hash<uint32_t>{}(k.Base);
        auto mix = [&](std::size_t x) {
            h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };
        mix(std::hash<const void *>{}(static_cast<const void *>(k.K)));
        mix(std::hash<const void *>{}(static_cast<const void *>(k.V)));
        return h;
    }
};

}

#endif //SPEAR_FEASIBILITYENVIRONMENT_H