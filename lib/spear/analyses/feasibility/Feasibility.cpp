/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <memory>
#include <unordered_map>

#include "analyses/feasibility/Feasibility.h"

namespace Feasibility {

FeasibilityAnalysis::FeasibilityAnalysis(const db_t *IRDB,
                                         const th_t *TH,
                                         const c_t *CFG,
                                         psr::AliasInfoRef<v_t, n_t> PT)
    : Base(IRDB, TH, CFG, PT, {"main"}) {
    llvm::errs() << "FeasibilityAnalysis::FeasibilityAnalysis()\n";
    context = std::make_shared<z3::context>();
}

FeasibilityAnalysis::mono_container_t
FeasibilityAnalysis::normalFlow(n_t inst, const mono_container_t &In) {
    printContainer(llvm::errs(), In);

    return In;
}

FeasibilityAnalysis::mono_container_t
FeasibilityAnalysis::merge(const mono_container_t &Lhs,
                           const mono_container_t &Rhs) {
    mono_container_t Out = Lhs;
    Out.insert(Rhs.begin(), Rhs.end());
    return Out;
}

bool FeasibilityAnalysis::equal_to(const mono_container_t &Lhs,
                                   const mono_container_t &Rhs) {
    return Lhs == Rhs;
}

std::unordered_map<FeasibilityAnalysis::n_t, FeasibilityAnalysis::mono_container_t>
FeasibilityAnalysis::initialSeeds() {
    std::unordered_map<n_t, mono_container_t> Seeds;

    for (const auto &EP : this->getEntryPoints()) {
        const llvm::Function *F = this->getProjectIRDB()->getFunctionDefinition(EP);
        if (!F || F->empty()) {
            continue;
        }

        const llvm::BasicBlock &EntryBB = F->getEntryBlock();
        if (EntryBB.empty()) {
            continue;
        }

        const llvm::Instruction *First = &*EntryBB.begin();
        Seeds.emplace(First, mono_container_t{d_t::TrueExpression(context)});
    }

    return Seeds;
}

void FeasibilityAnalysis::printContainer(llvm::raw_ostream &OS,
                                         mono_container_t C) const {
    OS << "{";
    bool First = true;
    for (const auto &F : C) {
        if (!First) {
            OS << ", ";
        }
        First = false;
        OS << F.pathExpression.to_string();
    }
    OS << "}";
    OS << "\n";
}

}  // namespace Feasibility
