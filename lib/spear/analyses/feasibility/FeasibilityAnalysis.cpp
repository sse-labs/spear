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
            Feasibility::Util::dumpInst(Curr);
            llvm::errs() << "  ->  ";
            Feasibility::Util::dumpInst(Succ);
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

    const l_t StartVal = l_t::initial(this->store.get());

    if (F_DEBUG_ENABLED) {
        llvm::errs() << "[FDBG] StartVal==IdeNeutral="
                     << StartVal.equal_to(IdeNeutral) << "\n";
        llvm::errs() << "[FDBG] Zero=" << (const void *)Zero
                     << " isZero(Zero)=" << isZeroValue(Zero) << "\n";
    }

    for (n_t SP : ICFG->getStartPointsOf(&*Main)) {
        const d_t Fact = static_cast<d_t>(SP);

        Seeds.addSeed(SP, Zero, IdeNeutral);
        //Seeds.addSeed(SP, Fact, StartVal);

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
    auto Inner = std::make_shared<IdentityFlow<d_t, container_t>>();
    return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "Identity", this, Curr, Succ);
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getCallFlowFunction(n_t CallSite, f_t Callee) {
    auto Inner = std::make_shared<IdentityFlow<d_t, container_t>>();
    return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "CallIdentity", this, CallSite, CallSite);
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getRetFlowFunction(n_t CallSite, f_t Callee,
                                                                                n_t ExitStmt, n_t RetSite) {
    auto Inner = std::make_shared<IdentityFlow<d_t, container_t>>();
    return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "RetIdentity", this, CallSite, CallSite);
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getCallToRetFlowFunction(n_t CallSite, n_t RetSite,
                                                                                       llvm::ArrayRef<f_t> Callees) {
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
            const llvm::Instruction *TEntry = Feasibility::Util::firstRealInst(Br->getSuccessor(0));
            const llvm::Instruction *FEntry = Feasibility::Util::firstRealInst(Br->getSuccessor(1));

            const bool TakeTrue  = (succ == TEntry);
            const bool TakeFalse = (succ == FEntry);

            if (!TakeTrue && !TakeFalse) {
                if (F_DEBUG_ENABLED) {
                    llvm::errs() << F_TAG << "   branch-succ-match: succ=";
                    Feasibility::Util::dumpInst(succ);
                    llvm::errs() << "  trueEntry=";
                    Feasibility::Util::dumpInst(const_cast<llvm::Instruction*>(TEntry));
                    llvm::errs() << "  falseEntry=";
                    Feasibility::Util::dumpInst(const_cast<llvm::Instruction*>(FEntry));
                    llvm::errs() << "  TakeTrue=" << TakeTrue << " TakeFalse=" << TakeFalse << "\n";
                }

                return EF(std::in_place_type<FeasibilityIdentityEF>);
            }

            llvm::Value *CondV = Br->getCondition();
            while (auto *CastI = llvm::dyn_cast<llvm::CastInst>(CondV)) {
                CondV = CastI->getOperand(0);
            }

            if (auto *ICmp = llvm::dyn_cast<llvm::ICmpInst>(CondV)) {

                if (F_DEBUG_ENABLED) {
                    llvm::errs() << F_TAG << "   branch-succ-match: succ=";
                    Feasibility::Util::dumpInst(succ);
                    llvm::errs() << "  trueEntry=";
                    Feasibility::Util::dumpInst(const_cast<llvm::Instruction*>(TEntry));
                    llvm::errs() << "  falseEntry=";
                    Feasibility::Util::dumpInst(const_cast<llvm::Instruction*>(FEntry));
                    llvm::errs() << "  TakeTrue=" << TakeTrue << " TakeFalse=" << TakeFalse << "\n";
                }

                return EF(std::in_place_type<FeasibilityAssumeIcmpEF>, ICmp, /*TakeTrueEdge=*/TakeTrue);
            }

            if (F_DEBUG_ENABLED) {
                llvm::errs() << F_TAG << "   branch-succ-match: succ=";
                Feasibility::Util::dumpInst(succ);
                llvm::errs() << "  trueEntry=";
                Feasibility::Util::dumpInst(const_cast<llvm::Instruction*>(TEntry));
                llvm::errs() << "  falseEntry=";
                Feasibility::Util::dumpInst(const_cast<llvm::Instruction*>(FEntry));
                llvm::errs() << "  TakeTrue=" << TakeTrue << " TakeFalse=" << TakeFalse << "\n";
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
