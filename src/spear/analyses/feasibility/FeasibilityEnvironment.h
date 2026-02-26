/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

/**
 * Internal data structures for the environment component of the feasibility analysis.
 * The environment tracks variable bindings (e.g., from PHI nodes) that are relevant for the path condition.
 *
 */

#ifndef SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYENVIRONMENT_H_
#define SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYENVIRONMENT_H_

#include <llvm/IR/Value.h>

namespace Feasibility {

/**
 * Main component of the environment. Represents a linked list of variable bindings,
 * where each node represents a single binding (key-value pair).
 * The linking between the nodes represents the nesting of environments as phi nodes also might be nested.
 */
class EnvNode {
 public:
    /**
     * Possible previous node, representing the parent environment. nullptr if this is the root node.
     */
    const EnvNode *parent = nullptr;

    /**
     * The key of the binding represented by this node. This is typically an LLVM value (e.g., a variable).
     */
    const llvm::Value *key = nullptr;

    /**
     * The value of the binding represented by this node. This is typically an LLVM value (e.g., a variable or a constant).
     */
    const llvm::Value *val = nullptr;
};

/**
 * Key for the environment map, representing a single binding (key-value pair) in the environment.
 * This struct is used as the key in the environment map to allow for efficient lookup of bindings based on the environment ID and the key-value pair.
 */
struct EnvKey {
    /**
     * Id of the environment this binding belongs to.
     * This is used to distinguish bindings from different environments, as they might have the same key and value but belong to different environments.
     */
    uint32_t Base = 0;

    /**
     * The key of the binding represented by this struct.
     */
    const llvm::Value *key = nullptr;

    /**
     * The underlying value of the binding represented by this struct.
     */
    const llvm::Value *value = nullptr;

    /**
     * Comparison operator for EnvKey, used for equality comparison in the environment map.
     * Equality is established over base id, key and value, as these three components uniquely
     * identify a binding in the environment.
     * @param other other element to check equality against
     * @return true if the two EnvKey instances represent the same binding in the same environment, false otherwise
     */
    bool operator==(const EnvKey &other) const noexcept {
        return Base == other.Base && key == other.key && value == other.value;
    }
};

/**
 * Hash function for EnvKey, used for hashing in the environment map.
 * The hash is computed based on the base id, key and value of the EnvKey,
 * as these three components uniquely identify a binding in the environment.
 */
struct EnvKeyHash {
    /**
     * Hash function for EnvKey, used for hashing in the environment map.
     * Uses Fibonacci hashing to combine the hash values of the base id, key and value of the EnvKey.
     *
     * @param k the EnvKey to hash
     * @return the hash value for the given EnvKey
    */
    std::size_t operator()(const EnvKey &k) const noexcept {
        std::size_t h = std::hash<uint32_t>{}(k.Base);
        auto mix = [&](std::size_t x) {
            h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };
        mix(std::hash<const void *>{}(static_cast<const void *>(k.key)));
        mix(std::hash<const void *>{}(static_cast<const void *>(k.value)));
        return h;
    }
};

}  // namespace Feasibility

#endif  // SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYENVIRONMENT_H_
