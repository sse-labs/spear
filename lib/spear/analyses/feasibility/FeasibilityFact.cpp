/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityFact.h"

#include <llvm/IR/Argument.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/Support/raw_ostream.h>

namespace Feasibility {

FeasibilityFact FeasibilityFact::TrueExpression(std::shared_ptr<z3::context> context) {
    return FeasibilityFact(context, context->bool_val(true));
}

FeasibilityFact FeasibilityFact::FalseExpression(std::shared_ptr<z3::context> context) {
    return FeasibilityFact(context, context->bool_val(false));
}

FeasibilityFact FeasibilityFact::addExpression(const z3::expr &constraint) const {
    FeasibilityFact out(*this);
    out.pathExpression = (out.pathExpression && constraint).simplify();
    return out;
}

bool FeasibilityFact::operator<(const FeasibilityFact &O) const noexcept {
    // 1) PC
    unsigned h1 = astHash(pathExpression);
    unsigned h2 = astHash(O.pathExpression);
    if (h1 != h2) return h1 < h2;

    auto keyPtr = [](const llvm::Value *V) {
        return reinterpret_cast<uintptr_t>(V);
    };

    // 2) Memory env: size, then sorted (key ptr, value hash)
    if (memoryEnvironment.size() != O.memoryEnvironment.size())
        return memoryEnvironment.size() < O.memoryEnvironment.size();

    std::vector<std::pair<uintptr_t, unsigned>> MA, MB;
    MA.reserve(memoryEnvironment.size());
    MB.reserve(O.memoryEnvironment.size());

    for (auto &kv : memoryEnvironment) MA.emplace_back(keyPtr(kv.first), astHash(kv.second));
    for (auto &kv : O.memoryEnvironment) MB.emplace_back(keyPtr(kv.first), astHash(kv.second));

    std::sort(MA.begin(), MA.end());
    std::sort(MB.begin(), MB.end());

    if (MA != MB) return MA < MB;

    // 3) SSA env: size, then sorted (key ptr, value hash)
    if (ssaEnvironment.size() != O.ssaEnvironment.size())
        return ssaEnvironment.size() < O.ssaEnvironment.size();

    std::vector<std::pair<uintptr_t, unsigned>> SA, SB;
    SA.reserve(ssaEnvironment.size());
    SB.reserve(O.ssaEnvironment.size());

    for (auto &kv : ssaEnvironment) SA.emplace_back(keyPtr(kv.first), astHash(kv.second));
    for (auto &kv : O.ssaEnvironment) SB.emplace_back(keyPtr(kv.first), astHash(kv.second));

    std::sort(SA.begin(), SA.end());
    std::sort(SB.begin(), SB.end());

    if (SA != SB) return SA < SB;

    return false;
}

bool FeasibilityFact::operator==(const FeasibilityFact &o) const noexcept {
    return !(*this < o) && !(o < *this);
}

bool FeasibilityFact::isFeasible() {
    z3::solver solverComponent(*this->solverContext);
    solverComponent.add(this->pathExpression);
    return solverComponent.check() == z3::sat;
}

FeasibilityFact FeasibilityFact::simplify() const {
    FeasibilityFact out(*this);
    out.pathExpression = out.pathExpression.simplify();
    return out;
}

FeasibilityFact FeasibilityFact::storeMem(const llvm::Value *location, const z3::expr &value) const {
    FeasibilityFact out(*this);
    out.memoryEnvironment.insert_or_assign(location, value);
    return out;
}

FeasibilityFact FeasibilityFact::defineSSA(const llvm::Value *location, const z3::expr &value) const {
    FeasibilityFact out(*this);
    out.ssaEnvironment.insert_or_assign(location, value);
    return out;
}

void FeasibilityFact::print() const {
    llvm::errs() << "PC: " << pathExpression.to_string() << "\n";
    llvm::errs() << "Env size: " << memoryEnvironment.size() << "\n";
    llvm::errs() << "SSA size: " << ssaEnvironment.size() << "\n";
    for (const auto &entry : ssaEnvironment) {
        llvm::errs() << "  ssa@" << (const void*)entry.first
                     << " -> " << entry.second.to_string() << "\n";
    }

    for (const auto &entry : memoryEnvironment) {
        const llvm::Value *V = entry.first;

        // Print a useful key, even if unnamed
        llvm::errs() << "  key@" << (const void*)V << " ";

        if (V) {
            if (V->hasName()) {
                llvm::errs() << "%" << V->getName();
            } else {
                // Print the LLVM IR for the value (works for instructions, args, globals, etc.)
                llvm::errs() << "<unnamed> ";
                if (auto *I = llvm::dyn_cast<llvm::Instruction>(V)) {
                    llvm::errs() << I; // prints instruction text
                } else if (auto *A = llvm::dyn_cast<llvm::Argument>(V)) {
                    llvm::errs() << "arg:" << A->getArgNo();
                } else if (auto *GV = llvm::dyn_cast<llvm::GlobalValue>(V)) {
                    llvm::errs() << "global:" << GV->getName();
                } else {
                    llvm::errs() << "value";
                }
            }
        } else {
            llvm::errs() << "<null>";
        }

        llvm::errs() << " -> " << entry.second.to_string() << "\n";
    }
}

}  // namespace Feasibility
