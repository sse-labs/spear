/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_ANALYSISSTRATEGY_H_
#define SRC_SPEAR_ANALYSISSTRATEGY_H_


/**
 * Class for the Strategy enum.
 */
class AnalysisStrategy {
 public:
    /**
     * Basic Enum to distinguish the different types of analysis
     */
    enum Strategy { WORSTCASE, AVERAGECASE, BESTCASE };
};

#endif  // SRC_SPEAR_ANALYSISSTRATEGY_H_
