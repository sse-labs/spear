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
    llvm::errs() << "Inst: " << *inst
             << "  #InFacts=" << In.size();
    if (!In.empty()) llvm::errs() << "  first.mem=" << In.begin()->memoryEnvironment.size();
    llvm::errs() << "\n";

    llvm::errs() << "VISIT node@" << (const void*)inst << " : " << *inst
             << "  #InFacts=" << In.size() << "\n";

    //printContainer(llvm::errs(), In);

    /**
     * We need to implement the actual flow function logic here.
     * This will depend on the specific instructions and how they affect the path conditions.
     *
     * This boils down to the following steps:
     * 1. Analyze the instruction to determine how it affects the path condition.
     *  -> Handle constants
     *  -> Handle comparisons (e.g., if (x < 5) -> add constraint x < 5 to the path condition)
     * 2. For each fact in the input set, create a new fact that incorporates the effect of the instruction.
     * 3. Return the set of new facts as the output of the flow function.
     */

    // Deal with empty input and missing instructions
    if (!inst) {
        return In;
    }

    mono_container_t Out;

    // If the incoming fact is empty. We add the true expression as initial value
    if (In.empty()) {
        return In;
    }

    // Init the out value as in value to deal with instructions that do not affect the path condition.
    // We will update it if we find an instruction that affects the path condition.
    Out = In;

    if (auto *loadInst = llvm::dyn_cast<llvm::LoadInst>(inst)) {
        llvm::errs() << "Handling load instruction: " << *loadInst << "\n";

        auto Succs = this->getCFG()->getSuccsOf(inst);
        llvm::errs() << "Node@" << (const void*)inst << " succs:\n";
        for (auto S : Succs) {
            llvm::errs() << "  succ@" << (const void*)S << " : " << *S << "\n";
        }

        const llvm::Value *Ptr = loadInst->getPointerOperand()->stripPointerCasts();
        llvm::Type *Ty = loadInst->getType();

        mono_container_t loadout;
        for (const auto &fact : In) {
            auto it = fact.memoryEnvironment.find(Ptr);

            z3::expr loaded = context->bool_val(true);

            if (it != fact.memoryEnvironment.end()) {
                loaded = it->second;
            } else if (Ty->isIntegerTy()) {
                unsigned bw = llvm::cast<llvm::IntegerType>(Ty)->getBitWidth();
                loaded = context->bv_const(
                    (loadInst->hasName()
                         ? loadInst->getName().str()
                         : ("load_" + std::to_string((uintptr_t)loadInst)))
                        .c_str(),
                    bw);
            } else if (Ty->isPointerTy()) {
                const llvm::DataLayout &DL = loadInst->getModule()->getDataLayout();
                unsigned as = llvm::cast<llvm::PointerType>(Ty)->getAddressSpace();
                unsigned bw = DL.getPointerSizeInBits(as);
                loaded = context->bv_const(
                    (loadInst->hasName()
                         ? loadInst->getName().str()
                         : ("ptrload_" + std::to_string((uintptr_t)loadInst)))
                        .c_str(),
                    bw);
            } else {
                loadout.insert(fact);
                continue;
            }

            auto nf = fact.defineSSA(loadInst, loaded);
            loadout.insert(nf);
        }
        return loadout;
    }

    if (auto *storeinst = llvm::dyn_cast<llvm::StoreInst>(inst)) {
        llvm::errs() << "Handling store instruction: " << *storeinst << "\n";

        const llvm::Value *valOp = storeinst->getValueOperand();
        const llvm::Value *ptrOp = storeinst->getPointerOperand()->stripPointerCasts(); // KEY

        if (auto *constval = llvm::dyn_cast<llvm::ConstantInt>(valOp)) {
            llvm::errs() << "Handling constant store: " << *constval << "\n";

            mono_container_t storeout;

            // Create the BV constant with the correct bitwidth (supports >64 via string)
            unsigned bw = constval->getBitWidth();
            uint64_t bits = constval->getValue().getZExtValue();
            z3::expr c = context->bv_val(bits, bw);

            // Update the environment for each incoming fact
            for (const auto &fact : In) {
                auto newFact = fact.storeMem(ptrOp, c);
                llvm::errs() << "  storeMem -> memEnvSize=" << newFact.memoryEnvironment.size() << "\n";
                storeout.insert(newFact);
            }

            return storeout;
        }

        mono_container_t storeout;
        for (const auto &fact : In) {
            // resolve() already handles: ConstantInt, LoadInst, CastInst, BinaryOperator, SSA env, fresh tmp
            auto rhs = this->resolve(valOp, fact);

            if (!rhs) {
                // unsupported RHS type -> conservative: keep fact unchanged
                storeout.insert(fact);
                continue;
            }

            storeout.insert(fact.storeMem(ptrOp, rhs.value()));
        }

        return storeout;
    }

    if (auto *icmpinst = llvm::dyn_cast<llvm::ICmpInst>(inst)) {
        llvm::errs() << "Handling icmp instruction: " << *icmpinst << "\n";
        mono_container_t icmpout;

        for (auto const& fact : In) {
            auto lhs = this->resolve(icmpinst->getOperand(0), fact);
            auto rhs = this->resolve(icmpinst->getOperand(1), fact);

            if (!lhs || !rhs) {
                // Unsupported operand types, skip this fact
                icmpout.insert(fact);
                continue;
            }

            z3::expr condition = context->bool_val(true);
            switch (icmpinst->getPredicate()) {
                case llvm::CmpInst::Predicate::ICMP_EQ:
                    condition = (lhs.value() == rhs.value());
                    break;
                case llvm::CmpInst::Predicate::ICMP_NE:
                    condition = (lhs.value() != rhs.value());
                    break;
                case llvm::CmpInst::Predicate::ICMP_ULT:
                    condition = z3::ult(lhs.value(), rhs.value());
                    break;
                case llvm::CmpInst::Predicate::ICMP_ULE:
                    condition = z3::ule(lhs.value(), rhs.value());
                    break;
                case llvm::CmpInst::Predicate::ICMP_UGT:
                    condition = z3::ugt(lhs.value(), rhs.value());
                    break;
                case llvm::CmpInst::Predicate::ICMP_UGE:
                    condition = z3::uge(lhs.value(), rhs.value());
                    break;
                case llvm::CmpInst::Predicate::ICMP_SLT:
                    condition = z3::slt(lhs.value(), rhs.value());
                    break;
                case llvm::CmpInst::Predicate::ICMP_SLE:
                    condition = z3::sle(lhs.value(), rhs.value());
                    break;
                case llvm::CmpInst::Predicate::ICMP_SGT:
                    condition = z3::sgt(lhs.value(), rhs.value());
                    break;
                case llvm::CmpInst::Predicate::ICMP_SGE:
                    condition = z3::sge(lhs.value(), rhs.value());
                    break;
                default:
                    // Unsupported predicate. This case should never happen as we only handle ICmpInst.
                    icmpout.insert(fact);
                    continue;
            }

            // Add the condition to the path condition of the fact
            auto newFact = fact.addExpression(condition);
            newFact.print();
            icmpout.insert(newFact);
        }

        return icmpout;
    }

    return Out;
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
    if (Lhs.size() != Rhs.size()) return false;

    auto itL = Lhs.begin();
    auto itR = Rhs.begin();
    for (; itL != Lhs.end(); ++itL, ++itR) {
        if (!equivUnderLess(*itL, *itR)) return false;
    }
    return true;
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

        // We seed the analysis with a single fact that represents the initial path condition (true)
        // at the entry point of the program.
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

