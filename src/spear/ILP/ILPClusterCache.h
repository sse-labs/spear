
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_ILPCLUSTERCACHE_H
#define SPEAR_ILPCLUSTERCACHE_H
#include <optional>
#include <string>
#include <unordered_map>

#include "ILPTypes.h"

class ILPClusterCache {
 public:
    static ILPClusterCache* instance;

    /**
    * Get the singleton instance of the ILPClusterCache
    * @return Returns a reference to the singleton instance of the ILPClusterCache
    */
     static ILPClusterCache &getInstance();

     /**
      * Load cache from given file
      * If the file does not exist, we create it
      */
    ILPClusterCache(std::string cacheFile, bool enabled);

    /**
     * Deleted copy/move to enforce singleton semantics
     */
    ILPClusterCache(const ILPClusterCache&) = delete;
    ILPClusterCache& operator=(const ILPClusterCache&) = delete;
    ILPClusterCache(ILPClusterCache&&) = delete;
    ILPClusterCache& operator=(ILPClusterCache&&) = delete;

     /**
      * Check if entry under hash exists
      * @param hash Hash to search for
      * @return true if entry exists, false otherwise
      */
    bool entryExists(std::string hash);

    /**
     * Get the entry found under the given hash, if it exists
     * @param hash Hash to query
     * @return Returns the entry found under the given hash, if it exists. std::nullopt otherwise
     * The entry is a double value representing the WCEC of the loop cluster represented by the hash
     */
    std::optional<ILPResult> getEntry(const std::string& hash);

    /**
     * Store the given energy under the given hash
     * @param hash Hash to store the energy for
     * @param value Energy to store at the hash
     */
    void setEntry(const std::string& hash, ILPResult value);

    /**
     * Store the cache to the underlying file;
     */
    void writeBackCache();

 private:
    /**
     * Enables/Disables the cache
     */
    bool isEnabled = true;

     /**
      * Filepath the actual cache is stored
      */
    std::string cacheFile;

    /**
     * Internal cache data structure that maps loop cluster hashes to their calculated WCEC values.
     */
    std::unordered_map<std::string, ILPResult> cache;
};

#endif //SPEAR_ILPCLUSTERCACHE_H
