/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityElement.h"
#include "analyses/feasibility/FeasibilityElement.h"

#include <unordered_set>

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


    SatCache.push_back(SatTri::Sat);    // true
    SatCache.push_back(SatTri::Unsat);  // false
    SatCache.push_back(SatTri::Sat);    // initial true

    EnvRoots.push_back(nullptr);
}

uint32_t FeasibilityAnalysisManager::mkAtomic(z3::expr a) {
    if (auto pid = findFormulaId(a)) return *pid;
    Formulas.push_back(a);
    uint32_t id = Formulas.size() - 1;
    FormularsToId.emplace(a, id);
    SatCache.push_back(SatTri::Unknown);
    return id;
}

uint32_t FeasibilityAnalysisManager::mkAnd(uint32_t aId, uint32_t bId) {
    if (aId == FeasibilityElement::bottomId || bId == FeasibilityElement::bottomId) return FeasibilityElement::bottomId;
    if (aId == FeasibilityElement::topId)  return bId;
    if (bId == FeasibilityElement::topId)  return aId;
    if (aId == bId)      return aId;

    // optional: commutativity canonicalization
    if (aId > bId) std::swap(aId, bId);

    auto a = getExpression(aId);
    auto b = getExpression(bId);

    z3::expr res = mkAndSimplified(a, b);

    if (auto pid = findFormulaId(res)) return *pid;

    Formulas.push_back(res);
    uint32_t id = Formulas.size() - 1;
    FormularsToId.emplace(res, id);
    SatCache.push_back(SatTri::Unknown);
    return id;
}

uint32_t FeasibilityAnalysisManager::mkOr(uint32_t aId, uint32_t bId) {
    if (aId == FeasibilityElement::topId || bId == FeasibilityElement::topId) return FeasibilityElement::topId;
    if (aId == FeasibilityElement::bottomId) return bId;
    if (bId == FeasibilityElement::bottomId) return aId;
    if (aId == bId)      return aId;

    if (aId > bId) std::swap(aId, bId);

    auto a = getExpression(aId);
    auto b = getExpression(bId);

    z3::expr res = mkOrSimplified(a, b);

    if (auto pid = findFormulaId(res)) return *pid;

    Formulas.push_back(res);
    uint32_t id = Formulas.size() - 1;
    FormularsToId.emplace(res, id);
    SatCache.push_back(SatTri::Unknown);
    return id;
}

bool FeasibilityAnalysisManager::isBoolTrue(const z3::expr &e) {
  return e.is_true();
}
bool FeasibilityAnalysisManager::isBoolFalse(const z3::expr &e) {
  return e.is_false();
}

bool FeasibilityAnalysisManager::isNotExpr(const z3::expr &e) {
  return e.is_app() && e.decl().decl_kind() == Z3_OP_NOT && e.num_args() == 1;
}

bool FeasibilityAnalysisManager::isNotOf(const z3::expr &a, const z3::expr &b) {
  // a == (not b)
  return isNotExpr(a) && Z3_is_eq_ast(a.ctx(), a.arg(0), b);
}

void FeasibilityAnalysisManager::collectOrArgs(const z3::expr &e, std::vector<z3::expr> &out) {
  if (e.is_app() && e.decl().decl_kind() == Z3_OP_OR) {
    for (unsigned i = 0; i < e.num_args(); ++i) collectOrArgs(e.arg(i), out);
  } else {
    out.push_back(e);
  }
}

void FeasibilityAnalysisManager::collectAndArgs(const z3::expr &e, std::vector<z3::expr> &out) {
  if (e.is_app() && e.decl().decl_kind() == Z3_OP_AND) {
    for (unsigned i = 0; i < e.num_args(); ++i) collectAndArgs(e.arg(i), out);
  } else {
    out.push_back(e);
  }
}

z3::expr FeasibilityAnalysisManager::mkNaryOr(z3::context& ctx, const std::vector<z3::expr>& v) {
    std::vector<Z3_ast> asts;
    asts.reserve(v.size());
    for (auto& e : v) asts.push_back(e);
    return z3::expr(ctx, Z3_mk_or(ctx, (unsigned)asts.size(), asts.data()));
}

