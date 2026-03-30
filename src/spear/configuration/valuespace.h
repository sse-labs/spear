
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_VALUESPACE_H
#define SPEAR_VALUESPACE_H

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
    COMPARISON,
    LEGACY
};


#endif //SPEAR_VALUESPACE_H
