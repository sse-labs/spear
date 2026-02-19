/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityElement.h"
#include "analyses/feasibility/FeasibilityElement.h"

#include "analyses/feasibility/util.h"

namespace Feasibility {

FeasibilityAnalysisManager::FeasibilityAnalysisManager(std::unique_ptr<z3::context> ctx)
    : OwnedContext(std::move(ctx)),
      Solver(*OwnedContext) {

    Context = OwnedContext.get();
    Formulas = std::vector<z3::expr>();

    // Add true formular at id 0
    Formulas.push_back(Context->bool_val(true));
    // Add false formular at id 1
    Formulas.push_back(Context->bool_val(false));
    // Add true formular at id 2 for initial normal values
    Formulas.push_back(Context->bool_val(true));
}

uint32_t FeasibilityAnalysisManager::mkAtomic(z3::expr a) {
    Formulas.push_back(a);
    return Formulas.size() - 1;
}

uint32_t FeasibilityAnalysisManager::mkAnd(uint32_t aId, uint32_t bId) {
    auto a = this->getExpression(aId);
    auto b = this->getExpression(bId);

    z3::expr f = a && b;
    // We should add a simplifier here to avoid creating too many complex formulas that are not simplified,
    // which can lead to performance issues in the solver.
    // But do not use .simplify() as it is too aggressive
    auto res = f;

    // llvm::errs() << "\t[mkand] " << a.to_string() << " && " << b.to_string() << " -> " << res.to_string() << "\n";

    auto potentialid = this->findFormulaId(res);

    // When calculating the conjunction of two formulas, we might end up with a formula that is already present in our manager.
    // In this case, we should reuse the existing formula ID instead of adding a new one to avoid unnecessary
    // duplication and to keep our formula vector manageable.
    if (potentialid.has_value()) {
        // llvm::errs() << "\t\t\t\t" << "Found existing formula ID for";
        return potentialid.value();
    }

    // If we do not find an existing formula ID, we add the new formula to the manager and return its ID.
    Formulas.push_back(f);
    return Formulas.size() - 1;
}

uint32_t FeasibilityAnalysisManager::mkOr(uint32_t aId, uint32_t bId) {
    auto a = this->getExpression(aId);
    auto b = this->getExpression(bId);

    z3::expr f = a || b;
    // We should add a simplifier here to avoid creating too many complex formulas that are not simplified,
    // which can lead to performance issues in the solver.
    // But do not use .simplify() as it is too aggressive
    auto res = f;

    auto potentialid = this->findFormulaId(res);

    // llvm::errs() << "\t[mkor] " << aId << " || " << bId << " -> " << potentialid << "\n";
    // llvm::errs() << "\t[mkor] " << a.to_string() << " || " << b.to_string() << " -> " << res.to_string() << "\n";

    // When calculating the conjunction of two formulas, we might end up with a formula that is already present in our manager.
    // In this case, we should reuse the existing formula ID instead of adding a new one to avoid unnecessary
    // duplication and to keep our formula vector manageable.
    if (potentialid.has_value()) {
        // llvm::errs() << "\t\t\t\t" << "Found existing formula ID for";
        return potentialid.value();
    }

    // If we do not find an existing formula ID, we add the new formula to the manager and return its ID.
    Formulas.push_back(f);
    return Formulas.size() - 1;
}

uint32_t FeasibilityAnalysisManager::mkNot(z3::expr a) {
    z3::expr f = !a;
    Formulas.push_back(f);
    return Formulas.size() - 1;
}

z3::expr FeasibilityAnalysisManager::getExpression(uint32_t id) {

    /*for (int i = 0; i < Formulas.size(); i++) {
        llvm::errs() << "\t\t\t\t" << "Formula ID: " << i << " Formula: " << Formulas[i].to_string() << "\n";
    }*/

    if (id < Formulas.size()) {
        return Formulas[id];
    } else {
        // Return a default expression (e.g., true) if the ID is out of bounds. In practice, you might want to handle this case differently, such as throwing an exception or returning an optional.
        llvm::errs() << "WARNING " << "OUT OF BOUNDS FORMULA ID: " << id << "\n";
        return Context->bool_val(true);
    }
}

std::optional<uint32_t> FeasibilityAnalysisManager::findFormulaId(z3::expr expr) {
    // llvm::errs() << "\t\t\t" << "Finding formula ID for expression: " << expr.to_string() << "\n";

    for (uint32_t i = 0; i < Formulas.size(); i++) {
        z3::expr f = Formulas[i];
        auto first = expr.to_string();
        auto second = f.to_string();


        // llvm::errs() << "\t\t\t\t" << first << " == " << second << " ["<<  i << "]" << "[" << Formulas[i].to_string() << "]" << " Result " << (first == second) << "\n";


        if (first == second) {
            return i;
        }
    }
    return std::nullopt;
}

bool FeasibilityAnalysisManager::isSat(uint32_t id) {
    auto expr = this->getExpression(id);
    this->Solver.push();
    this->Solver.add(expr);
    bool result = this->Solver.check() == z3::sat;
    this->Solver.pop();
    return result;
}

FeasibilityElement FeasibilityElement::createElement(FeasibilityAnalysisManager *man, uint32_t formulaId, Kind type) {
    // Initialize the element with the initial formula (true) and a reference to the manager to access shared resources.
    return FeasibilityElement(type, formulaId, man);
}

FeasibilityElement::Kind FeasibilityElement::getKind() {
    return kind;
}

bool FeasibilityElement::isTop() const {
    return Kind::Top == kind;
}

bool FeasibilityElement::isBottom() const {
    return Kind::Bottom == kind;
}

FeasibilityElement FeasibilityElement::join(FeasibilityElement &other) const {
    auto A = this->manager->getExpression(this->formularID);
    auto B = other.manager->getExpression(other.formularID);

    auto newId = this->manager->mkOr(A, B);
    auto newElement = FeasibilityElement(Kind::Normal, newId, this->manager);

    return newElement;
}

FeasibilityAnalysisManager *FeasibilityElement::getManager() {
    return this->manager;
}

bool FeasibilityElement::isFeasible() const {
    if (this->isTop()) {
        return true;
    }

    if (this->isBottom()) {
        return false;
    }

    auto formula =this->manager->getExpression(this->formularID);
    this->manager->Solver.push();
    this->manager->Solver.add(formula);
    bool result = this->manager->Solver.check() == z3::sat;
    this->manager->Solver.pop();
    return result;
}

bool FeasibilityElement::isInFeasible() const {
    return !isFeasible();
}


std::string FeasibilityElement::toString() const {
    if (kind == Kind::Bottom) {
        return "⊥";
    } else if (kind == Kind::Top) {
        return "⊤";
    } else {
        auto formula = this->manager->getExpression(this->formularID);
        std::string s;
        llvm::raw_string_ostream rso(s);
        rso << formula.to_string();
        rso.flush();
        return s;
    }
}

std::ostream &operator<<(std::ostream &os, const std::optional<FeasibilityElement> &E) {
    return os << toString(E);
}

std::string toString(const std::optional<FeasibilityElement> &E) {
    if (E.has_value()) {
        return E->toString();
    } else {
        return "nullopt";
    }
}

bool FeasibilityElement::operator==(const FeasibilityElement &other) const {
    return kind == other.kind && formularID == other.formularID && manager == other.manager;
}

bool FeasibilityElement::operator!=(const FeasibilityElement &other) const {
    return !(*this == other);
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const FeasibilityElement &E) {
    return os << toString(E);
}

std::string toString(const std::optional<FeasibilityElement::Kind> &K) {
    if (K.has_value()) {
        switch (K.value()) {
            case FeasibilityElement::Kind::Top:
                return "Top";
            case FeasibilityElement::Kind::Bottom:
                return "Bottom";
            case FeasibilityElement::Kind::Normal:
                return "Normal";
            default:
                return "Unknown";
        }
    } else {
        return "nullopt";
    }
}

std::ostream &operator<<(std::ostream &os, const std::optional<FeasibilityElement::Kind> &K) {
    return os << toString(K);
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const FeasibilityElement::Kind &K) {
    return os << toString(K);
}

} // namespace Feasibility
