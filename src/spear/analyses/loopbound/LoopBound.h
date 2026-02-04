/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

/**
 * Main LoopBound Anaylsis component. Implements a Phasar based IDE analysis.
 * Uses Cullman and Martins approach for loop bound solving. See
 *
 * Christoph Cullmann and Florian Martin. Data-Flow Based Detection of Loop Bounds.
 * In 7th International Workshop on Worst-Case Execution Time Analysis (WCET'07).
 * Open Access Series in Informatics (OASIcs), Volume 6, pp. 1-6,
 * Schloss Dagstuhl – Leibniz-Zentrum für Informatik (2007)
 * https://doi.org/10.4230/OASIcs.WCET.2007.1193
 *
 * Calculates loop increment per iteration (alongside other loop properties) which we then can use
 * to calculate the amount of iterations the loop executes.
 *
 */

#ifndef SRC_SPEAR_ANALYSES_LOOPBOUND_LOOPBOUND_H_
#define SRC_SPEAR_ANALYSES_LOOPBOUND_LOOPBOUND_H_

#include <phasar/DataFlow/IfdsIde/IDETabulationProblem.h>
#include <phasar/DataFlow/IfdsIde/EdgeFunctions.h>
#include <phasar/PhasarLLVM/Domain/LLVMAnalysisDomain.h>
#include <phasar/PhasarLLVM/DB/LLVMProjectIRDB.h>

#include <llvm/IR/Instructions.h>
#include <llvm/Analysis/LoopInfo.h>

#include <set>
#include <vector>

#include "DeltaInterval.h"

namespace LoopBound {

/**
 * Internal representation of the counter corresponding to one loop.
 */
struct LoopCounterICMP {
    llvm::Value *CounterSide = nullptr;      // Operator with counter variable
    llvm::Value *InvariantSide = nullptr;    // Operator the counter is checked against
    std::vector<const llvm::Value *> Roots;  // Counter the ICMP is build uppon
};

/**
 * Internal description of the loop we are analyzing.
 * Stores information about the loop, the counter and the related scalar values
 * attached to them
 */
struct LoopParameterDescription {
    llvm::Loop *loop = nullptr;  // The loop the description is based upon
    llvm::ICmpInst *icmp = nullptr;  // The ICMP instruction of the loop
    const llvm::Value *counterRoot = nullptr;  // The instruction defining the counter of the loop
    std::optional<int64_t> init = std::nullopt;  // Initial value of the loop
};

/**
 * Domain definition of the LoopBoundAnalysis
 */
struct LoopBoundDomain : psr::LLVMAnalysisDomainDefault {
    using d_t = const llvm::Value *;  // Flow-Fact -> In our case the loop counter root
    using l_t = DeltaInterval;  // Latice Element -> Our DeltaInterval to track increments
    using i_t = psr::LLVMBasedICFG;  // Control flow type -> Here we operate on the ICFG
};

/**
 * Increment Representation to distinguish additive from multiplicative increments
 */
struct LoopBoundIncrementInstance {
    std::optional<int64_t> increment;
    DeltaInterval::ValueType type;
};

/**
 * LoopBoundIDEAnalysis class
 * Implements the LoopBound analysis
 */
class LoopBoundIDEAnalysis final  : public psr::IDETabulationProblem<LoopBoundDomain, std::set<const llvm::Value *>> {
 public:
    /**
     * Derive types of the analysis from the base class definition for usage inside the class.
     */
    using base_t = psr::IDETabulationProblem<LoopBoundDomain, std::set<const llvm::Value *>>;
    using n_t = typename base_t::n_t;
    using d_t = typename base_t::d_t;
    using l_t = typename base_t::l_t;
    using f_t = typename base_t::f_t;

    /**
     * Define function pointer types
     */
    using container_t = std::set<d_t>;
    using FlowFunctionPtrType = typename base_t::FlowFunctionPtrType;
    using EdgeFunctionType = psr::EdgeFunction<l_t>;

    /**
     * Explicit constructor that creates a new instance of the LoopBound analysis.
     * @param IRDB LLVM IR database of the file(s) under analysis
     * @param loops Vector containing all loops found in the current program
     */
    explicit LoopBoundIDEAnalysis(const psr::LLVMProjectIRDB *IRDB, std::vector<llvm::Loop*> *loops);

