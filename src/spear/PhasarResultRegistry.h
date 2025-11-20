#ifndef PHASAR_RESULT_REGISTRY_H
#define PHASAR_RESULT_REGISTRY_H

#include <map>
#include <string>
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>

#include "PhasarHandler.h" // for BoundVarMap

class PhasarResultRegistry {
public:
    static PhasarResultRegistry& get() {
        static PhasarResultRegistry instance;
        return instance;
    }

    void store(const PhasarHandlerPass::BoundVarMap& results) {
        this->results = results;
    }

    const PhasarHandlerPass::BoundVarMap& getResults() const {
        return results;
    }

private:
    PhasarResultRegistry() = default;

    PhasarHandlerPass::BoundVarMap results;
};

#endif // PHASAR_RESULT_REGISTRY_H