z3::expr FeasibilityAnalysisManager::mkNaryAnd(z3::context& ctx, const std::vector<z3::expr>& v) {
    std::vector<Z3_ast> asts;
    asts.reserve(v.size());
    for (auto& e : v) asts.push_back(e);
    return z3::expr(ctx, Z3_mk_and(ctx, (unsigned)asts.size(), asts.data()));
}

z3::expr FeasibilityAnalysisManager::mkOrSimplified(const z3::expr &a,
                                                   const z3::expr &b) {
    z3::context &ctx = a.ctx();

    std::vector<z3::expr> args;
    args.reserve(8);
    collectOrArgs(a, args);
    collectOrArgs(b, args);

    // Dedup of full literals (including (not p))
    std::unordered_set<z3::expr, Z3ExprHash, Z3ExprEq> seen;
    seen.reserve(args.size());

    // For fast tautology detection:
    // store base terms in pos/neg, where literal is either base or (not base)
    std::unordered_set<z3::expr, Z3ExprHash, Z3ExprEq> pos, neg;
    pos.reserve(args.size());
    neg.reserve(args.size());

    std::vector<z3::expr> uniq;
    uniq.reserve(args.size());

    for (auto &x : args) {
        if (isBoolTrue(x)) return ctx.bool_val(true);
        if (isBoolFalse(x)) continue;

        // normalize literal into (base, isNeg)
        if (isNotExpr(x)) {
            z3::expr base = x.arg(0);
            // base OR (not base) => true
            if (pos.find(base) != pos.end()) return ctx.bool_val(true);
            neg.insert(base);
        } else {
            // x OR (not x) => true
            if (neg.find(x) != neg.end()) return ctx.bool_val(true);
            pos.insert(x);
        }

        // dedup exact literal
        if (seen.insert(x).second) uniq.push_back(x);
    }

    if (uniq.empty()) return ctx.bool_val(false);
    if (uniq.size() == 1) return uniq[0];

    // Canonical ordering to maximize interning hits
    std::sort(uniq.begin(), uniq.end(),
              [](const z3::expr &lhs, const z3::expr &rhs) {
                auto hl = Z3_get_ast_hash(lhs.ctx(), lhs);
                auto hr = Z3_get_ast_hash(rhs.ctx(), rhs);
                if (hl != hr) return hl < hr;

                if (Z3_is_eq_ast(lhs.ctx(), lhs, rhs)) return false;
                return Z3_ast(lhs) < Z3_ast(rhs);
              });

    return mkNaryOr(ctx, uniq);
}

z3::expr FeasibilityAnalysisManager::mkAndSimplified(const z3::expr &a,
                                                    const z3::expr &b) {
  z3::context &ctx = a.ctx();

  std::vector<z3::expr> args;
  args.reserve(8);
  collectAndArgs(a, args);
  collectAndArgs(b, args);

  // Dedup of full literals (including (not p))
  std::unordered_set<z3::expr, Z3ExprHash, Z3ExprEq> seen;
  seen.reserve(args.size());

  // For fast contradiction detection:
  // store base terms in pos/neg, where literal is either base or (not base)
  std::unordered_set<z3::expr, Z3ExprHash, Z3ExprEq> pos, neg;
  pos.reserve(args.size());
  neg.reserve(args.size());

  std::vector<z3::expr> uniq;
  uniq.reserve(args.size());

  for (auto &x : args) {
    if (isBoolFalse(x)) return ctx.bool_val(false);
    if (isBoolTrue(x)) continue;

    // normalize literal into (base, isNeg)
    if (isNotExpr(x)) {
      z3::expr base = x.arg(0);
      // base AND (not base) => false
      if (pos.find(base) != pos.end()) return ctx.bool_val(false);
      neg.insert(base);
    } else {
      // x AND (not x) => false
      if (neg.find(x) != neg.end()) return ctx.bool_val(false);
      pos.insert(x);
    }

    // dedup exact literal
    if (seen.insert(x).second) uniq.push_back(x);
  }

  if (uniq.empty()) return ctx.bool_val(true);
  if (uniq.size() == 1) return uniq[0];

  // Canonical ordering to maximize interning hits:
  // sort by AST hash, tie-break by structural equality fallback.
  std::sort(uniq.begin(), uniq.end(),
            [](const z3::expr &lhs, const z3::expr &rhs) {
              auto hl = Z3_get_ast_hash(lhs.ctx(), lhs);
              auto hr = Z3_get_ast_hash(rhs.ctx(), rhs);
              if (hl != hr) return hl < hr;
              // Stable tie-breaker: if structurally equal, not less; otherwise
              // fall back to pointer address for strict weak ordering.
              if (Z3_is_eq_ast(lhs.ctx(), lhs, rhs)) return false;
              return Z3_ast(lhs) < Z3_ast(rhs);
            });

    return mkNaryAnd(ctx, uniq);
}