std::optional<z3::expr> FeasibilityAnalysis::createIntVal(const llvm::Value *val) {
        if (auto constval = llvm::dyn_cast<llvm::ConstantInt>(val)) {
            unsigned bitwidth = constval->getBitWidth();
            uint64_t numval = constval->getZExtValue();

            return context->bv_val(numval, bitwidth);
        }

        return std::nullopt;
}

std::optional<z3::expr> FeasibilityAnalysis::createBitVal(const llvm::Value *V, const FeasibilityFact &Fact) {
    // constant integer
    if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(V)) {
        unsigned bw = CI->getBitWidth();
        // safe for <=64; for general use CI->getValue().toString(...)
        uint64_t bits = CI->getValue().getZExtValue();
        return context->bv_val(bits, bw);
    }

    // if V is a "location" we stored to before (e.g., pointer operand of store)
    if (auto it = Fact.memoryEnvironment.find(V); it != Fact.memoryEnvironment.end()) {
        return it->second;
    }

    // otherwise: represent as symbolic variable (still handleable!)
    if (V->getType()->isIntegerTy()) {
        unsigned bw = llvm::cast<llvm::IntegerType>(V->getType())->getBitWidth();
        std::string name = V->hasName() ? V->getName().str() : ("tmp_" + std::to_string(reinterpret_cast<uintptr_t>(V)));
        return context->bv_const(name.c_str(), bw);
    }

    // unsupported type (pointers, floats, etc. for now)
    return std::nullopt;
}

