#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ColorVertex.hpp"
#include "Scene.hpp"
#include "Units.hpp"
#include "sketch/Constraint.hpp"
#include "sketch/Primitive.hpp"

struct SketchDimensionLabel {
  glm::vec3 worldPos;
  std::string text;
};

// A Sketch is an ordered collection of 2D elements (primitives + metadata)
// that live on a single plane.  It manages constraints, DOF tracking, and
// selection, and can tessellate everything to colored line segments.
class Sketch {
 public:
  // --- Element management ---
  void addPrimitive(SketchPrimitive prim);
  void addCompletedPrimitive(CompletedSketchPrimitive prim);
  void addElement(SketchElement elem);
  void deleteSelected();
  void clear();
  bool empty() const;

  const std::vector<SketchElement>& elements() const;
  std::vector<SketchElement>& elements();
  // Legacy accessor — returns just the geometry.
  std::vector<SketchPrimitive> primitives() const;

  // --- Construction toggle ---
  void toggleConstruction(size_t idx);

  // --- Constraints ---
  void addConstraint(SketchConstraint c);
  const std::vector<SketchConstraint>& constraints() const;
  // Apply a single-step constraint solve and update DOF status.
  void solveConstraints();

  // High-level: apply the named constraint tool to the current selection.
  // For dimensional constraints, `valueMm` must be set (length/radius in mm,
  // angle in degrees).  Returns a status message describing what happened.
  std::string applyConstraintToSelection(ConstraintTool tool, float valueMm = 0.0f);

  // --- Selection ---
  void select(size_t idx);
  void toggleSelect(size_t idx);
  void clearSelection();
  bool isSelected(size_t idx) const;
  bool hasSelection() const;
  std::vector<size_t> selectedIndices() const;

  std::optional<size_t> hitTest(glm::vec2 pos, float threshold) const;
  void selectInRect(glm::vec2 rectMin, glm::vec2 rectMax);
  void addToSelectInRect(glm::vec2 rectMin, glm::vec2 rectMax);

  // --- Snap ---
  // Find the nearest control point within `threshold`.
  std::optional<glm::vec2> snapToPoint(glm::vec2 pos, float threshold) const;
  std::vector<std::vector<glm::vec2>> closedProfiles(float tolerance = 0.5f) const;
  std::vector<glm::vec2> danglingEndpoints(float tolerance = 0.5f) const;

  // --- Rendering ---
  void appendLines(std::vector<ColorVertex>& lines, SketchPlane plane) const;
  // Append constraint annotation lines (dimension leaders, icons).
  void appendConstraintAnnotations(std::vector<ColorVertex>& lines, SketchPlane plane) const;
  void appendConstraintLabels(std::vector<SketchDimensionLabel>& labels, SketchPlane plane,
                              Unit unit) const;

  // --- Undo / Redo ---
  // Push the current state onto the undo stack (called automatically before mutations).
  void undoPush();
  bool canUndo() const;
  bool canRedo() const;
  void undo();
  void redo();

  // --- Direct state access (for serialization / history replay) ---
  // Returns a non-const reference so that replay code can overwrite the state.
  std::vector<SketchConstraint>& constraints();

 private:
  void applyOneConstraint(const SketchConstraint& c);
  void updateConstraintStatus();

  std::vector<SketchElement> elements_;
  std::vector<SketchConstraint> constraints_;
  std::vector<bool> selected_;

  // Undo / redo stacks. Each entry is a full snapshot of (elements_, constraints_).
  struct SketchSnapshot {
    std::vector<SketchElement> elements;
    std::vector<SketchConstraint> constraints;
  };
  static constexpr size_t kMaxUndoLevels = 100;
  std::vector<SketchSnapshot> undoStack_;
  std::vector<SketchSnapshot> redoStack_;
};