z3::expr FeasibilityAnalysisManager::simplify(z3::expr input) {
    if (input.is_true()) {
        return Context->bool_val(true);
    }

    if (input.is_false()) {
        return Context->bool_val(false);
    }

    return input;
}

uint32_t FeasibilityAnalysisManager::mkNot(z3::expr a) {
    z3::expr f = !a;
    Formulas.push_back(f);
    return Formulas.size() - 1;
}

const z3::expr& FeasibilityAnalysisManager::getExpression(uint32_t id) const {
    return Formulas[id];
}

std::optional<uint32_t> FeasibilityAnalysisManager::findFormulaId(const z3::expr& expr) const {
    // llvm::errs() << "\t\t\t" << "Finding formula ID for expression: " << expr.to_string() << "\n";

    auto it = FormularsToId.find(expr);
    if (it == FormularsToId.end()) {
        return std::nullopt;
    }

    return it->second;
}

bool FeasibilityAnalysisManager::isSat(uint32_t id) {
    if (id >= SatCache.size()) return false;

    auto &c = SatCache[id];
    if (c == SatTri::Sat)   return true;
    if (c == SatTri::Unsat) return false;

    const z3::expr e = getExpression(id);

    Solver.push();
    Solver.add(e);
    const bool sat = (Solver.check() == z3::sat);
    Solver.pop();

    c = sat ? SatTri::Sat : SatTri::Unsat;
    return sat;
}

uint32_t FeasibilityAnalysisManager::extendEnv(uint32_t baseEnvId, const llvm::Value* k, const llvm::Value* v) {
    EnvPool.push_back(EnvNode{EnvRoots[baseEnvId], k, v});
    EnvRoots.push_back(&EnvPool.back());
    return (uint32_t)EnvRoots.size() - 1;
}

const llvm::Value* FeasibilityAnalysisManager::resolve(uint32_t envId, const llvm::Value* v) const {
    // follow bindings (could add path compression cache later)
    if (envId == 0) {
        return v;
    };
    for (auto* n = EnvRoots[envId]; n; n = n->parent) {
        if (n->key == v) return n->val;
    }
    return v;
}

uint32_t FeasibilityAnalysisManager::applyPhiPack(uint32_t inEnvId, const llvm::BasicBlock* pred, const llvm::BasicBlock* succ) {
    uint32_t env = inEnvId;

    for (auto &I : *succ) {
        auto *phi = llvm::dyn_cast<llvm::PHINode>(&I);
        if (!phi) {
            break;
        };

        const llvm::Value* incoming = phi->getIncomingValueForBlock(pred);

        llvm::errs() << "Applying phi-pack for phi node: " << *phi << "\n";
        llvm::errs() << "\tIncoming value before " << pred->getName() << ": " << *incoming << "\n";

        // Resolve incoming through current env (handles phi-chains)
        incoming = resolve(env, incoming);

        llvm::errs() << "\tIncoming value after resolve " << pred->getName() << ": " << *incoming << "\n";

        // bind: phi-result := incoming
        llvm::errs() << "env before: " << env << "\n";
        env = extendEnv(env, phi, incoming);
        llvm::errs() << "env after: " << env << "\n";
    }
    return env;
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
    uint32_t newId = manager->mkOr(this->formularID, other.formularID);
    return FeasibilityElement(Kind::Normal, newId, manager);
}

FeasibilityAnalysisManager *FeasibilityElement::getManager() {
    return this->manager;
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
