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

template <typename D, typename ContainerT>
class GenFromZeroFlow final : public psr::FlowFunction<D, ContainerT> {
    Feasibility::FeasibilityAnalysis *A = nullptr;
    Feasibility::FeasibilityAnalysis::n_t Curr = nullptr;

public:
    GenFromZeroFlow(Feasibility::FeasibilityAnalysis *A,
                    Feasibility::FeasibilityAnalysis::n_t Curr)
        : A(A), Curr(Curr) {}

    ContainerT computeTargets(D Src) override {
        ContainerT Out;
        Out.insert(Src);

        const auto Fact = static_cast<Feasibility::FeasibilityAnalysis::d_t>(Src);
        if (A && A->isZeroValue(Fact)) {
            // Generate ONE non-zero fact from zero so the analysis can actually propagate.
            // Prefer the function object (stable across many edges) if available.
            if (Curr) {
                if (const auto *F = Curr->getFunction()) {
                    Out.insert(static_cast<Feasibility::FeasibilityAnalysis::d_t>(F));
                } else {
                    Out.insert(static_cast<Feasibility::FeasibilityAnalysis::d_t>(Curr));
                }
            }
        }
        return Out;
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
        // Seed zero with IDE-neutral
        Seeds.addSeed(SP, Zero, IdeNeutral);

        // Seed a body-local, definitely-non-zero fact with a non-neutral value
        const d_t Fact = static_cast<d_t>(SP);
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
    // IDE "top" must be the neutral element (identity for edge function lifting).
    return l_t::ideNeutral(this->store.get());
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::bottomElement() {
    // IDE "bottom" must be the absorbing element.
    return l_t::ideAbsorbing(this->store.get());
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::join(l_t Lhs, l_t Rhs) {
    // IDE lattice join must respect neutral/absorbing.
    if (Lhs.isIdeAbsorbing()) {
        return Rhs;
    }
    if (Rhs.isIdeAbsorbing()) {
        return Lhs;
    }
    if (Lhs.isIdeNeutral()) {
        return Rhs;
    }
    if (Rhs.isIdeNeutral()) {
        return Lhs;
    }
    // Delegate to your must-join semantics for "normal/domain" states.
    return Lhs.join(Rhs);
}

psr::EdgeFunction<FeasibilityAnalysis::l_t> FeasibilityAnalysis::allTopFunction() {
    // Must map everything to IDE-neutral (not domain-top).
    // FeasibilityElement is not trivially copyable, so we must not use psr::AllTop<l_t>.
    return psr::AllTop<l_t>{};
}

FeasibilityAnalysis::FlowFunctionPtrType
FeasibilityAnalysis::getNormalFlowFunction(n_t Curr, n_t Succ) {
    auto Inner = std::make_shared<IdentityFlow<d_t, container_t>>();
    return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "NormalIdentity", this, Curr, Succ);
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
        llvm::errs() << "\n" << Feasibility::Util::F_TAG << "   currFact=";
        Feasibility::Util::dumpFact(this, currNode);
        llvm::errs() << "\n" << Feasibility::Util::F_TAG << "   succFact=";
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
