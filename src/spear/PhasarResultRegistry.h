/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_PHASARRESULTREGISTRY_H_
#define SRC_SPEAR_PHASARRESULTREGISTRY_H_

#include <map>

#include "PhasarHandler.h"  // for BoundVarMap

class PhasarResultRegistry {
 public:
    static PhasarResultRegistry& get() {
        static PhasarResultRegistry instance;
        return instance;
    }

    void store(const PhasarHandlerPass::BoundVarMap& results) {
        this->results = results;
    }

    [[nodiscard]] const PhasarHandlerPass::BoundVarMap& getResults() const {
        return results;
    }

 private:
    PhasarResultRegistry() = default;

    PhasarHandlerPass::BoundVarMap results;
};

#endif  // SRC_SPEAR_PHASARRESULTREGISTRY_H_
