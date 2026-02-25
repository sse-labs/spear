// FeasibilityAnalysisManager.cpp
#include "analyses/feasibility/FeasibilityElement.h"

#include <algorithm>
#include <sstream>
#include <llvm/IR/Instructions.h>

#include "analyses/feasibility/FeasibilityAnalysisManager.h"

namespace Feasibility {

FeasibilityElement FeasibilityElement::createElement(FeasibilityAnalysisManager *man, uint32_t formulaId, Kind type,
    uint32_t envId) noexcept {
  return FeasibilityElement(type, formulaId, man, envId);
}

FeasibilityElement FeasibilityElement::join(const FeasibilityElement &other) const {
  // If either is Top → return the other (Top is neutral for join)
  if (isTop()) return other;
  if (other.isTop()) return *this;

  // If either is Bottom → Bottom dominates
  if (isBottom()) return *this;
  if (other.isBottom()) return other;

  // Empty ∩ X = X
  if (isEmpty()) return other;
  if (other.isEmpty()) return *this;

  // Normal ∩ Normal = intersection
  uint32_t newId = manager->intersect(formularID, other.formularID);

  if (newId == topId) {
    // intersection empty → Empty
    return createElement(manager, topId, Kind::Empty, 0);
  }

  return createElement(manager, newId, Kind::Normal, 0);
}

std::string FeasibilityElement::toString() const {
  std::ostringstream os;
  os << "FeasibilityElement{kind=";
  switch (kind) {
    case Kind::Top: os << "Top"; break;
    case Kind::Bottom: os << "Bottom"; break;
    case Kind::Normal: os << "Normal"; break;
    case Kind::Empty: os << "Empty"; break;
  }
  os << ", fid=" << formularID << ", env=" << envId << "}";
  return os.str();
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const FeasibilityElement &E) {
  os << E.toString();
  return os;
}

std::string toString(const std::optional<FeasibilityElement> &E) {
  if (!E) return "nullopt";
  return E->toString();
}

} // namespace Feasibility