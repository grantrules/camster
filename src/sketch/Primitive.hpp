#pragma once

#include <variant>

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
