/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_FEASIBILITYELEMENT_H
#define SPEAR_FEASIBILITYELEMENT_H

#include <optional>
#include <string>

#include <llvm/Support/raw_ostream.h>
#include <z3++.h>
#include <llvm/IR/Instructions.h>

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


/**
 *
 * FeasibilityElement represents an element in the lattice of the feasibility analysis.
 * It can be Top (representing the empty set, i.e., true), Bottom (error state), Normal (a specific formula), or Empty ().
 */
class FeasibilityElement {
public:
  /**
   * Type enum for the kind of element.
   * Top represents the trivially true set, Bottom represents an error state, Normal represents a
   * specific set of atomic formulas, and Empty represents an explicitly empty set.
   * - Bottom represents an error state, which is not represented by a formula set but directly by the Kind::Bottom.
   * - Normal represents a specific set of atomic formulas, which is represented by a formula ID that can be looked up
   * in the manager's formula storage.
   * - Empty represents an explicitly empty set of formulas, which is represented with a different kind to distinguish
   * it realizes the top element in our lattice
   * it from the trivially true set. Used for initialization.
   */
  enum class Kind : uint8_t { Bottom = 1, Normal = 2, Empty = 3 };

  /**
   * Id of the trivially true set (Top), which is represented by the empty set of formulas.
   * This is a special reserved ID in the manager's formula storage.
   */
  static constexpr uint32_t topId = 0;

  /**
   * Bottom is not represented by a formula set, but directly by the Kind::Bottom.
   * However, we reserve an ID in the manager's formula storage for it as well (bottomId),
   * which is not used in our representation but can be used as a placeholder if needed.
   */
  static constexpr uint32_t bottomId = 1;

  /**
   * Dummy constructor to create a default Top element.
   * The actual initialization should be done through the createElement factory method.
   */
  FeasibilityElement() noexcept : kind(Kind::Empty), formularID(topId), manager(nullptr), envId(0) {}

  /**
   * Create a new FeasibilityElement with the given manager, formula ID, kind, and environment ID.
   * @param manager Manager instance to which this element belongs
   * @param formulaId Id of the set of formulas this element represents
   * @param kind Kind of the element (Top, Bottom, Normal, Empty)
   * @param envId Environment ID representing variable bindings for this element (default is 0, meaning no bindings)
   * @return Returns a newly created FeasibilityElement with the specified properties.
   */
  static FeasibilityElement createElement(FeasibilityAnalysisManager *manager,
                                          uint32_t formulaId, Kind kind,
                                          uint32_t envId = 0) noexcept;

  /**
   * Checks if the element is Top
   * @return true if the element is Top, false otherwise
   */
  bool isTop() const noexcept {
    return kind == Kind::Empty;
  }

  /**
   * Checks if the element is Bottom
   * @return true if the element is Bottom, false otherwise
   */
  bool isBottom() const noexcept {
    return kind == Kind::Bottom;
  }

  /**
   * Checks if the element is Normal
   * @return true if the element is Normal, false otherwise
   */
  bool isNormal() const noexcept {
    return kind == Kind::Normal;
  }

  /**
   * Checks if the element is Empty
   * @return true if the element is Empty, false otherwise
   */
  bool isEmpty() const noexcept {
    return kind == Kind::Empty;
  }

  /**
   * Get the manager instance associated with this element.
   * @return Manager instance associated with this element, or nullptr if not set
   */
  FeasibilityAnalysisManager *getManager() const noexcept {
    return manager;
  }

  /**
   * Get the kind of this element (Top, Bottom, Normal, Empty).
   * @return Kind of this element
   */
  Kind getKind() const noexcept {
    return kind;
  }

  /**
   * Get the formula ID associated with this element, which represents the set of atomic formulas it corresponds to.
   * The formula ID is an index into the manager's formula storage, where the actual set of formulas can be retrieved.
   * @return Formula ID associated with this element, or topId if this is a Top element
   */
  uint32_t getFormulaId() const noexcept {
    return formularID;
  }

  /**
   * Get the environment ID associated with this element, which represents the variable bindings for this element.
   * The environment ID is an index into the manager's environment storage, where the actual
   * variable bindings can be retrieved.
   * @return Environment ID associated with this element, or 0 if this element has no variable bindings
   */
  uint32_t getEnvId() const noexcept {
    return envId;
  }

