/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_FEASIBILITYELEMENT_H
#define SPEAR_FEASIBILITYELEMENT_H

#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

#include <llvm/Support/raw_ostream.h>
#include <z3++.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>

#include <phasar/DataFlow/IfdsIde/EdgeFunction.h>

namespace Feasibility {


// Forward declaration
class FeasibilityAnalysisManager;

/**
* ICMP and phi aware propagation atom
  * Each LazyAtom carries the CFG edge (Pred->Succ) whose PHIs must be applied
  * before evaluating the ICmp atom.
  */
struct LazyAtom {
  /**
   * Previous block the atom is coming from
   */
  const llvm::BasicBlock *PredBB = nullptr;

  /**
   * Successor block the atom will be goin to
   */
  const llvm::BasicBlock *SuccBB = nullptr;

  /**
   * ICMP instruction realizing the underlying comparison
   */
  const llvm::ICmpInst *icmp = nullptr;

  /**
   * Flag to detect whether we are in the edge realizing the true outcome of the comparison or in the false outcome
   */
  bool TrueEdge = true;

  /**
   * Dummy default constructor
   */
  LazyAtom() = default;

  /**
   * Actual initialization constructor. Creates a new LazyAtom
   * @param predecessorBlock BasicBlock the atom originates from
   * @param successorBlock BasicBlock the atom is flowing towards
   * @param icmpInstruction Pointer to the ICMP instruction the atom is build upon
   * @param areWeOnTheTrueEdge Flag to detect the boolean state of the underlying edge
   */
  LazyAtom(
    const llvm::BasicBlock *predecessorBlock,
    const llvm::BasicBlock *successorBlock,
    const llvm::ICmpInst *icmpInstruction,
    bool areWeOnTheTrueEdge)
  : PredBB(predecessorBlock), SuccBB(successorBlock), icmp(icmpInstruction), TrueEdge(areWeOnTheTrueEdge) {}

  /**
   * Comparison operator. Check equality of LazyAtoms depending on blocks, underlying ICMP instruction and the
   * underlying edge boolean state.
   * @param other
   * @return
   */
  bool operator==(const LazyAtom &other) const noexcept {
    return PredBB == other.PredBB && SuccBB == other.SuccBB && icmp == other.icmp && TrueEdge == other.TrueEdge;
  }
};



// ============================================================================
// FeasibilityElement
// ============================================================================
class FeasibilityElement {
public:
  enum class Kind : uint8_t { Top = 0, Bottom = 1, Normal = 2, Empty = 3 };

  static constexpr uint32_t topId    = 0; // empty set == true
  static constexpr uint32_t bottomId = 1; // reserved (unused for sets)

  FeasibilityElement() noexcept
      : kind(Kind::Top), formularID(topId), manager(nullptr), envId(0) {}

  static FeasibilityElement createElement(FeasibilityAnalysisManager *man,
                                          uint32_t formulaId, Kind type,
                                          uint32_t envId = 0) noexcept;

  bool isTop() const noexcept { return kind == Kind::Top; }
  bool isBottom() const noexcept { return kind == Kind::Bottom; }
  bool isNormal() const noexcept { return kind == Kind::Normal; }
  bool isEmpty() const noexcept { return kind == Kind::Empty; }

  FeasibilityAnalysisManager *getManager() const noexcept { return manager; }
  Kind getKind() const noexcept { return kind; }
  uint32_t getFormulaId() const noexcept { return formularID; }
  uint32_t getEnvId() const noexcept { return envId; }

  void setFormulaId(uint32_t id) noexcept { formularID = id; }
  void setEnvId(uint32_t id) noexcept { envId = id; }
  void setKind(Kind k) noexcept { kind = k; }

  /// MUST-join (intersection) with reachability semantics:
  /// ⊥ ⊔ x = x
  /// x ⊔ ⊥ = x
  /// Top = empty set (true), absorbing for intersection.
  FeasibilityElement join(const FeasibilityElement &other) const;

  std::string toString() const;

  bool operator==(const FeasibilityElement &other) const noexcept {
    return kind == other.kind && formularID == other.formularID &&
           envId == other.envId && manager == other.manager;
  }
  bool operator!=(const FeasibilityElement &other) const noexcept {
    return !(*this == other);
  }

private:
  friend class FeasibilityAnalysisManager;

  FeasibilityElement(Kind k, uint32_t fid, FeasibilityAnalysisManager *m,
                     uint32_t e) noexcept
      : kind(k), formularID(fid), manager(m), envId(e) {}

  Kind kind{Kind::Top};
  uint32_t formularID{topId};
  FeasibilityAnalysisManager *manager{nullptr};
  uint32_t envId{0};
};

std::string toString(const std::optional<FeasibilityElement> &E);

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const FeasibilityElement &E);

} // namespace Feasibility

#endif // SPEAR_FEASIBILITYELEMENT_H