/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYSETSATNESS_H_
#define SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYSETSATNESS_H_

#include <llvm/ADT/Hashing.h>
#include "FeasibilityAnalysisManager.h"

/**
 * SetSatnessKey is the key type for the SET satness cache.
 * It consists of a pointer to the FeasibilityAnalysisManager and a sorted vector of AST IDs
 * representing the conjunction of the path condition.
 */
struct SetSatnessKey {
    /**
     * Local instance of the manager, used to retrieve the formula set corresponding to the AST IDs in the key.
     */
    const Feasibility::FeasibilityAnalysisManager *Mgr = nullptr;

    /**
     * Vector of AST IDs representing the conjunction of the path condition.
     * The vector is sorted to ensure that the order of the AST IDs does not affect the equality and hash of the key.
     */
    llvm::SmallVector<unsigned, 8> AstIds;

    /**
     * Equality operator for SetSatnessKey, comparing both the manager pointer and the vector of AST IDs.
     * @param other The other SetSatnessKey to compare with.
     * @return true if both the manager pointer and the vector of AST IDs are equal, false otherwise.
     */
    bool operator==(const SetSatnessKey &other) const {
        return Mgr == other.Mgr && AstIds == other.AstIds;
    }
};

/**
* Hash function for SetSatnessKey, combining the hash of the manager pointer and the hash of the vector of AST IDs.
* This is used for storing SetSatnessKey in hash-based containers (e.g., std::unordered_map).
*/
struct SetSatnessHash {
    size_t operator()(const SetSatnessKey &K) const noexcept {
        // Calculate the hash by combining the hash of the manager pointer and the hash of the vector of AST IDs.
        // Using llvm::hash_combine to combine the hashes, and llvm::hash_combine_range to hash the vector of AST IDs.
        return llvm::hash_combine(K.Mgr, llvm::hash_combine_range(K.AstIds.begin(), K.AstIds.end()));
    }
};

/**
 * DenseMapInfo specialization for SetSatnessKey, providing the necessary methods for using SetSatnessKey as a
 * key in llvm::DenseMap. This includes methods for getting the empty key,
 * the tombstone key, hashing a SetSatnessKey, and comparing two SetSatnessKeys for equality.
 */
struct SetSatnessKeyInfo : llvm::DenseMapInfo<SetSatnessKey> {
    /**
     * Returns an empty key for SetSatnessKey, which is a special value that is used to represent an empty slot in the DenseMap.
     * The empty key is defined as a SetSatnessKey with the manager pointer set to a special invalid value
     * (e.g., ~0) and the vector of AST IDs containing a single invalid value (e.g., ~0u).
     * @return SetSatnessKey representing the empty key for DenseMap
     */
    static inline SetSatnessKey getEmptyKey() {
        SetSatnessKey K;
        K.Mgr = reinterpret_cast<const Feasibility::FeasibilityAnalysisManager*>(~uintptr_t(0));
        K.AstIds.push_back(~0u);
        return K;
    }

    /**
     * Returns a tombstone key for SetSatnessKey, which is a special value that is used to represent a deleted slot
     * in the DenseMap. The tombstone key is defined as a SetSatnessKey with the manager pointer set to a special
     * invalid value (e.g., ~1) and the vector of AST IDs containing a
     * @return SetSatnessKey representing the tombstone key for DenseMap
     */
    static inline SetSatnessKey getTombstoneKey() {
        SetSatnessKey K;
        K.Mgr = reinterpret_cast<const Feasibility::FeasibilityAnalysisManager*>(~uintptr_t(1));
        K.AstIds.push_back(~0u);
        return K;
    }

    /**
     * Computes the hash value for a given SetSatnessKey. This is used by the DenseMap to hash keys for efficient lookup.
     * The hash is computed by combining the hash of the manager pointer and the hash of the vector of AST IDs.
     * @param K The SetSatnessKey for which to compute the hash value.
     * @return The computed hash value for the given SetSatnessKey.
     */
    static unsigned getHashValue(const SetSatnessKey &K) {
        // K.AstIds is sorted, so order is stable.
        return llvm::hash_combine(K.Mgr, llvm::hash_combine_range(K.AstIds.begin(), K.AstIds.end()));
    }

    /**
     * Compares two SetSatnessKeys for equality. This is used by the DenseMap to determine if two keys are the same.
     * Two SetSatnessKeys are considered equal if they have the same manager pointer and the same vector of AST IDs.
     * @param L The first SetSatnessKey to compare.
     * @param R The second SetSatnessKey to compare.
     * @return true if both SetSatnessKeys are equal, false otherwise.
     */
    static bool isEqual(const SetSatnessKey &L, const SetSatnessKey &R) {
        return L == R;
    }
};

#endif  // SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYSETSATNESS_H_