    /**
     * Analyzes the given store instruction to find the increment that is occuring to the given root variable.
     * Peals backwards from the store to find the constant increment.
     * Heart of the LoopBound analysis
     *
     * Example:
     *
     * Assume the following LLVM IR representation. Our counter root is line %28 because it refers to the counter %i.
     * We want to analyze the increment of the loop and run backwards from the given store. We find the line %add15
     * and deduce the increment value from there. This approach does not take care about other stores.
     * Multiple stores to the counter have to be detected above this method and a call to detect the increment has to be
     * performed for each of them.
     *
     * %28 = load i32, ptr %i, align 4, !dbg !1145
     * %add15 = add nsw i32 %28, 4, !dbg !1146
     * store i32 %add15, ptr %i, align 4, !dbg !1147
     *
     *
     * @param storeInst Store instruction that should be analyzed for constant increments
     * @param counterRoot Loop counter the store instruction should be analyzed for
     * @return
     */
    static std::optional<LoopBoundIncrementInstance> extractConstIncFromStore(
    const llvm::StoreInst *storeInst, const llvm::Value *counterRoot);

    /**
     * Getter to return the internal list of LoopParamterDescriptions
     * @return Returns a vector of the descriptions.
     */
    std::vector<LoopParameterDescription> getLoopParameterDescriptions();

    /**
     * Check if the given Fact stores any information other than the ZeroValue.
     * This method is rather useful if you want to check, if the given fact is the initial seeded value
     * @param Fact Fact to be analyzed
     * @return True if the fact is the ZeroValue, false otherwise
     */
    bool isZeroValue(d_t Fact) const noexcept override;

 private:
    /**
     * Internal representation of the loop parameter descriptions
     */
    std::vector<LoopParameterDescription> LoopDescriptions;

    /**
     * Internal storage of loops
     */
    std::vector<llvm::Loop*> *loops;

    /**
     * Method to seed the analysis with the start facts
     * @return Returns seeds added to the analysis
     */
    [[nodiscard]] psr::InitialSeeds<n_t, d_t, l_t> initialSeeds() override;

    /**
     * Create a new top element from the used lattice
     * @return Top element of the lattice
     */
    l_t topElement() override;

    /**
     * Create a new bottom element from the used lattice
     * @return Bottom element of the lattice
     */
    l_t bottomElement() override;

    /**
     * Perform a join of the two given lattice values
     * @param Lhs Left lattice value
     * @param Rhs Right lattice value
     * @return Joined value of the left and right value
     */
    l_t join(l_t Lhs, l_t Rhs) override;

    /**
     * Returns an edge function that maps every lattice value to top
     * We just use the base version of phasar here and do not perform any extra work.
     * @return Edge function that returns top
     */
    psr::EdgeFunction<l_t> allTopFunction() override;

    /**
     * Checks if the block encoded by curr has an edge to the loop header. We therefore need to check if succ is the
     * header of the loop.
     *
     * We need this function to hinder phasar in propagating values along the backedge of the loop.
     *
     * @param Curr Current node we are looking at
     * @param Succ Follow-up node
     * @return Returns true if curr is the latch and succ is the header. False otherwise
     */
    bool isLatchToHeaderEdge(n_t Curr, n_t Succ) const;

    /**
     * Normal flow function.
     * Just returns the IdentityFlow.
     *
     * @param Curr Current node
     * @param Succ Next node
     * @return Returns the IdentityFlow
     */
    FlowFunctionPtrType getNormalFlowFunction(n_t Curr, n_t Succ) override;

    /**
     * Call flow function.
     * Just returns the IdentityFlow, as the LoopBound analysis is entirely intraprocedual.
     *
     * @param CallSite Current node
     * @param Callee Next node
     * @return Returns the IdentityFlow
     */
    FlowFunctionPtrType getCallFlowFunction(n_t CallSite, f_t Callee) override;

    /**
     * Ret flow function.
     * Just returns the IdentityFlow, as the LoopBound analysis is entirely intraprocedual.
     *
     * @param CallSite Current node
     * @param Callee Next node
     * @param ExitStmt Exit node
     * @param RetSite Return node
     * @return Returns the IdentityFlow
     */
    FlowFunctionPtrType getRetFlowFunction(n_t CallSite, f_t Callee, n_t ExitStmt, n_t RetSite) override;

    /**
     * CallRet flow function
     * Just returns the KeepLocalOnCallToRet, as the LoopBound analysis is entirely intraprocedual.
     *
     * @param CallSite Call node
     * @param RetSite Return node
     * @param Callees Called functions
     * @return Return the KeepLocalOnCallToRet flow
     */
    FlowFunctionPtrType getCallToRetFlowFunction(n_t CallSite, n_t RetSite, llvm::ArrayRef<f_t> Callees) override;

    /**
     * Normal edge function
     *
     * Performs the detection of the relevant instructions for our analysis.
     *
     * Emits a DeltaIntervalIdentity for every case except if we encounter a store instruction.
     * In this case a DeltaIntervalCollect is emitted.
     *
     * @param Curr Current node
     * @param CurrNode Fact of the current node
     * @param Succ Next node
     * @param SuccNode Fact of the next node
     * @return EdgeFunction with either DeltaIntervalIdentity or DeltaIntervalCollect
     */
    EdgeFunctionType getNormalEdgeFunction(n_t Curr, d_t CurrNode, n_t Succ, d_t SuccNode) override;

