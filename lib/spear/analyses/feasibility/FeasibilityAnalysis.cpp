/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityAnalysis.h"

#include <phasar/PhasarLLVM/DataFlow/IfdsIde/LLVMZeroValue.h>

#include "analyses/feasibility/util.h"
#include "analyses/feasibility/FeasibilityEdgeFunction.h" // add

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

FeasibilityAnalysis::FeasibilityAnalysis(llvm::FunctionAnalysisManager *FAM, const psr::LLVMProjectIRDB *IRDB)
: base_t(IRDB, {"main"}, std::optional<d_t>(
    static_cast<d_t>(psr::LLVMZeroValue::getInstance()))) {
    (void)FAM;
    store = std::make_unique<FeasibilityStateStore>();
}


psr::InitialSeeds<FeasibilityAnalysis::n_t, FeasibilityAnalysis::d_t, FeasibilityAnalysis::l_t>
FeasibilityAnalysis::initialSeeds() {
    psr::InitialSeeds<n_t, d_t, l_t> Seeds;

    psr::Nullable<f_t> Main = this->getProjectIRDB()->getFunctionDefinition("main");
    if (!Main || Main->isDeclaration()) {
        // No seeds => analysis yields TOP everywhere (or empty results)
        return Seeds;
    }

    // --- Pick a start statement/node ---
    const llvm::BasicBlock *EntryBB = &Main->getEntryBlock();

    // Usually OK: first instruction in entry block
    // (If you want: skip PHIs/Dbg, but entry typically has no PHIs)
    n_t Start = &*EntryBB->begin();
    if (!Start) return Seeds;

    // --- Seed only the zero fact with the initial lattice value ---
    d_t Zero = this->zeroValue();

    // New FeasibilityElement API
    l_t Init = l_t::initial(this->store.get());

    Seeds.addSeed(Start, Zero, Init);

    return Seeds;
}

FeasibilityAnalysis::d_t FeasibilityAnalysis::zeroValue() {
    return static_cast<d_t>(psr::LLVMZeroValue::getInstance());
}

bool FeasibilityAnalysis::isZeroValue(d_t Fact) const noexcept{
    return base_t::isZeroValue(Fact);
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::topElement() {
    return l_t::top(this->store.get());
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::bottomElement() {
    return l_t::bottom(this->store.get());
}

FeasibilityAnalysis::l_t FeasibilityAnalysis::join(l_t Lhs, l_t Rhs) {
    return Lhs.join(Rhs);
}

psr::EdgeFunction<FeasibilityAnalysis::l_t> FeasibilityAnalysis::allTopFunction() {
    // FeasibilityElement is not trivially copyable, so we must not use psr::AllTop<l_t>.
    return Feasibility::edgeTop();
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getNormalFlowFunction(n_t Curr, n_t Succ) {
    auto Inner = std::make_shared<IdentityFlow<d_t, container_t>>();
    return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "Identity", this, Curr, Succ);
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getCallFlowFunction(n_t CallSite, f_t Callee) {
    (void)CallSite;
    (void)Callee;
    return std::make_shared<IdentityFlow<d_t, container_t>>();
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getRetFlowFunction(n_t CallSite, f_t Callee,
                                                                                n_t ExitStmt, n_t RetSite) {
    (void)CallSite;
    (void)Callee;
    (void)ExitStmt;
    (void)RetSite;
    return std::make_shared<IdentityFlow<d_t, container_t>>();
}

FeasibilityAnalysis::FlowFunctionPtrType FeasibilityAnalysis::getCallToRetFlowFunction(n_t CallSite, n_t RetSite,
                                                                                       llvm::ArrayRef<f_t> Callees) {
    (void)CallSite;
    (void)RetSite;
    (void)Callees;
    return std::make_shared<KeepLocalOnCallToRet<d_t, container_t>>();
}

// IMPORTANT: Define *all* edge-function hooks to avoid base defaults that may instantiate psr::AllTop<l_t>.
FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getNormalEdgeFunction(n_t Curr, d_t CurrNode,
                                                                                n_t Succ, d_t SuccNode) {
    (void)Curr;
    (void)CurrNode;
    (void)Succ;
    (void)SuccNode;
    return Feasibility::edgeIdentity();
}

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getCallEdgeFunction(n_t CallSite, d_t SrcNode,
                                                                              f_t DestFun, d_t DestNode) {
    (void)CallSite;
    (void)SrcNode;
    (void)DestFun;
    (void)DestNode;
    return Feasibility::edgeIdentity();
}

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getReturnEdgeFunction(n_t CallSite, f_t Callee,
                                                                                n_t ExitStmt, d_t ExitNode,
                                                                                n_t RetSite, d_t RetNode) {
    (void)CallSite;
    (void)Callee;
    (void)ExitStmt;
    (void)ExitNode;
    (void)RetSite;
    (void)RetNode;
    return Feasibility::edgeIdentity();
}

FeasibilityAnalysis::EdgeFunctionType FeasibilityAnalysis::getCallToRetEdgeFunction(n_t CallSite, d_t CallNode,
                                                                                   n_t RetSite, d_t RetSiteNode,
                                                                                   llvm::ArrayRef<f_t> Callees) {
    (void)CallSite;
    (void)CallNode;
    (void)RetSite;
    (void)RetSiteNode;
    (void)Callees;
    return Feasibility::edgeIdentity();
}


}  // namespace Feasibility
