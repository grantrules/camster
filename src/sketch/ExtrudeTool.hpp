#pragma once

#include <vector>

#include <glm/vec3.hpp>

#include "ColorVertex.hpp"
#include "Scene.hpp"
#include "StlMesh.hpp"

// Interactive extrude tool.  Once begun with a set of closed profiles,
// the user can drag the mouse or type a distance to set extrusion depth.
// Call confirm() to produce the final mesh, or cancel() to abort.
class ExtrudeTool {
 public:
  // Start an extrusion with the given closed 2D profiles on `plane`.
  void begin(std::vector<std::vector<glm::vec2>> profiles, SketchPlane plane);

  // Feed a mouse ray for interactive distance adjustment.
  void mouseDown(glm::vec3 rayOrigin, glm::vec3 rayDir);
  void mouseMove(glm::vec3 rayOrigin, glm::vec3 rayDir);
  void mouseUp();

  // Directly set the extrusion distance (from the input field).
  void setDistance(float mm);
  float distance() const;

  bool active() const;
  SketchPlane plane() const;
  const std::vector<std::vector<glm::vec2>>& profiles() const;

  void cancel();
  StlMesh confirm();

  // Append preview wireframe lines for the extruded shape.
  void appendPreview(std::vector<ColorVertex>& lines) const;

 private:
  // Closest-approach parameter on the extrude axis from a mouse ray.
  float axisParam(glm::vec3 rayOrigin, glm::vec3 rayDir) const;

  bool active_ = false;
  bool dragging_ = false;
  float distance_ = 0.0f;
  float dragStartDistance_ = 0.0f;
  float dragRefParam_ = 0.0f;

  SketchPlane plane_ = SketchPlane::XY;
  std::vector<std::vector<glm::vec2>> profiles_;
  glm::vec3 centroid3D_{0.0f};
  glm::vec3 normal_{0.0f, 0.0f, 1.0f};
};