  /**
   * Set the formula ID for this element. This is used to update the set of atomic formulas this element represents.
   * @param id Id the element should now represent, which is an index into the manager's formula storage
   */
  void setFormulaId(uint32_t id) noexcept {
    formularID = id;
  }

  /**
   * Set the environment ID for this element. This is used to update the variable bindings for this element.
   * @param id Id of the environment this element should now represent,
   * which is an index into the manager's environment storage
   */
  void setEnvId(uint32_t id) noexcept {
    envId = id;
  }

  /**
   * Set the kind of this element (Top, Bottom, Normal, Empty).
   * @param k Kind to set for this element
   */
  void setKind(Kind k) noexcept {
    kind = k;
  }

  /**
   * Implement the join operation for the lattice of FeasibilityElements.
   * The join operation combines two elements according to the lattice rules:
   * - If either element is Top, the result is the other element (Top is neutral for join).
   * - If either element is Bottom, the result is Bottom (Bottom dominates).
   * - If either element is Empty, the result is the other element (Empty is neutral for join).
   * - If both elements are Normal, the result is a new Normal element representing the intersection of their
   * formula sets.
   * @param other The other FeasibilityElement to join with this element
   * @return The result of joining this element with the other element, according to the lattice rules described above.
   */
  FeasibilityElement join(const FeasibilityElement &other) const;

  /**
   * Convert this FeasibilityElement to a human-readable string representation for debugging purposes.
   * The string includes the kind of the element, its formula ID, and its environment ID.
   * @return A string representation of this FeasibilityElement.
   */
  std::string toString() const;

  /**
   * Equality operator for FeasibilityElement.
   * Two elements are considered equal if they have the same kind, formula ID, environment ID, and manager instance.
   * @param other The other FeasibilityElement to compare with this element
   * @return true if this element is equal to the other element, false otherwise
   */
  bool operator==(const FeasibilityElement &other) const noexcept {
    return kind == other.kind && formularID == other.formularID && envId == other.envId && manager == other.manager;
  }

  /**
   * Inequality operator for FeasibilityElement, defined as the negation of the equality operator.
   * @param other The other FeasibilityElement to compare with this element
   * @return true if this element is not equal to the other element, false otherwise
   */
  bool operator!=(const FeasibilityElement &other) const noexcept {
    return !(*this == other);
  }

private:
  /**
   * Private constructor for FeasibilityElement. This is used by the createElement factory method to create new
   * elements with specific properties.
   * @param kind Kind of the element (Top, Bottom, Normal, Empty)
   * @param formularID Formula ID representing the set of atomic formulas this element corresponds to
   * @param manager Manager instance to which this element belongs
   * @param envid Environment ID representing variable bindings for this element
   */
  FeasibilityElement(Kind kind, uint32_t formularID, FeasibilityAnalysisManager *manager, uint32_t envid) noexcept
      : kind(kind), formularID(formularID), manager(manager), envId(envid) {}

  /**
   * Kind of the element (Top, Bottom, Normal, Empty). This determines the general properties of the element in the lattice.
   * Default is Top, which represents the empty set of formulas (i.e., true).
   *
   */
  Kind kind{Kind::Empty};

  /**
   * Formula ID representing the set of atomic formulas this element corresponds to.
   * This is an index into the manager's formula storage. Default is topId, which represents the empty set of formulas.
   */
  uint32_t formularID{topId};

  /**
   * Manager instance to which this element belongs. This is used to access the manager's formula and
   * environment storage. Default is nullptr.
   */
  FeasibilityAnalysisManager *manager{nullptr};

  /**
   * Environment ID representing variable bindings for this element. This is an index into the manager's
   * environment storage. Default is 0, which represents no variable bindings (empty environment).
   */
  uint32_t envId{0};
};

/**
 * ToString function for optional FeasibilityElement.
 * If the optional has a value, it returns the string representation of the contained FeasibilityElement;
 * otherwise, it returns "nullopt".
 * @param element Optional FeasibilityElement to convert to string
 * @return String representation of the optional FeasibilityElement, or "nullopt" if it has no value
 */
std::string toString(const std::optional<FeasibilityElement> &element);

/**
 * Overleaded stream insertion operator for FeasibilityElement.
 * @param os Outputstream to which the FeasibilityElement will be written
 * @param element FeasibilityElement to write to the output stream
 * @return Reference to the output stream after writing the FeasibilityElement
 */
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const FeasibilityElement &element);

} // namespace Feasibility

#endif // SPEAR_FEASIBILITYELEMENT_H
