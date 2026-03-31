#pragma once

#include <optional>
#include <vector>

#include "ColorVertex.hpp"
#include "Scene.hpp"
#include "sketch/Primitive.hpp"

// A Sketch is an ordered collection of 2D primitives that live on a single
// plane.  It stores geometry in the plane's local 2D coordinate system and
// can tessellate everything to 3D colored line segments for rendering.
class Sketch {
 public:
  void addPrimitive(SketchPrimitive prim);
  void clear();
  bool empty() const;
  const std::vector<SketchPrimitive>& primitives() const;

  // --- Selection ---
  void select(size_t idx);
  void toggleSelect(size_t idx);
  void clearSelection();
  bool isSelected(size_t idx) const;
  bool hasSelection() const;
  std::vector<size_t> selectedIndices() const;

  // Find the nearest primitive to `pos` within `threshold` (2D plane coords).
  std::optional<size_t> hitTest(glm::vec2 pos, float threshold) const;

  // Select all primitives whose bounding box overlaps the given rect.
  // `replace` clears existing selection first; `add` keeps it.
  void selectInRect(glm::vec2 rectMin, glm::vec2 rectMax);
  void addToSelectInRect(glm::vec2 rectMin, glm::vec2 rectMax);

  // Tessellate all primitives into colored line-list vertices in world space.
  // Selected primitives use `selectedColor`.
  void appendLines(std::vector<ColorVertex>& lines, SketchPlane plane,
                   const glm::vec4& color = glm::vec4(1.0f),
                   const glm::vec4& selectedColor = glm::vec4(1.0f, 0.6f, 0.0f, 1.0f)) const;

 private:
  std::vector<SketchPrimitive> primitives_;
  std::vector<bool> selected_;
};