    /**
     * Call edge function
     *
     * Unused, as analysis is intraprocedual.
     *
     * @param CallSite Node of the call
     * @param SrcNode Fact at the call site
     * @param DestFun Called function
     * @param DestNode Fact at the site of the called function
     * @return Returns an edge function with DeltaIntervalIdentity
     */
    EdgeFunctionType getCallEdgeFunction(n_t CallSite, d_t SrcNode, f_t DestFun, d_t DestNode) override;

    /**
     * Return edge function
     *
     * Unused, as analysis is intraprocedual.
     *
     * @param CallSite Node of the call
     * @param Callee Callee function
     * @param ExitStmt
     * @param ExitNode
     * @param RetSite
     * @param RetNode
     * @return Returns an edge function with DeltaIntervalIdentity
     */
    EdgeFunctionType getReturnEdgeFunction(n_t CallSite, f_t Callee, n_t ExitStmt, d_t ExitNode, n_t RetSite,
                                           d_t RetNode) override;

    /**
     * Call to ret edge function
     *
     * Unused, as analysis is intraprocedual.
     *
     * @param CallSite Node of the call
     * @param CallNode Fact of the call
     * @param RetSite
     * @param RetSiteNode
     * @param Callees
     * @return Returns an edge function with DeltaIntervalIdentity
     */
    EdgeFunctionType getCallToRetEdgeFunction(n_t CallSite, d_t CallNode,
                                              n_t RetSite, d_t RetSiteNode,
                                              llvm::ArrayRef<f_t> Callees) override;


    /**
     * Queries our loop descriptions to find a description with the root being the given instruction
     * @param inst Instruction to search for
     * @return Returns a LoopParameterDescription if it can be found, nullptr otherwise
     */
    const LoopParameterDescription *getLoopDescriptionForInst(const llvm::Instruction *inst) const;

    /**
     * Check if the given fact is the counter root of the loop description the given instruction is contained in.
     * Especially performs an in-function check to validate the counter root and the instruction are located inside
     * the same function.
     *
     * @param Fact Fact to analyse
     * @param AtInst Instruction to check at
     * @return Returns true if the fact is the counter root, false otherwise
     */
    bool isCounterRootFactAtInst(d_t Fact, n_t AtInst) const;

    /**
     * Searches the interal representation of loops for possible loop counters.
     * If a loop counter is found, we create a LoopParameterDescription and safe it internally
     */
    void findLoopCounters();

    /**
     * Takes the given ICMP inst peels the operators from it until it finds the corresponding counter root
     * @param inst ICMP instruction to analyze
     * @param loop Loop the icmp belongs to
     * @return Returns an optional value containing the loop counter icmp description if it can be found
     */
    std::optional<LoopCounterICMP> findCounterFromICMP(llvm::ICmpInst *inst, llvm::Loop *loop);

    /**
     * Start at the given ICMP instruction and analyse the operands until we reach the corresponding
     * phi instruction to determine the loop counter.
     * @param start Instruction to start from
     * @param loop Loop the analysis runs in
     * @return Vector of roots corresponding to the start node
     */
    std::vector<const llvm::Value *> sliceBackwards(llvm::Value *start, llvm::Loop *loop);

    /**
     * Checks if the given load instruction is reading from a memory location that is written inside the given loop
     *
     * @param inst Load isntruction to check
     * @param loop Loop to check
     * @return Returns true if the load reads from a memory location that is modified inside the loop. False otherwise
     */
    bool loadIsCarriedIn(const llvm::LoadInst *inst, llvm::Loop *loop);

    /**
     * Checks if the given value is relevant to the loop.
     * The value is not relevant if:
     * - It is a constant
     * - It is not contained inside the loop
     * - It is a function argument
     *
     * In any other case it is relevant
     *
     * @param val Value to analyze
     * @param loop Corresponding loop
     * @return True if the value is not relevant, false otherwise.
     */
    bool isIrrelevantToLoop(const llvm::Value *val, llvm::Loop *loop);

    /**
     * Find init value of given counter value
     * @param Addr Counter to analyze
     * @param loop Corresponding loop
     * @return Optional of the found value
     */
    std::optional<int64_t> findConstInitForCell(const llvm::Value *Addr, llvm::Loop *loop);

    /**
     * Checks if the given load instruction is loading the given counter root
     *
     * @param value Load instruction to check
     * @param root Counter root to compare to
     * @return Returns true if the load references the counter, false otherwise
     */
    static bool isLoadOfCounterRoot(llvm::Value *value, const llvm::Value *root);
};

}  // namespace LoopBound

#endif  // SRC_SPEAR_ANALYSES_LOOPBOUND_LOOPBOUND_H_
