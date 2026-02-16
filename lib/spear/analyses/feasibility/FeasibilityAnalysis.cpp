/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityAnalysis.h"

#include <phasar/PhasarLLVM/ControlFlow/LLVMBasedICFG.h>
#include <phasar/PhasarLLVM/DataFlow/IfdsIde/LLVMZeroValue.h>

#include "analyses/feasibility/util.h"
#include "analyses/feasibility/FeasibilityEdgeFunction.h" // add
#include "analyses/loopbound/LoopBoundEdgeFunction.h"

namespace Feasibility {

template <typename D, typename ContainerT>
class DebugFlow final : public psr::FlowFunction<D, ContainerT> {
    std::shared_ptr<psr::FlowFunction<D, ContainerT>> Inner;
    const char *Name;
    Feasibility::FeasibilityAnalysis  *A;
    Feasibility::FeasibilityAnalysis ::n_t Curr;
    Feasibility::FeasibilityAnalysis ::n_t Succ;

public:
    DebugFlow(std::shared_ptr<psr::FlowFunction<D, ContainerT>> Inner,
              const char *Name,
              Feasibility::FeasibilityAnalysis  *A,
              Feasibility::FeasibilityAnalysis ::n_t Curr,
              Feasibility::FeasibilityAnalysis ::n_t Succ)
        : Inner(std::move(Inner)), Name(Name), A(A), Curr(Curr), Succ(Succ) {}

