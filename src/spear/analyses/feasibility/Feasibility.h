/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITY_H_
#define SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITY_H_

#include <phasar/DataFlow/Mono/IntraMonoProblem.h>
#include <phasar/PhasarLLVM/ControlFlow/LLVMBasedCFG.h>
#include <phasar/PhasarLLVM/Domain/LLVMAnalysisDomain.h>
#include <phasar/PhasarLLVM/TypeHierarchy/DIBasedTypeHierarchy.h>
#include <phasar/TypeHierarchy/TypeHierarchy.h>


#include <z3++.h>
#include <set>
#include <unordered_map>
#include <memory>

#include "FeasibilityFact.h"

namespace Feasibility {

/**
 * Analysis domain for the feasibility analysis.
 * Defines the types used in the analysis, such as the type of facts, nodes, etc.
 */
struct FeasibilityAnalysisDomain {
  using n_t = const llvm::Instruction *;
  using d_t = FeasibilityFact;
  using f_t = const llvm::Function *;
  using t_t = const llvm::DIType *;
  using th_t = psr::TypeHierarchy<t_t, f_t>;
  using v_t = const llvm::Value *;
  using i_t = const llvm::Instruction *;
  using c_t = psr::LLVMBasedCFG;
  using db_t = psr::LLVMProjectIRDB;

  using mono_container_t = std::set<d_t>;
};

/**
 * Feasibility analysis class.
 * Implements the actual analysis logic by defining the flow functions, merge function, etc.
 */
class FeasibilityAnalysis : public psr::IntraMonoProblem<FeasibilityAnalysisDomain> {
 public:
    using Domain = FeasibilityAnalysisDomain;
    using Base = psr::IntraMonoProblem<Domain>;

    using n_t = Domain::n_t;
    using d_t = Domain::d_t;
    using mono_container_t = Domain::mono_container_t;
    using db_t = Domain::db_t;
    using c_t = Domain::c_t;
    using v_t = Domain::v_t;
    using th_t = Domain::th_t;

    std::shared_ptr<z3::context> context;

    /**
     * Generic Constructor for the FeasibilityAnalysis.
     * Initializes the analysis with the given IRDB, type hierarchy, CFG and pointer information.
     * @param IRDB LLVM IR database of the file(s) under analysis
     * @param TH Type hierarchy to access type information during analysis
     * @param CFG Control flow graph to access the program's control flow during analysis
     * @param PT Pointer information to access aliasing information during analysis
     */
    FeasibilityAnalysis(const db_t *IRDB,
                        const th_t *TH,
                        const c_t *CFG,
                        psr::AliasInfoRef<v_t, n_t> PT);

    /**
     * Normal flow function for the feasibility analysis.
     * Defines how facts are transformed when flowing through an instruction.
     * @param Inst The instruction being analyzed
     * @param In The set of facts before the instruction
     * @return The set of facts after the instruction
     */
    mono_container_t normalFlow(n_t Inst, const mono_container_t &In) override;

    /**
     * Merge function for the feasibility analysis.
     * Defines how to merge facts from different control flow paths.
     * @param Lhs The set of facts from one control flow path
     * @param Rhs The set of facts from another control flow path
     * @return The merged set of facts
     */
    mono_container_t merge(const mono_container_t &Lhs,
                         const mono_container_t &Rhs) override;

    /**
     * Equality function for the feasibility analysis.
     * Defines how to compare two sets of facts for equality.
     * @param Lhs The first set of facts
     * @param Rhs The second set of facts
     * @return True if the sets of facts are equal, false otherwise
     */
    bool equal_to(const mono_container_t &Lhs,
                const mono_container_t &Rhs) override;

    /**
     * Initial seeds for the feasibility analysis.
     * Defines the initial facts at the entry points of the program.
     * @return A mapping from instructions to their initial facts
     */
    std::unordered_map<n_t, mono_container_t> initialSeeds() override;

    /**
     * Utility function to print a set of facts for debugging purposes.
     * @param OS The output stream to print to
     * @param container The set of facts to print
     */
    void printContainer(llvm::raw_ostream &OS, mono_container_t container) const override;

 private:
    std::optional<z3::expr> createIntVal(const llvm::Value *val);

    std::optional<z3::expr> createBitVal(const llvm::Value *V, const FeasibilityFact &Fact);

    std::optional<z3::expr> resolve(const llvm::Value *variable, const FeasibilityFact &Fact);

    z3::expr createFreshBitVal(const llvm::Value *key, unsigned bitwidth, const char *prefix);

    mono_container_t allTop();

    static bool equivUnderLess(const FeasibilityFact &a, const FeasibilityFact &b) {
        return !(a < b) && !(b < a);
    }
};

}  // namespace Feasibility

#endif  // SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITY_H_
