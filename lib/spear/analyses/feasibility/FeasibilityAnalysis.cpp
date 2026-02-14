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

        if (Feasibility::Util::F_DebugEnabled.load()) {
            llvm::errs() << Feasibility::Util::F_TAG << " FF " << Name << "  ";
            Feasibility::Util::dumpInst(Curr);
            llvm::errs() << "  ->  ";
            Feasibility::Util::dumpInst(Succ);
            llvm::errs() << "\n" << Feasibility::Util::F_TAG << "   Src=";
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

    // IMPORTANT: use the base-class zero value instance (like LoopBound does)
    const d_t Zero = this->getZeroValue();

    // IDE semantics: topElement() must be IDE-neutral
    const l_t IdeNeutral = topElement();

    // Must be a non-neutral value for the non-zero seed
    const l_t StartVal = l_t::initial(this->store.get());

    if (Feasibility::Util::F_DebugEnabled.load()) {
        llvm::errs() << "[FDBG] StartVal==IdeNeutral="
                     << StartVal.equal_to(IdeNeutral) << "\n";
        llvm::errs() << "[FDBG] Zero=" << (const void *)Zero
                     << " isZero(Zero)=" << isZeroValue(Zero) << "\n";
    }

    for (n_t SP : ICFG->getStartPointsOf(&*Main)) {
        const d_t Fact = static_cast<d_t>(SP);

        Seeds.addSeed(SP, Zero, IdeNeutral);
        Seeds.addSeed(SP, Fact, StartVal);

        if (Feasibility::Util::F_DebugEnabled.load()) {
            llvm::errs() << Feasibility::Util::F_TAG << " Seed(ICFG) @";
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
    // Value-lattice TOP: "unknown" (loss of precision).
    return l_t::ideNeutral(this->store.get());
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::bottomElement() {
    // Value-lattice BOTTOM: "infeasible".
    return l_t::ideAbsorbing(this->store.get());
}

psr::EdgeFunction<FeasibilityAnalysis::l_t> FeasibilityAnalysis::allTopFunction() {
    // Edge-function TOP in the EF lattice: maps any value to value-top ("unknown").
    return psr::AllTop<l_t>{};
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::join(l_t Lhs, l_t Rhs) {
    // Value lattice join (may-merge / union)

    l_t Res;

    if (Lhs.isBottom()) {
        Res = Rhs;
    } else if (Rhs.isBottom()) {
        Res = Lhs;
    } else if (Lhs.isIdeAbsorbing() || Rhs.isIdeAbsorbing()) {
        Res = l_t::ideAbsorbing(this->store.get());
    } else if (Lhs.isIdeNeutral()) {
        Res = Rhs;
    } else if (Rhs.isIdeNeutral()) {
        Res = Lhs;
    } else {
        Res = Lhs.join(Rhs);
    }

    if (Feasibility::Util::F_DebugEnabled.load()) {
        llvm::errs() << "[FDBG] join: " << Lhs << " lub " << Rhs << " = " << Res << "\n";
    }
    return Res;
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

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getNormalEdgeFunction(n_t curr, d_t currNode,
                                                                                n_t succ, d_t succNode) {
    if (Feasibility::Util::F_DebugEnabled.load()) {
        llvm::errs() << Feasibility::Util::F_TAG << " EF normal @";
        Feasibility::Util::dumpInst(curr);
        llvm::errs() << "\n" << Feasibility::Util::F_TAG << "   currCond=";
        Feasibility::Util::dumpFact(this, currNode);
        llvm::errs() << "\n" << Feasibility::Util::F_TAG << "   succCond=";
        Feasibility::Util::dumpFact(this, succNode);
        llvm::errs() << "\n";
    }

    if (isZeroValue(currNode) || isZeroValue(succNode)) {
        auto E = EF(std::in_place_type<FeasibilityIdentityEF>);
        if (Feasibility::Util::F_DebugEnabled.load()) {
            llvm::errs() << Feasibility::Util::F_TAG << "   reason=zero-involved  ";
            Feasibility::Util::dumpEF(E);
            llvm::errs() << "\n";
        }
        return E;
    }

    if (currNode != succNode) {
        auto E = EF(std::in_place_type<FeasibilityIdentityEF>);
        if (Feasibility::Util::F_DebugEnabled.load()) {
            llvm::errs() << Feasibility::Util::F_TAG << "   reason=fact-changed(curr!=succ)  ";
            Feasibility::Util::dumpEF(E);
            llvm::errs() << "\n";
        }
        return E;
    }

    /**
     * We need to deal with the instructions here.
     * Deal with
     * - store instructions
     * - load instructions
     * - br instructions
     *
     */

    if (auto *storeinst = llvm::dyn_cast<llvm::StoreInst>(curr)) {
        llvm::errs() << "Handling store instruction: " << *storeinst << "\n";

        const llvm::Value *valOp = storeinst->getValueOperand();
        const llvm::Value *ptrOp = storeinst->getPointerOperand()->stripPointerCasts(); // KEY

        if (auto *constval = llvm::dyn_cast<llvm::ConstantInt>(valOp)) {
            llvm::errs() << "Handling constant store: " << *constval << "\n";

            // Create the BV constant with the correct bitwidth (supports >64 via string)
            unsigned bw = constval->getBitWidth();
            uint64_t bits = constval->getValue().getZExtValue();
            z3::expr c = store->ctx().bv_val(bits, bw);

            auto E = EF(std::in_place_type<FeasibilitySetMemEF>, ptrOp, c);
            if (Feasibility::Util::F_DebugEnabled.load()) {
                llvm::errs() << Feasibility::Util::F_TAG << "   reason=store-involved  ";
                Feasibility::Util::dumpEF(E);
                llvm::errs() << "\n";
            }
            return E;
        }
    }


    if (auto *loadInst = llvm::dyn_cast<llvm::LoadInst>(curr)) {
        llvm::errs() << "Handling load instruction: " << *loadInst << "\n";

        const llvm::Value *ptrOp = loadInst->getPointerOperand()->stripPointerCasts(); // KEY

        auto E = EF(std::in_place_type<FeasibilitySetSSAEF>, ptrOp, store->ctx().bool_val(true));
        if (Feasibility::Util::F_DebugEnabled.load()) {
            llvm::errs() << Feasibility::Util::F_TAG << "   reason=load-involved  ";
            Feasibility::Util::dumpEF(E);
            llvm::errs() << "\n";
        }
        return E;
    }


    if (auto *brInst = llvm::dyn_cast<llvm::BranchInst>(curr)) {
        llvm::errs() << "Handling branch instruction: " << *brInst << "\n";

        if (brInst->isConditional()) {
            llvm::errs() << "[FDBG] conditional branch seen\n";
            llvm::Value *Cond = brInst->getCondition();

            llvm::BasicBlock *TrueBB  = brInst->getSuccessor(0);
            llvm::BasicBlock *FalseBB = brInst->getSuccessor(1);

            const bool IsTrueEdge  = (succ && succ->getParent() == TrueBB);
            const bool IsFalseEdge = (succ && succ->getParent() == FalseBB);

            llvm::errs() << "Condition: " << *Cond << "\n";
            llvm::errs() << "True successor: " << TrueBB->getName() << "\n";
            llvm::errs() << "False successor: " << FalseBB->getName() << "\n";

            if (!IsTrueEdge && !IsFalseEdge) {
                return EF(std::in_place_type<FeasibilityIdentityEF>);
            }

            z3::expr ZCond = store->ctx().bool_val(true);

            if (auto icmpinst = llvm::dyn_cast<llvm::ICmpInst>(Cond)) {
                llvm::errs() << "Branch condition is an ICmp: " << *icmpinst << "\n";

                auto lhs = Feasibility::Util::resolve(icmpinst->getOperand(0), store.get());
                auto rhs = Feasibility::Util::resolve(icmpinst->getOperand(1), store.get());
                if (!lhs || !rhs) {
                    return EF(std::in_place_type<FeasibilityIdentityEF>);
                }

                switch (icmpinst->getPredicate()) {
                    case llvm::CmpInst::Predicate::ICMP_EQ:
                        ZCond = (lhs.value() == rhs.value());
                        break;
                    case llvm::CmpInst::Predicate::ICMP_NE:
                        ZCond = (lhs.value() != rhs.value());
                        break;
                    case llvm::CmpInst::Predicate::ICMP_ULT:
                        ZCond = z3::ult(lhs.value(), rhs.value());
                        break;
                    case llvm::CmpInst::Predicate::ICMP_ULE:
                        ZCond = z3::ule(lhs.value(), rhs.value());
                        break;
                    case llvm::CmpInst::Predicate::ICMP_UGT:
                        ZCond = z3::ugt(lhs.value(), rhs.value());
                        break;
                    case llvm::CmpInst::Predicate::ICMP_UGE:
                        ZCond = z3::uge(lhs.value(), rhs.value());
                        break;
                    case llvm::CmpInst::Predicate::ICMP_SLT:
                        ZCond = z3::slt(lhs.value(), rhs.value());
                        break;
                    case llvm::CmpInst::Predicate::ICMP_SLE:
                        ZCond = z3::sle(lhs.value(), rhs.value());
                        break;
                    case llvm::CmpInst::Predicate::ICMP_SGT:
                        ZCond = z3::sgt(lhs.value(), rhs.value());
                        break;
                    case llvm::CmpInst::Predicate::ICMP_SGE:
                        ZCond = z3::sge(lhs.value(), rhs.value());
                        break;
                    default:
                        return EF(std::in_place_type<FeasibilityIdentityEF>);
                }

                // Apply cond on true-edge, !cond on false-edge
                const z3::expr EdgeCond = IsTrueEdge ? ZCond : !ZCond;

                auto E = EF(std::in_place_type<FeasibilityAssumeEF>, EdgeCond);
                if (Feasibility::Util::F_DebugEnabled.load()) {
                    llvm::errs() << Feasibility::Util::F_TAG << "   reason=branch-assume("
                                 << (IsTrueEdge ? "T" : "F") << ") -> " <<  EdgeCond.to_string() << "\n";
                }
                return E;
            }
        }
    }

    auto E = EF(std::in_place_type<FeasibilityIdentityEF>);
    return E;
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