    ContainerT computeTargets(D Src) override {
        ContainerT Out = Inner->computeTargets(Src);

        if (F_DEBUG_ENABLED) {
            llvm::errs() << F_TAG << " FF " << Name << "  ";
            if (Curr) { Feasibility::Util::dumpInst(Curr); } else { llvm::errs() << "<null>"; }
            llvm::errs() << "  ->  ";
            if (Succ) { Feasibility::Util::dumpInst(Succ); } else { llvm::errs() << "<null>"; }
            llvm::errs() << "\n" << F_TAG << "   Src=";
            Feasibility::Util::dumpFact(A,
                                static_cast<Feasibility::FeasibilityAnalysis ::d_t>(Src));
            llvm::errs() << "   Targets={";
            bool first = true;
            for (auto T : Out) {
                if (!first) {
                    llvm::errs() << ", ";
                }
                first = false;
                Feasibility::Util::dumpFact(
                    A, static_cast<Feasibility::FeasibilityAnalysis ::d_t>(T));
            }
            llvm::errs() << "}\n";
        }

        return Out;
    }
};

template <typename D, typename ContainerT>
class IdentityFlow final : public psr::FlowFunction<D, ContainerT> {
public:
    ContainerT computeTargets(D Src) override { return ContainerT{Src}; }
};


template <typename D, typename ContainerT>
class KeepLocalOnCallToRet final : public psr::FlowFunction<D, ContainerT> {
public:
    ContainerT computeTargets(D Src) override { return ContainerT{Src}; }
};


template <typename D, typename ContainerT>
class EmptyFlow final : public psr::FlowFunction<D, ContainerT> {
public:
    ContainerT computeTargets(D /*Src*/) override { return ContainerT{}; }
};


template <typename D, typename ContainerT>
class ZeroOnlyFlow final : public psr::FlowFunction<D, ContainerT> {
    Feasibility::FeasibilityAnalysis *A;
public:
    explicit ZeroOnlyFlow(Feasibility::FeasibilityAnalysis *A) : A(A) {}
    ContainerT computeTargets(D Src) override {
        return A->isZeroValue(static_cast<Feasibility::FeasibilityAnalysis::d_t>(Src))
                   ? ContainerT{Src}
                   : ContainerT{};
    }
};


FeasibilityAnalysis::FeasibilityAnalysis(llvm::FunctionAnalysisManager *FAM, const psr::LLVMProjectIRDB *IRDB, const psr::LLVMBasedICFG *ICFG)
: base_t(IRDB, {"main"}, std::optional<d_t>(
    static_cast<d_t>(psr::LLVMZeroValue::getInstance()))) {
    (void)FAM;
    store = std::make_unique<FeasibilityStateStore>();
    this->ICFG = ICFG;
}


psr::InitialSeeds<FeasibilityAnalysis::n_t, FeasibilityAnalysis::d_t,
                  FeasibilityAnalysis::l_t>
FeasibilityAnalysis::initialSeeds() {
    psr::InitialSeeds<n_t, d_t, l_t> Seeds;

    auto Main = this->getProjectIRDB()->getFunctionDefinition("main");
    if (!Main || Main->isDeclaration()) {
        return Seeds;
    }
    if (!ICFG) {
        assert(false && "FeasibilityAnalysis: ICFG is null");
        return Seeds;
    }

    const d_t Zero = this->getZeroValue();
    const l_t IdeNeutral = topElement();

    for (n_t SP : ICFG->getStartPointsOf(&*Main)) {
        Seeds.addSeed(SP, Zero, IdeNeutral);

        if (F_DEBUG_ENABLED) {
            llvm::errs() << F_TAG << " Seed(ICFG) @";
            Feasibility::Util::dumpInst(SP);
            llvm::errs() << "\n";
        }
    }

    return Seeds;
}

FeasibilityAnalysis::d_t FeasibilityAnalysis::zeroValue() const {
    return static_cast<d_t>(psr::LLVMZeroValue::getInstance());
}

bool FeasibilityAnalysis::isZeroValue(d_t Fact) const noexcept{
    return base_t::isZeroValue(Fact);
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::topElement() {
    return l_t::ideNeutral(this->store.get());
}

FeasibilityElement FeasibilityAnalysis::bottomElement() {
    return l_t::bottom(this->store.get());
}

psr::EdgeFunction<FeasibilityAnalysis::l_t> FeasibilityAnalysis::allTopFunction() {
    return psr::AllTop<l_t>{};
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::join(l_t Lhs, l_t Rhs) {
    if (Lhs.isBottom()) {
        return Rhs;
    }
    if (Rhs.isBottom()) {
        return Lhs;
    }

    // reached-but-infeasible should dominate
    if (Lhs.isIdeAbsorbing() || Rhs.isIdeAbsorbing()) {
        return l_t::ideAbsorbing(this->store.get());
    }

    if (Lhs.isIdeNeutral()) {
        return Rhs;
    }
    if (Rhs.isIdeNeutral()) {
        return Lhs;
    }

    return Lhs.join(Rhs);
}

FeasibilityAnalysis::FlowFunctionPtrType
FeasibilityAnalysis::getNormalFlowFunction(n_t Curr, n_t Succ) {
    // Only propagate the Zero fact. This prevents any non-zero facts from
    // reaching "weird" functions/blocks via the ICFG.
    auto Inner = std::make_shared<ZeroOnlyFlow<d_t, container_t>>(this);
    return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "ZeroOnly", this, Curr, Succ);
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getCallFlowFunction(n_t CallSite, f_t Callee) {
    // Intraprocedural: do not propagate any facts into the callee.
    auto Inner = std::make_shared<EmptyFlow<d_t, container_t>>();
    return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "CallEmpty", this, CallSite, CallSite);
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getRetFlowFunction(n_t CallSite, f_t Callee,
                                                                                n_t ExitStmt, n_t RetSite) {
    // Intraprocedural: do not propagate any facts back from the callee.
    auto Inner = std::make_shared<EmptyFlow<d_t, container_t>>();
    return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "RetEmpty", this, CallSite, RetSite);
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getCallToRetFlowFunction(n_t CallSite, n_t RetSite,
                                                                                       llvm::ArrayRef<f_t> Callees) {
    // Intraprocedural: keep facts within the caller across a call site.
    auto Inner = std::make_shared<KeepLocalOnCallToRet<d_t, container_t>>();
    return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "CallToRetKeepLocal", this, CallSite, RetSite);
}

FeasibilityAnalysis::EdgeFunctionType
FeasibilityAnalysis::getNormalEdgeFunction(n_t curr, d_t currNode,
                                           n_t succ, d_t succNode) {
  if (F_DEBUG_ENABLED) {
    llvm::errs() << F_TAG << " EF normal @";
    Feasibility::Util::dumpInst(curr);
    llvm::errs() << "\n" << F_TAG << "   currCond=";
    Feasibility::Util::dumpFact(this, currNode);
    llvm::errs() << "\n" << F_TAG << "   succCond=";
    Feasibility::Util::dumpFact(this, succNode);
    llvm::errs() << "\n";
  }

  // ---------------------------------------------------------------------------
  // 1) Handle conditional branches FIRST (must constrain Zero too!)
  // ---------------------------------------------------------------------------
    if (auto *Br = llvm::dyn_cast<llvm::BranchInst>(curr)) {
        if (Br->isConditional()) {

            const llvm::BasicBlock *SuccBB = succ ? succ->getParent() : nullptr;
            const llvm::BasicBlock *TrueBB = Br->getSuccessor(0);
            const llvm::BasicBlock *FalseBB = Br->getSuccessor(1);

            const bool TakeTrue  = (SuccBB == TrueBB);
            const bool TakeFalse = (SuccBB == FalseBB);

            if (!TakeTrue && !TakeFalse) {
                // Not a CFG successor edge we understand -> don't constrain
                return EF(std::in_place_type<FeasibilityIdentityEF>);
            }

            llvm::Value *CondV = Br->getCondition();
            while (auto *CastI = llvm::dyn_cast<llvm::CastInst>(CondV)) {
                CondV = CastI->getOperand(0);
            }

            // Constant condition fast path (important for your loop example too)
            if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(CondV)) {
                const bool CondTrue = !CI->isZero();
                const bool EdgeTaken = TakeTrue ? CondTrue : !CondTrue;
                return EdgeTaken ? EF(std::in_place_type<FeasibilityIdentityEF>)
                                 : EF(std::in_place_type<FeasibilityAllBottomEF>); // or your "make infeasible" EF
            }

            if (auto *ICmp = llvm::dyn_cast<llvm::ICmpInst>(CondV)) {
                return EF(std::in_place_type<FeasibilityAssumeIcmpEF>, ICmp, /*TakeTrueEdge=*/TakeTrue);
            }

            return EF(std::in_place_type<FeasibilityIdentityEF>);
        }
    }

    // 3) STORE / LOAD effects (MUST run for Zero too)
    if (auto *storeinst = llvm::dyn_cast<llvm::StoreInst>(curr)) {
        const llvm::Value *valOp = storeinst->getValueOperand();
        const llvm::Value *ptrOp = storeinst->getPointerOperand()->stripPointerCasts();

        if (auto *constval = llvm::dyn_cast<llvm::ConstantInt>(valOp)) {
            unsigned bw = constval->getBitWidth();
            uint64_t bits = constval->getValue().getZExtValue();
            z3::expr c = store->ctx().bv_val(bits, bw);
            const auto valueId = store->internExpr(c);
            return EF(std::in_place_type<FeasibilitySetMemEF>, ptrOp, valueId);
        }

        return EF(std::in_place_type<FeasibilityIdentityEF>);
    }

    if (auto *loadInst = llvm::dyn_cast<llvm::LoadInst>(curr)) {
        const llvm::Value *loc = loadInst->getPointerOperand()->stripPointerCasts();
        const llvm::Value *key = loadInst;
        return EF(std::in_place_type<FeasibilitySetSSAEF>, key, loc);
    }

    // 4) now you may keep the "zero-involved => ID" fast path,
    // but it no longer blocks store/load.
    if (currNode != succNode) {
        return EF(std::in_place_type<FeasibilityIdentityEF>);
    }
    if (isZeroValue(currNode) || isZeroValue(succNode)) {
        return EF(std::in_place_type<FeasibilityIdentityEF>);
    }
    return EF(std::in_place_type<FeasibilityIdentityEF>);
}

const llvm::BasicBlock* FeasibilityAnalysis::getSuccBB(FeasibilityAnalysis::n_t Succ) {
    if (!Succ) {
        return nullptr;
    }
    return Succ->getParent();
}

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getCallEdgeFunction(n_t CallSite, d_t SrcNode,
                                                                              f_t DestFun, d_t DestNode) {
    return EF(std::in_place_type<FeasibilityIdentityEF>);
}

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getReturnEdgeFunction(n_t CallSite, f_t Callee,
                                                                                n_t ExitStmt, d_t ExitNode,
                                                                                n_t RetSite, d_t RetNode) {
    return EF(std::in_place_type<FeasibilityIdentityEF>);
}

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getCallToRetEdgeFunction(n_t CallSite, d_t CallNode,
                                                                                   n_t RetSite, d_t RetSiteNode,
                                                                                   llvm::ArrayRef<f_t> Callees) {
    return EF(std::in_place_type<FeasibilityIdentityEF>);
}


}  // namespace Feasibility
