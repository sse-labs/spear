
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_CONFIGURATION_VALUESPACE_H_
#define SRC_SPEAR_CONFIGURATION_VALUESPACE_H_

/**
 * Enum to distinguish the analysis target in the application
 */
enum class Mode {
    UNDEFINED,
    PROGRAM,
    BLOCK,
    FUNCTION,
    INSTRUCTION,
    GRAPH
};

/**
 * Enum to distinguish the analysis target in the application
 */
enum class DeepCalls {
    UNDEFINED,
    ENABLED,
};

/**
 * Enum used to specify the output format
 */
enum class Format {
    UNDEFINED,
    PLAIN,
    JSON
};

/**
 * Enum describing the analysis strategy
 */
enum class Strategy {
    UNDEFINED,
    WORST,
    AVERAGE,
    BEST
};

enum class AnalysisType {
    UNDEFINED,
    MONOLITHIC,
    CLUSTERED,
    COMPARISON,  // Only needed for now. We should remove this later on
    LEGACY
};


#endif  // SRC_SPEAR_CONFIGURATION_VALUESPACE_H_
