#pragma once

#include <optional>
#include <vector>

#include "ColorVertex.hpp"
#include "Scene.hpp"

// Generates the axis gizmo – three colored arrows (RGB = XYZ) emanating from
// the origin, each with a small arrowhead. Also renders small square plane
// indicators (XY, XZ, YZ) positioned between the axes for visual feedback.
// The geometry is constant and regenerated on demand as a flat array of
// LINE_LIST vertex pairs.
class Gizmo {
 public:
  // Append gizmo line vertices to `lines` (arrows + plane indicators).
  void appendLines(std::vector<ColorVertex>& lines) const;

  // Test if a ray hits any plane indicator. Returns the closest plane.
  // The plane is positioned at the origin with the indicator size.
  std::optional<SketchPlane> rayHitsPlaneIndicator(
      glm::vec3 rayOrigin, glm::vec3 rayDir) const;

  // Append highlighted plane indicator lines (for hover feedback).
  void appendHighlightedPlane(std::vector<ColorVertex>& lines,
                              SketchPlane plane) const;

  // Size of the plane indicator square (in world coordinates).
  static constexpr float kPlaneSize = 0.4f;
};
