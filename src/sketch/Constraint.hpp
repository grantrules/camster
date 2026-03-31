#pragma once

#include <cstddef>
#include <variant>

// All constraint types.  Element indices refer to positions in the Sketch's
// element list.  Point indices (0, 1, …) refer to the control-point order
// returned by SketchElement::controlPoints().

// Two points must coincide.
struct CoincidentConstraint {
  size_t elemA;
  int pointA;  // which control point on elemA
  size_t elemB;
  int pointB;
};

// A line's start.y == end.y (or the relevant axis for the element).
struct HorizontalConstraint {
  size_t elem;
};

// A line's start.x == end.x.
struct VerticalConstraint {
  size_t elem;
};

// A specific point is locked to a position.
struct FixedConstraint {
  size_t elem;
  int point;  // which control point
  float x;
  float y;
};

// Line must have a specific length, or circle a specific radius.
struct LengthConstraint {
  size_t elem;
  float valueMm;
};

// Circle or arc must have a specific radius.
struct RadiusConstraint {
  size_t elem;
  float valueMm;
};

// Angle between a line and the horizontal axis.
struct AngleConstraint {
  size_t elem;
  float degrees;
};

// Two lines must be parallel.
struct ParallelConstraint {
  size_t elemA;
  size_t elemB;
};

// Two lines must be perpendicular.
struct PerpendicularConstraint {
  size_t elemA;
  size_t elemB;
};

// Two elements must have equal length (lines) or equal radius (circles/arcs).
struct EqualConstraint {
  size_t elemA;
  size_t elemB;
};

// Arc or circle is tangent to a line at the connection point.
struct TangentConstraint {
  size_t elemA;
  size_t elemB;
};

// A point lies at the midpoint of a line.
struct MidpointConstraint {
  size_t lineElem;
  size_t pointElem;
  int point;
};

using SketchConstraint =
    std::variant<CoincidentConstraint, HorizontalConstraint, VerticalConstraint,
                 FixedConstraint, LengthConstraint, RadiusConstraint,
                 AngleConstraint, ParallelConstraint, PerpendicularConstraint,
                 EqualConstraint, TangentConstraint, MidpointConstraint>;

// Identifies which kind of constraint the user wants to apply.  Shared
// between the toolbar UI and the constraint-application logic in Sketch.
enum class ConstraintTool {
  None,
  Horizontal,
  Vertical,
  Coincident,
  Perpendicular,
  Parallel,
  Equal,
  Fixed,
  Tangent,
  Midpoint,
  Length,
  Radius,
  Angle,
};

// How many DOF does this constraint remove?
inline int constraintDofReduction(const SketchConstraint& c) {
  return std::visit([](const auto& v) -> int {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, CoincidentConstraint>)    return 2;
    if constexpr (std::is_same_v<T, HorizontalConstraint>)    return 1;
    if constexpr (std::is_same_v<T, VerticalConstraint>)      return 1;
    if constexpr (std::is_same_v<T, FixedConstraint>)         return 2;
    if constexpr (std::is_same_v<T, LengthConstraint>)        return 1;
    if constexpr (std::is_same_v<T, RadiusConstraint>)        return 1;
    if constexpr (std::is_same_v<T, AngleConstraint>)         return 1;
    if constexpr (std::is_same_v<T, ParallelConstraint>)      return 1;
    if constexpr (std::is_same_v<T, PerpendicularConstraint>) return 1;
    if constexpr (std::is_same_v<T, EqualConstraint>)         return 1;
    if constexpr (std::is_same_v<T, TangentConstraint>)       return 1;
    if constexpr (std::is_same_v<T, MidpointConstraint>)      return 2;
  }, c);
}

// Which elements does this constraint reference?
inline void constraintElements(const SketchConstraint& c, size_t* a, size_t* b) {
  std::visit([&](const auto& v) {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, CoincidentConstraint>) {
      *a = v.elemA; *b = v.elemB;
    } else if constexpr (std::is_same_v<T, ParallelConstraint> ||
                         std::is_same_v<T, PerpendicularConstraint> ||
                         std::is_same_v<T, EqualConstraint> ||
                         std::is_same_v<T, TangentConstraint>) {
      *a = v.elemA; *b = v.elemB;
    } else if constexpr (std::is_same_v<T, MidpointConstraint>) {
      *a = v.lineElem; *b = v.pointElem;
    } else {
      // Single-element constraints.
      *a = v.elem; *b = v.elem;
    }
  }, c);
}
