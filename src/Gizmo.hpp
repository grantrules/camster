#pragma once

#include <vector>

#include "ColorVertex.hpp"

// Generates the axis gizmo – three colored arrows (RGB = XYZ) emanating from
// the origin, each with a small arrowhead.  The geometry is constant and
// regenerated on demand as a flat array of LINE_LIST vertex pairs.
class Gizmo {
 public:
  // Append gizmo line vertices to `lines`.
  void appendLines(std::vector<ColorVertex>& lines) const;
};
