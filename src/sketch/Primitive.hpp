#pragma once

#include <cstddef>
#include <variant>
#include <vector>

#include <glm/vec2.hpp>

// 2D sketch primitives.  Coordinates are in the sketch plane's local 2D space
// and are stored in millimeters (the canonical internal unit).

struct SketchLine {
  glm::vec2 start;
  glm::vec2 end;
};

struct SketchRect {
  glm::vec2 min;
  glm::vec2 max;
};

struct SketchCircle {
  glm::vec2 center;
  float radius = 0.0f;
};

struct SketchArc {
  glm::vec2 center;
  float radius = 0.0f;
  float startAngle = 0.0f;  // radians
  float sweepAngle = 0.0f;  // radians, positive = CCW
};

using SketchPrimitive = std::variant<SketchLine, SketchRect, SketchCircle, SketchArc>;

enum class SketchDimensionKind {
  Length,
  RectangleWidth,
  RectangleHeight,
  Diameter,
};

struct SketchDimensionSpec {
  SketchDimensionKind kind;
  float valueMm = 0.0f;
};

struct CompletedSketchPrimitive {
  SketchPrimitive geometry;
  std::vector<SketchDimensionSpec> dimensions;
};

// How well-constrained is an element?
enum class ConstraintStatus {
  Unconstrained,     // no constraints at all
  UnderConstrained,  // some constraints, but DOF > 0
  FullyConstrained,  // DOF == 0
  OverConstrained,   // conflicting or redundant constraints
};

// Wrapper that adds metadata to a geometric primitive.
struct SketchElement {
  SketchPrimitive geometry;
  bool construction = false;
  ConstraintStatus status = ConstraintStatus::Unconstrained;

  // Intrinsic degrees of freedom for this element type.
  int baseDof() const;

  // Get control points (for snapping / coincident detection).
  std::vector<glm::vec2> controlPoints() const;
};

// Return the number of DOF for a bare primitive type.
inline int baseDofFor(const SketchPrimitive& p) {
  return std::visit([](const auto& v) -> int {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, SketchLine>)   return 4;  // 2 endpoints
    if constexpr (std::is_same_v<T, SketchRect>)   return 4;  // 2 corners
    if constexpr (std::is_same_v<T, SketchCircle>) return 3;  // cx, cy, r
    if constexpr (std::is_same_v<T, SketchArc>)    return 5;  // cx, cy, r, start, sweep
  }, p);
}

inline int SketchElement::baseDof() const { return baseDofFor(geometry); }

inline std::vector<glm::vec2> SketchElement::controlPoints() const {
  return std::visit([](const auto& v) -> std::vector<glm::vec2> {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, SketchLine>)   return {v.start, v.end};
    if constexpr (std::is_same_v<T, SketchRect>)   return {v.min, v.max};
    if constexpr (std::is_same_v<T, SketchCircle>) return {v.center};
    if constexpr (std::is_same_v<T, SketchArc>)    return {v.center};
  }, geometry);
}
