
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_CONFIGURATIONUTILS_H
#define SPEAR_CONFIGURATIONUTILS_H
#include <string>


#include "valuespace.h"

class ConfigurationUtils {
 public:
    /**
     * Convert a string to mode enum type
     *
     * @param str String to convert
     * @return Mode enum type
     */
    static Mode strToMode(const std::string& str);

    /**
     * Convert a string to format enum type
     *
     * @param str String to convert
     * @return Format enum type
     */
    static Format strToFormat(const std::string& str);

    /**
     * Convert a string to strategy enum type
     *
     * @param str String to convert
     * @return Strategy enum type
     */
    static Strategy strToStrategy(const std::string& str);

    /**
     * Convert a string to an analysis type enum type
     *
     * @param str String to convert
     * @return AnalysisType enum type
     */
    static AnalysisType strToAnalysisType(const std::string& str);
};

#endif //SPEAR_CONFIGURATIONUTILS_H