std::optional<z3::expr> FeasibilityAnalysis::resolve(const llvm::Value *variable, const FeasibilityFact &fact) {
    if (!variable) {
        return std::nullopt;
    }

    // Resolve constant values
    if (const auto *constVal = llvm::dyn_cast<llvm::ConstantInt>(variable)) {
        unsigned bitwidth = constVal->getBitWidth();
        uint64_t bits = constVal->getZExtValue();

        return context->bv_val(bits, bitwidth);
    }

    // Resolve already defined variables in the SSA environment
    if (auto it = fact.ssaEnvironment.find(variable); it != fact.ssaEnvironment.end()) {
        return it->second;
    }

    // Resolve loads
    if (const auto *loadInst = llvm::dyn_cast<llvm::LoadInst>(variable)) {
        const llvm::Value *ptr = loadInst->getPointerOperand()->stripPointerCasts();

        if (auto memIt = fact.memoryEnvironment.find(ptr); memIt != fact.memoryEnvironment.end()) {
            return memIt->second;
        }

        if (!loadInst->getType()->isIntegerTy()) {
            return std::nullopt;
        }

        unsigned bitwidth = llvm::cast<llvm::IntegerType>(loadInst->getType())->getBitWidth();
        return createFreshBitVal(loadInst, bitwidth, "load");
    }

    // TODO: What about casts????
    if (const auto *Cast = llvm::dyn_cast<llvm::CastInst>(variable)) {
        auto operandExpression = resolve(Cast->getOperand(0), fact);
        if (!operandExpression) return std::nullopt;

        if (!Cast->getType()->isIntegerTy()) return std::nullopt;
        unsigned destinationBitwidth = llvm::cast<llvm::IntegerType>(Cast->getType())->getBitWidth();

        // Bitvector casts
        if (llvm::isa<llvm::ZExtInst>(Cast)) {
            unsigned SrcBW = operandExpression->get_sort().bv_size();
            if (destinationBitwidth > SrcBW) return z3::zext(*operandExpression, destinationBitwidth - SrcBW);
            if (destinationBitwidth < SrcBW) return operandExpression->extract(destinationBitwidth - 1, 0);
            return *operandExpression;
        }
        if (llvm::isa<llvm::SExtInst>(Cast)) {
            unsigned SrcBW = operandExpression->get_sort().bv_size();
            if (destinationBitwidth > SrcBW) return z3::sext(*operandExpression, destinationBitwidth - SrcBW);
            if (destinationBitwidth < SrcBW) return operandExpression->extract(destinationBitwidth - 1, 0);
            return *operandExpression;
        }
        if (llvm::isa<llvm::TruncInst>(Cast)) {
            unsigned SrcBW = operandExpression->get_sort().bv_size();
            if (destinationBitwidth <= SrcBW) return operandExpression->extract(destinationBitwidth - 1, 0);
            // weird trunc (shouldn't happen) -> zext
            return z3::zext(*operandExpression, destinationBitwidth - SrcBW);
        }
        if (llvm::isa<llvm::BitCastInst>(Cast)) {
            // For integers, bitcast is usually no-op in BV world if widths match.
            unsigned SrcBW = operandExpression->get_sort().bv_size();
            if (SrcBW == destinationBitwidth) return *operandExpression;
            if (destinationBitwidth < SrcBW) return operandExpression->extract(destinationBitwidth - 1, 0);
            return z3::zext(*operandExpression, destinationBitwidth - SrcBW);
        }

        return std::nullopt;
    }

    // Binary operators
    if (const auto *binaryOperator = llvm::dyn_cast<llvm::BinaryOperator>(variable)) {
        if (!binaryOperator->getType()->isIntegerTy()) return std::nullopt;

        auto lhs = resolve(binaryOperator->getOperand(0), fact);
        auto rhs = resolve(binaryOperator->getOperand(1), fact);
        if (!lhs || !rhs) return std::nullopt;

        switch (binaryOperator->getOpcode()) {
            case llvm::Instruction::Add:
                return (*lhs) + (*rhs);
            case llvm::Instruction::Sub:
                return (*lhs) - (*rhs);
            case llvm::Instruction::Mul:
                return (*lhs) * (*rhs);
            case llvm::Instruction::And:
                return (*lhs) & (*rhs);
            case llvm::Instruction::Or:
                return (*lhs) | (*rhs);
            case llvm::Instruction::Xor:
                return (*lhs) ^ (*rhs);
            case llvm::Instruction::Shl:
                return z3::shl(*lhs, *rhs);
            case llvm::Instruction::LShr:
                return z3::lshr(*lhs, *rhs);
            case llvm::Instruction::AShr:
                return z3::ashr(*lhs, *rhs);
            default:
                break;
        }
        return std::nullopt;
    }

    // Variable is a memory location key
    if (auto memIt = fact.memoryEnvironment.find(variable->stripPointerCasts()); memIt != fact.memoryEnvironment.end()) {
        return memIt->second;
    }

    // Fresh val
    if (variable->getType()->isIntegerTy()) {
        unsigned bitwidth = llvm::cast<llvm::IntegerType>(variable->getType())->getBitWidth();
        return createFreshBitVal(variable, bitwidth, "tmp");
    }

    return std::nullopt;
}

z3::expr FeasibilityAnalysis::createFreshBitVal(const llvm::Value *key, unsigned bitwidth, const char *prefix) {
    std::string name;
    if (key && key->hasName()) {
        name = key->getName().str();
    } else {
        name = std::string(prefix) + "_" +
               std::to_string(reinterpret_cast<uintptr_t>(key));
    }
    return context->bv_const(name.c_str(), bitwidth);
}

FeasibilityAnalysis::mono_container_t FeasibilityAnalysis::allTop() {
    return mono_container_t{FeasibilityFact::TrueExpression(context)};
}

}  // namespace Feasibility
